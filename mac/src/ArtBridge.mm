// See ArtBridge.h. Counterpart of ime/SampleIME/MspyBridge.cpp.

#import "ArtBridge.h"

// Only for NSApplicationWillTerminateNotification, which is where the
// preference store gets its last chance to be written.
#import <AppKit/AppKit.h>

#include <os/log.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "McBopomofoLM.h"
#include "composer.h"
#include "relaxed_tone_lm.h"
#include "user_preferences.h"

// ---------------------------------------------------------------- logging --

// os_log with an explicit {public} rather than NSLog. The unified logging
// system redacts the arguments of %@ -- Console.app shows the line, with
// every value replaced by <private> -- which quietly made this whole
// facility useless, the always-on failure messages included. A format
// specifier is the only thing that decides it; there is no defaults key.
static void ArtEmit(NSString *message) {
    os_log(OS_LOG_DEFAULT, "[ArtShuangpin] %{public}s",
           message.UTF8String ?: "(null)");
}

void ArtLog(NSString *format, ...) {
    static BOOL enabled = NO;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        enabled = [[NSUserDefaults standardUserDefaults] boolForKey:@"debug"];
        // Says so once, so "is the flag on?" is answerable from the log
        // itself rather than by trusting that the app was restarted after
        // the defaults write.
        if (enabled) {
            ArtEmit(@"debug logging is ON");
        }
    });
    if (!enabled) {
        return;
    }
    va_list args;
    va_start(args, format);
    NSString *message = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);
    ArtEmit(message);
}

void ArtLogAlways(NSString *format, ...) {
    va_list args;
    va_start(args, format);
    NSString *message = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);
    ArtEmit(message);
}

// ------------------------------------------------------------ conversions --

namespace {

NSString *ArtStringFromUTF8(const std::string &utf8) {
    if (utf8.empty()) {
        return @"";
    }
    NSString *out = [[NSString alloc] initWithBytes:utf8.data()
                                             length:utf8.size()
                                           encoding:NSUTF8StringEncoding];
    return out ?: @"";
}

}  // namespace

// -------------------------------------------------------------- ArtResult --

@interface ArtResult ()
@property (nonatomic, readwrite) BOOL consumed;
@property (nonatomic, copy, readwrite) NSString *commitText;
// Declared here rather than only in the @implementation so ArtBridge, later
// in this file, can see them.
+ (instancetype)resultWith:(const mspy::Composer::Result &)result;
+ (instancetype)ignored;
@end

@implementation ArtResult

+ (instancetype)resultWith:(const mspy::Composer::Result &)result {
    ArtResult *out = [[ArtResult alloc] init];
    out.consumed = result.consumed;
    out.commitText = ArtStringFromUTF8(result.commitText);
    return out;
}

+ (instancetype)ignored {
    ArtResult *out = [[ArtResult alloc] init];
    out.consumed = NO;
    out.commitText = @"";
    return out;
}

@end

// ------------------------------------------------------------ ArtSegments --

@interface ArtSegments ()
@property (nonatomic, copy, readwrite) NSString *before;
@property (nonatomic, copy, readwrite) NSString *unconfirmed;
@property (nonatomic, copy, readwrite) NSString *highlighted;
@property (nonatomic, copy, readwrite) NSString *after;
@end

@implementation ArtSegments

- (instancetype)init {
    self = [super init];
    if (self) {
        _before = @"";
        _unconfirmed = @"";
        _highlighted = @"";
        _after = @"";
    }
    return self;
}

- (NSString *)fullText {
    return [NSString stringWithFormat:@"%@%@%@%@", _before, _unconfirmed,
                                      _highlighted, _after];
}

- (NSUInteger)caretIndex {
    return _before.length + _unconfirmed.length;
}

- (NSRange)anchorRange {
    return NSMakeRange(self.caretIndex, _highlighted.length);
}

- (NSUInteger)anchorDisplayIndex {
    NSString *full = self.fullText;
    if (full.length == 0) {
        return NSNotFound;
    }
    if (_highlighted.length > 0) {
        return self.caretIndex;
    }
    // Cursor at the right end: hang the window off the last character. Ask
    // for the start of its composed sequence so the index can never land on
    // a low surrogate.
    return [full rangeOfComposedCharacterSequenceAtIndex:full.length - 1].location;
}

@end

// -------------------------------------------------------------- ArtBridge --

@interface ArtBridge ()
- (void)loadPreferences;
- (void)savePreferences;
@end

@implementation ArtBridge {
    std::shared_ptr<McBopomofo::McBopomofoLM> _lm;
    std::shared_ptr<mspy::RelaxedToneLM> _relaxed;
    std::shared_ptr<mspy::UserPreferences> _preferences;
    std::unique_ptr<mspy::Composer> _composer;
}

+ (ArtBridge *)shared {
    static ArtBridge *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[ArtBridge alloc] init];
    });
    return instance;
}

+ (NSUInteger)candidatePageSize {
    return mspy::Composer::kCandidatePageSize;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [self loadEngine];
    }
    return self;
}

// --- engine construction (cli/repl.cpp builds the same three layers) -------

- (void)loadEngine {
    NSString *path = [[NSBundle mainBundle] pathForResource:@"mspy-data"
                                                     ofType:@"txt"];
    if (path.length == 0) {
        NSString *resources = [[NSBundle mainBundle] resourcePath];
        path = [resources stringByAppendingPathComponent:@"mspy-data.txt"];
    }
    _dataPath = path ?: @"<no resource path>";

    try {
        _lm = std::make_shared<McBopomofo::McBopomofoLM>();
        _lm->loadLanguageModel(_dataPath.fileSystemRepresentation);
        if (!_lm->isDataModelLoaded()) {
            [self failWithReason:@"the file could not be opened or is not a "
                                 @"language model"];
            return;
        }
        // Learned corrections are applied by the composer itself, as node
        // overrides on the reading grid rather than as scores, so since v0.6
        // no language model layer sits above tone relaxation. McBopomofoLM's
        // own user-phrase slot stays empty: it scores every entry at 0, which
        // is what used to let one stale pick own a reading forever
        // (spec §7, 決策記錄 2026-08-04).
        _relaxed = std::make_shared<mspy::RelaxedToneLM>(_lm);
        _preferences = std::make_shared<mspy::UserPreferences>();
        [self loadPreferences];
        _composer = std::make_unique<mspy::Composer>(_relaxed);
        _composer->setPreferences(_preferences);
    } catch (const std::exception &e) {
        [self failWithReason:[NSString stringWithUTF8String:e.what()] ?: @"?"];
        return;
    }

    // The bridge is a process-lifetime singleton and it owns the composer
    // that owns this lambda, so an unretained capture is both cycle-free
    // and safe.
    ArtBridge *__unsafe_unretained bridge = self;
    // The composer has already written the pick into the store by the time
    // this fires; all that is left is to persist it. Corrections are rare
    // (a keypress or two per sentence at worst) and must survive a crash, so
    // each one is written out immediately.
    _composer->onLearned = [bridge](const std::string &context,
                                    const std::string &reading,
                                    const std::string &value) {
        ArtLog(@"learned %s %s after %s", value.c_str(), reading.c_str(),
               context.c_str());
        [bridge savePreferences];
    };

    // An input method is normally ended with a signal rather than a clean
    // quit. Nothing is held back now that every pick is written as it
    // happens, but a flush costs nothing and covers a failed write.
    [[NSNotificationCenter defaultCenter]
        addObserverForName:NSApplicationWillTerminateNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    [bridge flushUserPreferences];
                }];

    _ready = YES;
    ArtLog(@"language model loaded: %@", _dataPath);
}

- (void)failWithReason:(NSString *)reason {
    _lm.reset();
    _relaxed.reset();
    _preferences.reset();
    _composer.reset();
    _ready = NO;
    _loadError = reason;
    // Always logged, debug flag or not: this is the one failure the user
    // has to be able to diagnose from Console.app alone.
    ArtLogAlways(@"language model NOT loaded: %@ (%@). The input method "
                 @"will pass every key through to the application.",
                 _dataPath, reason);
}

// --- user preferences ------------------------------------------------------
//
// Port of CMspyBridge::LoadPreferences / SavePreferences.  Since v0.6 the
// store records each pick together with the CONTEXT it was made in, and the
// composer applies it as a node override rather than as a score (spec §7).
// That changed the file format, so it also changed the file name.

- (void)loadPreferences {
    NSString *support = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
    if (support.length == 0) {
        return;
    }
    NSString *dir = [support stringByAppendingPathComponent:@"ArtShuangpin"];
    NSError *error = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:&error]) {
        ArtLog(@"cannot create %@: %@", dir, error);
        return;
    }
    _userChoicesPath = [dir stringByAppendingPathComponent:@"user-choices.txt"];

    // A store left by a pre-v0.4.0 build records no context, and context is
    // the whole of a v0.6 record — it cannot be invented. Move the old file
    // aside rather than delete it: it is the only copy of what the user
    // taught the previous build.
    NSString *legacy = [dir stringByAppendingPathComponent:@"user-phrases.txt"];
    NSFileManager *files = [NSFileManager defaultManager];
    if ([files fileExistsAtPath:legacy]) {
        NSString *parked = [legacy stringByAppendingPathExtension:@"bak"];
        [files removeItemAtPath:parked error:nil];
        NSError *moveError = nil;
        if ([files moveItemAtPath:legacy toPath:parked error:&moveError]) {
            ArtLog(@"parked the old preference file at %@", parked);
        } else {
            ArtLog(@"could not park %@: %@", legacy, moveError);
        }
    }

    std::ifstream in(_userChoicesPath.fileSystemRepresentation,
                     std::ios::binary);
    if (!in.is_open()) {
        return;  // nothing learned yet; the file appears on first selection
    }
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    in.close();

    _preferences->loadFromText(text);
    ArtLog(@"%lu learned entries from %@", (unsigned long)_preferences->size(),
           _userChoicesPath);
}

- (void)savePreferences {
    if (_userChoicesPath.length == 0 || !_preferences ||
        !_preferences->dirty()) {
        return;
    }
    const char *path = _userChoicesPath.fileSystemRepresentation;

    // Unlike Windows — where every application hosts its own TIP instance —
    // one macOS process serves everybody, so there is usually no sibling to
    // race with.  The merge is kept anyway: a second copy of the input
    // method, or the user editing the file by hand, costs nothing to
    // survive.
    std::ifstream in(path, std::ios::binary);
    if (in.is_open()) {
        std::string text((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        in.close();
        mspy::UserPreferences onDisk;
        onDisk.loadFromText(text);
        _preferences->mergeFrom(onDisk);
    }

    // Write a sibling file and rename over the target, so neither a crash
    // nor a concurrent reader ever sees a half-written store.
    NSString *tempPath = [_userChoicesPath stringByAppendingString:@".tmp"];
    const char *temp = tempPath.fileSystemRepresentation;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return;
        }
        out << _preferences->serialize();
        if (!out.good()) {
            return;
        }
    }
    if (std::rename(temp, path) == 0) {
        _preferences->clearDirty();
    } else {
        std::remove(temp);
        ArtLog(@"could not replace %@", _userChoicesPath);
    }
}

- (void)flushUserPreferences {
    [self savePreferences];
}

// --- composer passthrough --------------------------------------------------

- (ArtComposerState)state {
    if (!_composer) {
        return ArtComposerStateEmpty;
    }
    switch (_composer->state()) {
        case mspy::Composer::State::kComposing:
            return ArtComposerStateComposing;
        case mspy::Composer::State::kSelecting:
            return ArtComposerStateSelecting;
        case mspy::Composer::State::kEmpty:
            break;
    }
    return ArtComposerStateEmpty;
}

- (BOOL)wouldConsumeChar:(char)c {
    return _composer ? _composer->wouldConsume(c) : NO;
}

- (ArtResult *)feedChar:(char)c {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->feedChar(c);
    return [ArtResult resultWith:result];
}

- (ArtResult *)feedBackspace {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->feedBackspace();
    return [ArtResult resultWith:result];
}

- (ArtResult *)feedEnter {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->feedEnter();
    return [ArtResult resultWith:result];
}

- (ArtResult *)feedEsc {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->feedEsc();
    return [ArtResult resultWith:result];
}

- (ArtResult *)feedEnglishChar:(char)c {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->feedEnglishChar(c);
    return [ArtResult resultWith:result];
}

- (ArtResult *)switchLanguageToEnglish:(BOOL)toEnglish {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->switchLanguage(toEnglish);
    return [ArtResult resultWith:result];
}

- (ArtResult *)selectCandidateAtPageRow:(NSUInteger)row {
    if (!_composer) {
        return [ArtResult ignored];
    }
    size_t index = _composer->candidatePageIndex() *
                       mspy::Composer::kCandidatePageSize +
                   row;
    if (index >= _composer->candidates().size()) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->selectCandidate(index);
    return [ArtResult resultWith:result];
}

- (ArtResult *)closeCandidateMenu {
    if (!_composer) {
        return [ArtResult ignored];
    }
    mspy::Composer::Result result = _composer->closeCandidateMenu();
    return [ArtResult resultWith:result];
}

- (void)cancel {
    if (_composer) {
        _composer->cancel();
    }
}

- (ArtSegments *)segments {
    ArtSegments *out = [[ArtSegments alloc] init];
    if (!_composer) {
        return out;
    }
    mspy::Composer::DisplaySegments segments = _composer->displaySegments();
    out.before = ArtStringFromUTF8(segments.before);
    out.unconfirmed = ArtStringFromUTF8(segments.unconfirmed);
    out.highlighted = ArtStringFromUTF8(segments.highlighted);
    out.after = ArtStringFromUTF8(segments.after);
    return out;
}

- (NSArray<NSString *> *)currentPageCandidates {
    NSMutableArray<NSString *> *out = [NSMutableArray array];
    if (!_composer) {
        return out;
    }
    for (const auto &candidate : _composer->currentPageCandidates()) {
        [out addObject:ArtStringFromUTF8(candidate.value)];
    }
    return out;
}

- (NSUInteger)candidatePageIndex {
    return _composer ? _composer->candidatePageIndex() : 0;
}

- (NSUInteger)candidatePageCount {
    return _composer ? _composer->candidatePageCount() : 0;
}

@end
