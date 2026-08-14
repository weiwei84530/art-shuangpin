// Idle editing layer (spec §6 「閒置編輯層」).
//
// With no composition in progress the shell eats the unshifted top digit row
// and replays each key as the keystroke it stands for.  This is the macOS
// counterpart of InjectNavigationKey() in ime/SampleIME/KeyEventSink.cpp —
// SendInput there, CGEventPost here.  The injected key lands back in our own
// controller, which is harmless: arrows and deletes are always passed
// through while idle, so there is nothing to guard against.
//
// Note the deliberate difference from the Windows build: line start/end post
// Cmd+Left / Cmd+Right, not Home / End.  On macOS, Home and End mean
// "document start/end" (and in many apps only scroll), while the line-start
// and line-end movements are Cmd+arrow.  The spec calls for 行首/行尾, so
// the meaning is ported rather than the key code.
//
// SelectToLineStart/End carry Shift themselves; the rest take it from
// `shiftHeld`, which is always NO now that the layer only claims unshifted
// digits, but is kept because the parameter costs nothing and the Windows
// side has the same shape.

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, ArtNavigationKey) {
    ArtNavigationKeyLineStart,          // '1'
    ArtNavigationKeySelectToLineStart,  // '2'
    ArtNavigationKeySelectToLineEnd,    // '3'
    ArtNavigationKeyLineEnd,            // '4'
    ArtNavigationKeyForwardDelete,    // '5'
    ArtNavigationKeyBackspace,        // '6'
    ArtNavigationKeyUp,               // '7'
    ArtNavigationKeyDown,             // '8'
    ArtNavigationKeyLeft,             // '9'
    ArtNavigationKeyRight,            // '0'
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
