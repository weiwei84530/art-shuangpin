// The IMKInputController.  Named in resources/Info.plist under
// InputMethodServerControllerClass, and instantiated by IMK through the
// ObjC runtime — renaming the class means editing the plist too.
//
// Its whole job is key routing (spec §6) plus rendering.  Every decision
// about what a key *means* while composing belongs to mspy::Composer; the
// shell owns only the idle editing layer (the unshifted digit row), the bare-Shift
// toggle, the numpad exemption, the "arrows do nothing while composing"
// rule, routing English-mode keys into the same composition, and drawing.
// See CLAUDE.md, "Key ownership — shell vs composer".

#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#import "ArtCandidateWindow.h"

NS_ASSUME_NONNULL_BEGIN

@interface ArtInputController : IMKInputController <ArtCandidateWindowDelegate>
@end

NS_ASSUME_NONNULL_END
