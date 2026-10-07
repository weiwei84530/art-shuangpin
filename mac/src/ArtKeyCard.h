// The keyboard reminder card (2026-10-07).
//
// `|` (Shift+\) in Chinese mode puts up a picture of the keyboard with every
// key's initial and finals on it -- the tutorial site's keycap annotations
// (web/tutorials.js) -- for the moment you have forgotten where ㄩㄥ lives.
// Any other key takes it down again and then does its own job; `|` itself
// toggles. Chinese mode already ate `|` without output, so claiming it costs
// nothing; English mode still types it.
//
// Same card, same trigger, same layout as ime/SampleIME/KeyCard.cpp — keep
// the two alike. Unlike the 中/英 bubble it does not need a caret: it is
// centred on the screen the caret (or, failing that, the pointer) is on.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface ArtKeyCard : NSObject

@property (class, nonatomic, readonly) ArtKeyCard *shared;
@property (nonatomic, readonly, getter=isVisible) BOOL visible;

/// `caretRect` only picks the screen; an empty rect means "the pointer's".
- (void)showNearRect:(NSRect)caretRect;
- (void)hide;

@end

NS_ASSUME_NONNULL_END
