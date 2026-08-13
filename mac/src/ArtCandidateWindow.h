// The candidate panel (spec §6 「候選窗 UI」).
//
// Rolled by hand rather than using IMKCandidates: IMK's own panel cannot do
// the 1-6 digit labels, the page indicator, or — the part that actually
// matters — open under one specific character of the composition rather than
// at its start.
//
// This class draws and positions.  It decides nothing: the candidate list,
// the page and the selection all come from mspy::Composer.

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@protocol ArtCandidateWindowDelegate <NSObject>
/// A candidate was clicked. `row` is 0-based within the current page.
- (void)candidateWindowDidSelectRow:(NSUInteger)row;
@end

@interface ArtCandidateWindow : NSObject

@property (class, nonatomic, readonly) ArtCandidateWindow *shared;
@property (nonatomic, weak, nullable) id<ArtCandidateWindowDelegate> delegate;
@property (nonatomic, readonly, getter=isVisible) BOOL visible;

/// `anchorRect` is the screen rectangle of the anchor character — the one
/// the cursor sits to the left of — as reported by
/// -[IMKTextInput attributesForCharacterIndex:lineHeightRectangle:].
/// The panel opens directly underneath it, flips above when the screen's
/// bottom is in the way, and is clamped to the screen's sides.
/// An empty rect means "the client would not tell us": the panel then keeps
/// wherever it was last placed.
- (void)showCandidates:(NSArray<NSString *> *)candidates
             pageIndex:(NSUInteger)pageIndex
             pageCount:(NSUInteger)pageCount
            anchorRect:(NSRect)anchorRect;

- (void)hide;

@end

NS_ASSUME_NONNULL_END
