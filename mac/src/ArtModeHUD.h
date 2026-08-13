// The Chinese/English indicator (spec §6 「中英切換」).
//
// Windows gets this for free: English mode there is the system's own
// keyboard-open state, so the OS toolbar shows 中/英 by itself.  macOS has
// no equivalent an input method can borrow without declaring a second input
// mode in Info.plist — and a wrong tsInputModeListKey makes the input method
// disappear from the input source list entirely, which is a much worse
// failure than a missing indicator.  So the first version shows the state
// where the user is already looking: a small card that flashes next to the
// caret at the moment of the switch, plus a checkmark in the IMK menu.
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
/// fades out.  An empty rect falls back to the middle of the main screen.
- (void)flashChinese:(BOOL)chinese nearRect:(NSRect)caretRect;

@end

NS_ASSUME_NONNULL_END
