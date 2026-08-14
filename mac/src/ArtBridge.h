// The seam between the Cocoa shell and mspy::Composer.
//
// This is the macOS counterpart of ime/SampleIME/MspyBridge.h in the
// art-shuangpin tree: it owns the engine stack
//
//     McBopomofoLM -> RelaxedToneLM -> mspy::Composer
//
// and republishes it as UTF-16/Cocoa types.  What the user has taught the
// IME is NOT a layer of that stack: since v0.6 the composer applies it
// itself, as overrides on the reading grid (spec §7).  Every piece of C++ in this
// project lives behind this header, so the rest of src/ is plain ObjC.
//
// Nothing here decides anything about input behaviour.  If you find
// yourself wanting to add a rule to this file, the rule belongs upstream in
// art-shuangpin's core/ — see CLAUDE.md.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ArtComposerState) {
    ArtComposerStateEmpty = 0,
    ArtComposerStateComposing,
    ArtComposerStateSelecting,
};

/// Mirror of mspy::Composer::DisplaySegments.
///
/// The caret sits between `unconfirmed` and `highlighted`, and
/// `highlighted` IS the selection anchor: the single character to the RIGHT
/// of the cursor (spec §6 「游標錨點反白」).  It is empty when the cursor is
/// at the right end.
@interface ArtSegments : NSObject
@property (nonatomic, copy, readonly) NSString *before;
@property (nonatomic, copy, readonly) NSString *unconfirmed;
@property (nonatomic, copy, readonly) NSString *highlighted;
@property (nonatomic, copy, readonly) NSString *after;

/// before + unconfirmed + highlighted + after.
@property (nonatomic, copy, readonly) NSString *fullText;
/// UTF-16 offset of the caret within fullText.
@property (nonatomic, readonly) NSUInteger caretIndex;
/// Range of `highlighted` within fullText; length 0 at the right end.
@property (nonatomic, readonly) NSRange anchorRange;
/// Index to hang the candidate window off: the anchor character, or the
/// last character when there is no anchor.  NSNotFound if fullText is empty.
@property (nonatomic, readonly) NSUInteger anchorDisplayIndex;
@end

/// Mirror of mspy::Composer::Result.
@interface ArtResult : NSObject
@property (nonatomic, readonly) BOOL consumed;
@property (nonatomic, copy, readonly) NSString *commitText;
@end

@interface ArtBridge : NSObject

/// One composer for the whole process.  IMK makes one controller per client,
/// but only one client has focus at a time and -deactivateServer: commits
/// and resets, so the shared composer is never split between two apps.  It
/// also keeps learning global, which is what the Windows build does.
@property (class, nonatomic, readonly) ArtBridge *shared;

/// NO when the language model could not be loaded.  The shell then passes
/// every key straight through — a broken dictionary must never cost the
/// user the ability to type.
@property (nonatomic, readonly, getter=isReady) BOOL ready;
/// The path we tried, whether or not it worked.
@property (nonatomic, copy, readonly) NSString *dataPath;
/// Human-readable reason, nil when ready.  Shown in the IMK menu.
@property (nonatomic, copy, readonly, nullable) NSString *loadError;
/// The store of learned corrections (spec §7): lines of
/// `值 讀音鍵 上下文 次數 序號`.  A `user-phrases.txt` left by an older build
/// records no context and so cannot be converted; it is renamed aside
/// rather than read.
@property (nonatomic, copy, readonly, nullable) NSString *userChoicesPath;

/// Writes the preference store now.  Called when the process is going away;
/// a no-op when nothing is pending.
- (void)flushUserPreferences;

/// mspy::Composer::kCandidatePageSize.
@property (class, nonatomic, readonly) NSUInteger candidatePageSize;

@property (nonatomic, readonly) ArtComposerState state;

/// Side-effect-free eat/pass decision, exactly as the TSF shell uses it in
/// OnTestKeyDown.  `c` must be ASCII.
- (BOOL)wouldConsumeChar:(char)c;

- (ArtResult *)feedChar:(char)c;
- (ArtResult *)feedBackspace;
- (ArtResult *)feedEnter;
- (ArtResult *)feedEsc;

/// A character typed while the shell is in ENGLISH mode.  It settles the
/// Chinese in progress and joins the SAME uncommitted buffer as literal
/// text, which is what keeps one underline running across both scripts
/// (spec §6 中英切換 v5).  `c` keeps its case.  Returns
/// consumed=NO when nothing is composing — the shell then lets the key
/// through, so ordinary English typing is untouched.
- (ArtResult *)feedEnglishChar:(char)c;

/// The bare-Shift language switch.  Settles what is in progress and drops in
/// the half-width separator space where the two scripts meet.  It NEVER
/// commits: both sides of that junction are the composer's own text, so the
/// decision needs no guesswork about the host document.  Returns consumed=NO
/// when there is no composition, which is the shell's cue that the tap is
/// nothing but a mode flip.
- (ArtResult *)switchLanguageToEnglish:(BOOL)toEnglish;

/// Mouse selection in the candidate panel; `row` is 0-based within the
/// current page.  Keyboard selection does NOT come through here — digits
/// 1-6 are ordinary characters that the composer interprets itself.
- (ArtResult *)selectCandidateAtPageRow:(NSUInteger)row;
/// Window-only teardown (mouse dismissal); leaves the composition alone.
- (ArtResult *)closeCandidateMenu;
/// Unconditional reset, for app-terminated compositions.
- (void)cancel;

- (ArtSegments *)segments;
- (NSArray<NSString *> *)currentPageCandidates;
- (NSUInteger)candidatePageIndex;
- (NSUInteger)candidatePageCount;

@end

/// Logging wrapper gated on
///     defaults write com.mspy.inputmethod.ArtShuangpin debug -bool YES
/// The equivalent of MSPY_DEBUG_LOG in the Windows build.
extern void ArtLog(NSString *format, ...) NS_FORMAT_FUNCTION(1, 2);

/// The same, but always emitted: for the handful of failures a user has to
/// be able to diagnose from Console.app with no debug flag set.
extern void ArtLogAlways(NSString *format, ...) NS_FORMAT_FUNCTION(1, 2);

NS_ASSUME_NONNULL_END
