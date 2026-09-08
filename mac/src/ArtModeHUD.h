// The Chinese/English indicator (spec §6 「中英切換」).
//
// macOS shows nothing of its own when an input method changes its internal
// mode: there is no indicator to borrow without declaring a second input
// mode in Info.plist, and a wrong tsInputModeListKey makes the input method
// disappear from the input source list entirely, which is a much worse
// failure than a missing indicator.  So we draw it, where the user is
// already looking: a small card next to the caret, plus a checkmark in the
// IMK menu.  Measured 2026-09-08, Windows turns out to show nothing either,
// so ime/SampleIME/ModeIndicator.cpp is the same card with the same two
// triggers — keep the two looking and behaving alike.
//
// Two moments, both of them a mode the user did not read anywhere:
//
//   * the bare Shift tap — the switch they just made;
//   * -activateServer: — the switch the per-application memory made for
//     them, which nothing else announces.
//
// Swapping this for a real menu-bar icon (two input modes plus
// -[IMKTextInput selectInputMode:]) is the documented v2 upgrade; see
// docs/NOTES.md.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface ArtModeHUD : NSObject

@property (class, nonatomic, readonly) ArtModeHUD *shared;

/// Flashes 中 or 英 near `caretRect` (screen coordinates, as returned by
/// -[IMKTextInput attributesForCharacterIndex:lineHeightRectangle:]) and
/// fades out.  An empty rect falls back to the pointer.
- (void)flashChinese:(BOOL)chinese nearRect:(NSRect)caretRect;

@end

NS_ASSUME_NONNULL_END
