// See ArtInputController.h.
//
// The key-routing order below is a transliteration of
// CCompositionProcessorEngine::IsVirtualKeyNeedMspy in the Windows build
// (ime/SampleIME/CompositionProcessorEngine.cpp).  Keep them in the same
// order: several of the rules only work because of what precedes them —
// wouldConsume() claims idle digits and punctuation, so the idle navigation
// keys have to be taken off the table before it is ever asked.

#import "ArtInputController.h"

#import <Carbon/Carbon.h>  // kVK_* virtual key codes

#import "ArtBridge.h"
#import "ArtModeHUD.h"
#import "ArtNavigation.h"

#pragma mark - shell state

// Shared by every controller instance.  IMK makes one controller per client,
// but the composer and the Chinese/English mode are properties of the user,
// not of a text field.
//
// Upstream v0.5 deleted the passive last-character memory this file used to
// carry (LastCharClass, the caret snapshot taken at commit time, the global
// mouse monitor that invalidated both).  The separator space is now decided
// inside the composer, where both sides of the junction are its own text —
// see -toggleChineseModeWithClient:.  Do not bring the guesswork back.
//
// The Chinese/English mode is remembered per application (spec §6
// 「中英模式各應用程式獨立記憶」). On Windows this comes for free: the TIP
// runs inside the application's own process, so the instance IS the per-app
// slot. A macOS input method is a single process serving every application,
// so the slot has to be built by hand — keyed by bundle identifier, with
// sChineseMode a cache of whichever application currently holds focus.
//
// Applications start in ENGLISH, matching the Windows build's
// InitializeSampleIMECompartment(FALSE). The memory is dropped when the
// application quits, so relaunching it starts in English again; that is what
// the Windows per-process behaviour amounts to, and spec §6 says 行程.
static BOOL sChineseMode = NO;
static NSString *sCurrentAppKey = nil;
static NSMutableDictionary<NSString *, NSNumber *> *sModeByApp = nil;

static BOOL sShiftAloneCandidate = NO;

#pragma mark - key classification

namespace {

// The numpad exemption (spec §6 「九宮格豁免」) must be decided from the key
// code.  NSEventModifierFlagNumericPad is NOT usable for this: macOS sets it
// on the arrow keys too.
bool IsKeypadKeyCode(unsigned short code) {
    switch (code) {
        case kVK_ANSI_Keypad0:
        case kVK_ANSI_Keypad1:
        case kVK_ANSI_Keypad2:
        case kVK_ANSI_Keypad3:
        case kVK_ANSI_Keypad4:
        case kVK_ANSI_Keypad5:
        case kVK_ANSI_Keypad6:
        case kVK_ANSI_Keypad7:
        case kVK_ANSI_Keypad8:
        case kVK_ANSI_Keypad9:
        case kVK_ANSI_KeypadDecimal:
        case kVK_ANSI_KeypadMultiply:
        case kVK_ANSI_KeypadPlus:
        case kVK_ANSI_KeypadMinus:
        case kVK_ANSI_KeypadDivide:
        case kVK_ANSI_KeypadEquals:
        case kVK_ANSI_KeypadClear:
            return true;
        default:
            return false;
    }
    // kVK_ANSI_KeypadEnter is deliberately absent: it is Enter.
}

// Keys that move the caret. Eaten with no effect while composing so the
// caret cannot escape the composition (spec §6); passed through when idle.
bool IsCaretMovementKeyCode(unsigned short code) {
    switch (code) {
        case kVK_LeftArrow:
        case kVK_RightArrow:
        case kVK_UpArrow:
        case kVK_DownArrow:
        case kVK_PageUp:
        case kVK_PageDown:
        case kVK_Home:
        case kVK_End:
            return true;
        default:
            return false;
    }
}

}  // namespace

#pragma mark - controller

@implementation ArtInputController

+ (void)initialize {
    if (self != [ArtInputController class]) {
        return;
    }
    sModeByApp = [NSMutableDictionary dictionary];

    // A quit application forgets its mode: the next launch is a new process
    // and starts in English, which is what the Windows build does by virtue
    // of the TIP dying with the process.
    [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceDidTerminateApplicationNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    NSRunningApplication *app =
                        note.userInfo[NSWorkspaceApplicationKey];
                    NSString *key = app.bundleIdentifier;
                    if (key.length > 0) {
                        [sModeByApp removeObjectForKey:key];
                    }
                }];
}

- (NSUInteger)recognizedEvents:(id)sender {
    // Flags-changed is not optional: the Chinese/English toggle is a bare
    // Shift tap, which produces no key event at all.
    return NSEventMaskKeyDown | NSEventMaskFlagsChanged;
}

#pragma mark session lifecycle

- (void)activateServer:(id)sender {
    [[ArtCandidateWindow shared] hide];

    id<IMKTextInput> client = (id<IMKTextInput>)sender;

    // Taking focus restores the mode this application was left in (spec §6).
    // Silently: the HUD is for switches the user made, and flashing it on
    // every application change would be noise.
    sCurrentAppKey = [ArtInputController appKeyForClient:client];
    NSNumber *remembered = sModeByApp[sCurrentAppKey];
    sChineseMode = remembered != nil ? remembered.boolValue : NO;

    // Which marked-text attributes the host admits to honouring decides how
    // much of the anchor emphasis it can possibly draw. One line in the log
    // as focus lands answers that for good, per application.
    ArtLog(@"activate %@ [%@] — marked text attributes: %@", sCurrentAppKey,
           sChineseMode ? @"中" : @"英", [client validAttributesForMarkedText]);
}

// The per-app memory's key. Bundle identifier is the only application
// identity an IMKTextInput exposes; hosts that report none share one slot,
// which is no worse than having no memory at all.
+ (NSString *)appKeyForClient:(id<IMKTextInput>)client {
    NSString *bundle = [client bundleIdentifier];
    return bundle.length > 0 ? bundle : @"<unidentified>";
}

- (void)deactivateServer:(id)sender {
    // Losing focus is one of the few things that still commits (spec §6):
    // since v0.4 a composition can run indefinitely, so this is all that
    // stands between a long buffer and its disappearance.
    [self commitCompositionInto:(id<IMKTextInput>)sender];
    [[ArtCandidateWindow shared] hide];
}

- (void)commitComposition:(id)sender {
    [self commitCompositionInto:(id<IMKTextInput>)sender];
}

- (void)commitCompositionInto:(id<IMKTextInput>)client {
    ArtBridge *bridge = [ArtBridge shared];
    if (bridge.state == ArtComposerStateEmpty) {
        [[ArtCandidateWindow shared] hide];
        return;
    }
    // Matches the Windows OnCompositionTerminated behaviour: the text is
    // kept, not thrown away.
    ArtResult *result = [bridge feedEnter];
    [self syncWithCommit:result.commitText client:client];
}

#pragma mark event entry point

- (BOOL)handleEvent:(NSEvent *)event client:(id)sender {
    id<IMKTextInput> client = (id<IMKTextInput>)(sender ?: [self client]);

    // The first question about any key that "does nothing" is whether it
    // reached the input method at all: a host that handles a key itself
    // leaves no line here, and no amount of reading the routing below will
    // show that. Only -characters is unsafe on a non-key event, so keyCode
    // is logged for every type.
    ArtLog(@"event type=%lu keyCode=%hu", (unsigned long)event.type,
           event.keyCode);

    if (event.type == NSEventTypeFlagsChanged) {
        [self handleFlagsChanged:event client:client];
        return NO;  // modifier events are never eaten
    }
    if (event.type != NSEventTypeKeyDown) {
        return NO;
    }
    return [self handleKeyDown:event client:client];
}

// Bare Shift tap = Chinese/English toggle (spec §6 「中英切換」). "Bare"
// means: Shift went down with no other modifier, and nothing was typed
// before it came back up. Shift+letter therefore still types an uppercase
// letter without toggling.
- (void)handleFlagsChanged:(NSEvent *)event client:(id<IMKTextInput>)client {
    NSEventModifierFlags flags =
        event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    BOOL shift = (flags & NSEventModifierFlagShift) != 0;
    // Caps Lock is deliberately not in this mask: its flag stays set for as
    // long as the lock is on, so including it would silently disable the
    // toggle for anyone typing with Caps Lock engaged.
    BOOL others = (flags & (NSEventModifierFlagControl |
                            NSEventModifierFlagOption |
                            NSEventModifierFlagCommand)) != 0;

    if (shift && !others) {
        sShiftAloneCandidate = YES;
        return;
    }
    if (!shift && sShiftAloneCandidate) {
        sShiftAloneCandidate = NO;
        [self toggleChineseModeWithClient:client];
        return;
    }
    sShiftAloneCandidate = NO;
}

- (BOOL)handleKeyDown:(NSEvent *)event client:(id<IMKTextInput>)client {
    // Anything typed disqualifies the pending Shift from being a bare tap.
    sShiftAloneCandidate = NO;

    ArtBridge *bridge = [ArtBridge shared];
    if (!bridge.ready) {
        // Fail open. A dictionary that would not load must never cost the
        // user the ability to type; see -menu for how they find out why.
        return NO;
    }

    NSEventModifierFlags flags =
        event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    const BOOL shift = (flags & NSEventModifierFlagShift) != 0;
    const BOOL hasCommandLike = (flags & (NSEventModifierFlagCommand |
                                          NSEventModifierFlagControl |
                                          NSEventModifierFlagOption)) != 0;
    const unsigned short keyCode = event.keyCode;
    const BOOL active = bridge.state != ArtComposerStateEmpty;

    ArtLog(@"keyDown keyCode=%hu chars=%@ mode=%@ active=%d shift=%d cmd=%d",
           keyCode, event.charactersIgnoringModifiers,
           sChineseMode ? @"chinese" : @"english", (int)active, (int)shift,
           (int)hasCommandLike);

    // Shortcuts belong to the application.
    if (hasCommandLike) {
        return NO;
    }

    // English mode with nothing composing = every key passes through,
    // exactly like the Windows build's "keyboard closed" compartment state.
    // A LIVE composition is the exception since upstream v0.5: it survives
    // the language switch, so English has to be typed into it rather than
    // past it (spec §6 中英切換 v5).
    if (!sChineseMode) {
        if (!active) {
            return NO;
        }
        return [self handleEnglishKeyDown:event client:client shift:shift];
    }

    // Numpad exemption (spec §6): idle it types literally; while composing
    // it commits the buffer first and then types.
    if (IsKeypadKeyCode(keyCode)) {
        if (active) {
            ArtResult *result = [bridge feedEnter];
            [self syncWithCommit:result.commitText client:client];
        }
        return NO;
    }

    if (IsCaretMovementKeyCode(keyCode)) {
        return active ? YES : NO;  // eaten with no effect while composing
    }

    switch (keyCode) {
        case kVK_Delete:  // Backspace
            if (!active) {
                return NO;
            }
            [self syncWithResult:[bridge feedBackspace] client:client];
            return YES;
        case kVK_Tab:
            // Tab is a second Backspace (spec §6, upstream v0.5), so
            // deleting never takes a hand off the main block. Shift+Tab is
            // left alone — reverse focus navigation is the safety valve —
            // and while idle it is replayed as a real Backspace, exactly the
            // way 9/0 are replayed as arrows.
            if (shift) {
                ArtLog(@"tab: shift held, passed to the application");
                return NO;
            }
            if (!active) {
                BOOL injected = [ArtNavigation inject:ArtNavigationKeyBackspace
                                            shiftHeld:NO];
                ArtLog(@"tab: idle, backspace injected=%d", (int)injected);
                return YES;
            }
            ArtLog(@"tab: composing, feedBackspace");
            [self syncWithResult:[bridge feedBackspace] client:client];
            return YES;
        case kVK_Return:
        case kVK_ANSI_KeypadEnter:
            if (!active) {
                return NO;
            }
            [self syncWithResult:[bridge feedEnter] client:client];
            return YES;
        case kVK_Escape:
            if (!active) {
                return NO;
            }
            [self syncWithResult:[bridge feedEsc] client:client];
            return YES;
        default:
            break;
    }

    // Idle navigation keys (spec §6 「閒置導航鍵」). This has to come before
    // wouldConsume(), which claims every idle digit and punctuation mark.
    //
    // The key identity is taken from charactersIgnoringModifiers so that
    // Shift+9 still reads as '9' here — that is what makes "do not
    // intercept 9/0 while Shift is held" expressible, leaving （）typable.
    if (!active) {
        NSString *bare = event.charactersIgnoringModifiers;
        unichar key = bare.length > 0 ? [bare characterAtIndex:0] : 0;
        ArtNavigationKey navigation = ArtNavigationKeyLeft;
        BOOL isNavigation = NO;
        if (key == '-') {
            navigation = ArtNavigationKeyLineStart;
            isNavigation = YES;  // intercepted regardless of Shift
        } else if (key == '=') {
            navigation = ArtNavigationKeyLineEnd;
            isNavigation = YES;
        } else if (!shift && key == '9') {
            navigation = ArtNavigationKeyLeft;
            isNavigation = YES;
        } else if (!shift && key == '0') {
            navigation = ArtNavigationKeyRight;
            isNavigation = YES;
        }
        if (isNavigation) {
            [ArtNavigation inject:navigation shiftHeld:shift];
            return YES;
        }
    }

    // Everything else: ask the composer, then feed it. Note `characters`
    // rather than charactersIgnoringModifiers — Shift+9 must arrive as '('
    // so the composer can turn it into （.
    NSString *characters = event.characters;
    if (characters.length == 0) {
        return NO;
    }
    unichar ch = [characters characterAtIndex:0];
    if (ch >= 'A' && ch <= 'Z') {
        ch = ch - 'A' + 'a';  // the composer's key table is lowercase
    }
    if (ch == 0 || ch >= 0x80) {
        return NO;
    }
    if (![bridge wouldConsumeChar:(char)ch]) {
        return NO;
    }
    [self syncWithResult:[bridge feedChar:(char)ch] client:client];
    return YES;
}

// English mode over a live composition — the transliteration of
// CCompositionProcessorEngine::IsVirtualKeyNeedMspyEnglish in the Windows
// build. The editing keys act on the buffer, printable ASCII joins it with
// its case intact, and everything else is the application's.
- (BOOL)handleEnglishKeyDown:(NSEvent *)event
                      client:(id<IMKTextInput>)client
                       shift:(BOOL)shift {
    ArtBridge *bridge = [ArtBridge shared];

    if (IsCaretMovementKeyCode(event.keyCode)) {
        return YES;  // eaten: the caret must not wander out of the buffer
    }
    switch (event.keyCode) {
        case kVK_Delete:  // Backspace
            [self syncWithResult:[bridge feedBackspace] client:client];
            return YES;
        case kVK_Tab:
            if (shift) {
                return NO;
            }
            [self syncWithResult:[bridge feedBackspace] client:client];
            return YES;
        case kVK_Return:
        case kVK_ANSI_KeypadEnter:
            [self syncWithResult:[bridge feedEnter] client:client];
            return YES;
        case kVK_Escape:
            [self syncWithResult:[bridge feedEsc] client:client];
            return YES;
        default:
            break;
    }

    // Printable ASCII only; function keys and anything a host makes of a dead
    // key belong to the application. Space is deliberately included — it is a
    // literal space here, which is what makes a multi-word English run
    // possible inside the composition (spec §6). Enter is what commits.
    NSString *characters = event.characters;
    if (characters.length != 1) {
        return NO;
    }
    unichar ch = [characters characterAtIndex:0];
    if (ch < 0x20 || ch >= 0x7F) {
        return NO;
    }
    [self syncWithResult:[bridge feedEnglishChar:(char)ch] client:client];
    return YES;
}

#pragma mark Chinese/English

// Every switch the user makes updates this application's memory. The Windows
// build deliberately excludes its language-bar toggle, because it cannot tell
// that click apart from the system's own writes to the shared open/close
// compartment. Here the menu is ours and there is no such ambiguity, so a
// menu switch is remembered like a Shift tap.
+ (void)setChineseMode:(BOOL)chinese {
    sChineseMode = chinese;
    if (sCurrentAppKey.length > 0) {
        sModeByApp[sCurrentAppKey] = @(chinese);
    }
}

- (void)toggleChineseModeWithClient:(id<IMKTextInput>)client {
    ArtBridge *bridge = [ArtBridge shared];
    if (!bridge.ready) {
        return;
    }
    if (client == nil) {
        // Reachable from the menu with no focused text field: flip the mode
        // and skip everything that would need somewhere to write to.
        [ArtInputController setChineseMode:!sChineseMode];
        [[ArtModeHUD shared] flashChinese:sChineseMode nearRect:NSZeroRect];
        return;
    }

    // The switch NEVER commits (spec §6 中英切換 v5, upstream v0.5). A live
    // composition keeps its underline across the boundary; English typed
    // after this joins the same buffer (see -handleEnglishKeyDown:...), and
    // the composer itself inserts the half-width separator space where the
    // two scripts meet — both sides of that junction are its own text, so
    // the decision needs no guesswork about the host document. That is what
    // retired this file's passive last-character memory; with no composition
    // there is nothing to separate and nothing is inserted.
    ArtResult *result = [bridge switchLanguageToEnglish:sChineseMode];
    [self syncWithResult:result client:client];

    [ArtInputController setChineseMode:!sChineseMode];
    ArtLog(@"mode -> %@ [%@]", sChineseMode ? @"中" : @"英", sCurrentAppKey);

    // Sampled after the switch, which is safe precisely because nothing was
    // committed: the marked text is still up, so the host still reports
    // where it is. An empty rect only happens with no composition, and the
    // HUD then falls back to the middle of the screen.
    [[ArtModeHUD shared] flashChinese:sChineseMode
                             nearRect:[self caretRectForClient:client]];
}

#pragma mark rendering

- (void)syncWithResult:(ArtResult *)result client:(id<IMKTextInput>)client {
    [self syncWithCommit:result.commitText client:client];
}

// The single place where composer state becomes something the user can see.
- (void)syncWithCommit:(NSString *)commit client:(id<IMKTextInput>)client {
    ArtBridge *bridge = [ArtBridge shared];

    if (commit.length > 0) {
        [client insertText:commit
          replacementRange:NSMakeRange(NSNotFound, 0)];
    }

    ArtSegments *segments = [bridge segments];
    NSString *full = segments.fullText;
    if (bridge.state == ArtComposerStateEmpty || full.length == 0) {
        [client setMarkedText:@""
               selectionRange:NSMakeRange(0, 0)
             replacementRange:NSMakeRange(NSNotFound, 0)];
        [[ArtCandidateWindow shared] hide];
        return;
    }

    NSMutableAttributedString *marked =
        [[NSMutableAttributedString alloc] initWithString:full];
    NSRange whole = NSMakeRange(0, full.length);
    NSRange anchor = segments.anchorRange;
    BOOL hasAnchor = anchor.length > 0 && NSMaxRange(anchor) <= full.length;

    // Single-colour display (spec §6 「單色顯示」): the host's own text
    // colour, underlined until commit. The black/blue split still exists
    // inside the composer's segments — it is what the cursor and the anchor
    // are computed from — but it is not drawn.
    //
    // The underline is SOLID, not the dotted one the Windows build uses.
    // Chromium-based hosts (Chrome, Electron, LINE) reduce this attribute to
    // `[style intValue] > 1`, and NSUnderlinePatternDot is 0x0100 — a dotted
    // single underline is 257, so every character would read as "thick" and
    // the anchor below would be indistinguishable from the rest.
    [marked addAttribute:NSUnderlineStyleAttributeName
                   value:@(NSUnderlineStyleSingle)
                   range:whole];

    // The anchor — the one character to the right of the cursor (spec §6
    // 「游標錨點反白」) — is marked with a THICK underline. On macOS that is
    // the native idiom for "the segment being worked on" (it is how Japanese
    // input marks the active clause), and it is the one channel that
    // survives every host: NSTextView draws a genuinely thicker line, and
    // Chromium maps it to its thick composition span. The background colour
    // is sent as well for hosts that render attributed marked text properly,
    // and is dropped without complaint by the ones that do not.
    if (hasAnchor) {
        [marked addAttribute:NSUnderlineStyleAttributeName
                       value:@(NSUnderlineStyleThick)
                       range:anchor];
        [marked addAttribute:NSUnderlineColorAttributeName
                       value:[NSColor controlAccentColor]
                       range:anchor];
        [marked addAttribute:NSBackgroundColorAttributeName
                       value:[NSColor selectedTextBackgroundColor]
                       range:anchor];
    }
    [self markClauseSegmentsIn:marked segments:segments];

    // The caret travels separately, as a ZERO-LENGTH selection range at the
    // composer's cursor.
    //
    // It used to carry the anchor's whole range, on the theory that a host
    // would highlight that range and put the caret at its start. Measured on
    // the Mac: the host did neither, and the caret stayed pinned to the end
    // of the composition while the cursor was in fact in the middle. The two
    // signals now travel on independent channels — position here, anchor in
    // the attributes above — so a host that honours only one of them still
    // shows something true.
    [client setMarkedText:marked
           selectionRange:NSMakeRange(segments.caretIndex, 0)
         replacementRange:NSMakeRange(NSNotFound, 0)];

    if (bridge.state == ArtComposerStateSelecting) {
        ArtCandidateWindow *window = [ArtCandidateWindow shared];
        window.delegate = self;
        [window showCandidates:[bridge currentPageCandidates]
                     pageIndex:[bridge candidatePageIndex]
                     pageCount:[bridge candidatePageCount]
                    anchorRect:[self anchorRectForClient:client
                                                segments:segments]];
    } else {
        [[ArtCandidateWindow shared] hide];
    }
}

// Clause segmentation: before-the-cursor / the anchor / after. Hosts use it
// to tell where one segment ends and the next begins, which is what lets the
// thick underline above read as "this one is active" rather than as a stray
// change of line weight.
- (void)markClauseSegmentsIn:(NSMutableAttributedString *)marked
                    segments:(ArtSegments *)segments {
    NSUInteger length = marked.length;
    NSUInteger caret = segments.caretIndex;
    NSRange anchor = segments.anchorRange;
    NSInteger clause = 0;

    if (caret > 0 && caret <= length) {
        [marked addAttribute:NSMarkedClauseSegmentAttributeName
                       value:@(clause++)
                       range:NSMakeRange(0, caret)];
    }
    if (anchor.length > 0 && NSMaxRange(anchor) <= length) {
        [marked addAttribute:NSMarkedClauseSegmentAttributeName
                       value:@(clause++)
                       range:anchor];
    }
    NSUInteger tail = NSMaxRange(anchor);
    if (tail < length) {
        [marked addAttribute:NSMarkedClauseSegmentAttributeName
                       value:@(clause++)
                       range:NSMakeRange(tail, length - tail)];
    }
}

// Screen rectangle of the anchor character, which is where the candidate
// window has to open (spec §6 「候選窗開在游標錨點字的正下方」).
- (NSRect)anchorRectForClient:(id<IMKTextInput>)client
                     segments:(ArtSegments *)segments {
    NSUInteger index = segments.anchorDisplayIndex;
    if (index == NSNotFound) {
        index = 0;
    }
    NSRect rect = NSZeroRect;
    [client attributesForCharacterIndex:index lineHeightRectangle:&rect];
    if (NSIsEmptyRect(rect) && index != 0) {
        // Hosts that only report the start of the marked text.
        [client attributesForCharacterIndex:0 lineHeightRectangle:&rect];
    }
    return rect;
}

- (NSRect)caretRectForClient:(id<IMKTextInput>)client {
    NSRect rect = NSZeroRect;
    [client attributesForCharacterIndex:0 lineHeightRectangle:&rect];
    return rect;
}

#pragma mark candidate window delegate

- (void)candidateWindowDidSelectRow:(NSUInteger)row {
    id<IMKTextInput> client = (id<IMKTextInput>)[self client];
    if (client == nil) {
        return;
    }
    [self syncWithResult:[[ArtBridge shared] selectCandidateAtPageRow:row]
                  client:client];
}

#pragma mark menu

- (NSMenu *)menu {
    ArtBridge *bridge = [ArtBridge shared];
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@"ArtShuangpin"];
    menu.autoenablesItems = NO;

    if (!bridge.ready) {
        NSString *title =
            [NSString stringWithFormat:@"⚠️ 詞庫未載入：%@", bridge.dataPath];
        NSMenuItem *problem = [[NSMenuItem alloc] initWithTitle:title
                                                         action:NULL
                                                  keyEquivalent:@""];
        problem.enabled = NO;
        [menu addItem:problem];
        [menu addItem:[NSMenuItem separatorItem]];
    }

    NSMenuItem *chinese =
        [[NSMenuItem alloc] initWithTitle:@"中文輸入"
                                   action:@selector(chooseChineseMode:)
                            keyEquivalent:@""];
    chinese.target = self;
    chinese.state = sChineseMode ? NSControlStateValueOn
                                 : NSControlStateValueOff;
    [menu addItem:chinese];

    NSMenuItem *english =
        [[NSMenuItem alloc] initWithTitle:@"英文輸入"
                                   action:@selector(chooseEnglishMode:)
                            keyEquivalent:@""];
    english.target = self;
    english.state = sChineseMode ? NSControlStateValueOff
                                 : NSControlStateValueOn;
    [menu addItem:english];

    [menu addItem:[NSMenuItem separatorItem]];

    BOOL trusted = [ArtNavigation isTrusted];
    NSMenuItem *accessibility = [[NSMenuItem alloc]
        initWithTitle:(trusted ? @"輔助使用權限：已授權（閒置導航可用）"
                               : @"輔助使用權限：未授權（閒置導航停用）…")
               action:@selector(openAccessibilitySettings:)
        keyEquivalent:@""];
    accessibility.target = self;
    [menu addItem:accessibility];

    if (bridge.userChoicesPath.length > 0) {
        NSMenuItem *phrases =
            [[NSMenuItem alloc] initWithTitle:@"顯示使用者詞庫檔…"
                                       action:@selector(revealUserPhrases:)
                                keyEquivalent:@""];
        phrases.target = self;
        [menu addItem:phrases];
    }

    [menu addItem:[NSMenuItem separatorItem]];
    NSString *version = [[NSBundle mainBundle]
        objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
    NSMenuItem *about = [[NSMenuItem alloc]
        initWithTitle:[NSString stringWithFormat:@"阿特雙拼輸入法 %@",
                                                 version ?: @""]
               action:NULL
        keyEquivalent:@""];
    about.enabled = NO;
    [menu addItem:about];

    return menu;
}

- (void)chooseChineseMode:(id)sender {
    if (!sChineseMode) {
        [self toggleChineseModeWithClient:(id<IMKTextInput>)[self client]];
    }
}

- (void)chooseEnglishMode:(id)sender {
    if (sChineseMode) {
        [self toggleChineseModeWithClient:(id<IMKTextInput>)[self client]];
    }
}

- (void)openAccessibilitySettings:(id)sender {
    [ArtNavigation promptForTrustIfNeeded];
    [ArtNavigation openAccessibilitySettings];
}

- (void)revealUserPhrases:(id)sender {
    NSString *path = [ArtBridge shared].userChoicesPath;
    if (path.length == 0) {
        return;
    }
    [[NSWorkspace sharedWorkspace] selectFile:path
                     inFileViewerRootedAtPath:@""];
}

@end
