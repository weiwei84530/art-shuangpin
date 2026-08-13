// Idle navigation keys (spec §6 「閒置導航鍵」).
//
// With no composition in progress the shell eats 9/0/-/= and Tab and replays
// them as the keystroke they stand for.  This is the macOS counterpart of
// InjectNavigationKey() in ime/SampleIME/KeyEventSink.cpp:55 — SendInput
// there, CGEventPost here.  The injected key lands back in our own
// controller, which is harmless: arrows are always passed through while
// idle, so there is nothing to guard against.
//
// Note the deliberate difference from the Windows build: `-`/`=` post
// Cmd+Left / Cmd+Right, not Home / End.  On macOS, Home and End mean
// "document start/end" (and in many apps only scroll), while the line-start
// and line-end movements are Cmd+arrow.  The spec calls for 行首/行尾, so
// the meaning is ported rather than the key code.  A physically held Shift
// is carried onto the injected event, which turns the movement into a
// selection exactly as it does on Windows.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ArtNavigationKey) {
    ArtNavigationKeyLeft,       // '9'
    ArtNavigationKeyRight,      // '0'
    ArtNavigationKeyLineStart,  // '-'
    ArtNavigationKeyLineEnd,    // '='
    ArtNavigationKeyBackspace,  // Tab (spec §6, upstream v0.5)
};

@interface ArtNavigation : NSObject

/// Posts the keystroke.  Returns NO when the process is not trusted for
/// Accessibility, in which case nothing was posted and the user has been
/// prompted (at most once per process).
+ (BOOL)inject:(ArtNavigationKey)key shiftHeld:(BOOL)shiftHeld;

/// Non-prompting check, for the menu item's state.
+ (BOOL)isTrusted;

/// Shows the system "grant Accessibility access" prompt, at most once per
/// process.  There is no way around this permission: an input method cannot
/// otherwise move another application's insertion point.
+ (void)promptForTrustIfNeeded;

/// Opens System Settings at Privacy & Security → Accessibility.
+ (void)openAccessibilitySettings;

@end

NS_ASSUME_NONNULL_END
