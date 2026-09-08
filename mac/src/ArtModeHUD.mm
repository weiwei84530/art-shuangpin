// See ArtModeHUD.h.

#import "ArtModeHUD.h"

// Matched to the Windows card (ime/SampleIME/ModeIndicator.cpp): 28x24 at
// 96 dpi with an 11pt glyph, which is these numbers in points. Small on
// purpose -- it is a glance, not a dialog.
static const NSTimeInterval kHoldSeconds = 0.55;
static const NSTimeInterval kFadeSeconds = 0.25;
static const CGFloat kHUDWidth = 30.0;
static const CGFloat kHUDHeight = 26.0;
static const CGFloat kHUDCorner = 6.0;
static const CGFloat kHUDGap = 5.0;
static const CGFloat kHUDFontSize = 15.0;

// Chinese mode wears the app icon's own red (mac/tools/make_icon.m: a white
// 特 on sRGB 0.78/0.13/0.13), so the card that says "you are typing
// Chinese" and the icon in the input-source menu are recognizably the same
// thing.  English mode keeps the plain system card, so the two modes differ
// by more than one glyph.  Same split as the Windows card, which takes its
// red from the Windows icon (#C42B1C, scripts/make_icon.py) -- the two
// icons have always been a shade apart, and each card follows its own.
static NSColor *ArtChineseCardColor(void) {
    return [NSColor colorWithSRGBRed:0.78 green:0.13 blue:0.13 alpha:0.95];
}

static NSColor *ArtChineseBorderColor(void) {
    return [NSColor colorWithSRGBRed:0.63 green:0.10 blue:0.10 alpha:1.0];
}

#pragma mark - view

@interface ArtModeHUDView : NSView
@property (nonatomic, copy) NSString *glyph;
@property (nonatomic, assign) BOOL chinese;
@end

@implementation ArtModeHUDView

- (BOOL)isFlipped {
    return YES;
}

- (void)drawRect:(NSRect)dirtyRect {
    NSBezierPath *card =
        [NSBezierPath bezierPathWithRoundedRect:self.bounds
                                        xRadius:kHUDCorner
                                        yRadius:kHUDCorner];
    [(_chinese ? ArtChineseCardColor()
               : [[NSColor windowBackgroundColor] colorWithAlphaComponent:0.95]) setFill];
    [card fill];
    [(_chinese ? ArtChineseBorderColor() : [NSColor separatorColor]) setStroke];
    card.lineWidth = 1.0;
    [card stroke];

    if (_glyph.length == 0) {
        return;
    }
    NSFont *font = [NSFont fontWithName:@"PingFang TC" size:kHUDFontSize]
                       ?: [NSFont systemFontOfSize:kHUDFontSize];
    NSDictionary *attributes = @{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName :
            (_chinese ? [NSColor whiteColor] : [NSColor labelColor]),
    };
    NSSize size = [_glyph sizeWithAttributes:attributes];
    [_glyph drawAtPoint:NSMakePoint((NSWidth(self.bounds) - size.width) / 2,
                                    (NSHeight(self.bounds) - size.height) / 2)
         withAttributes:attributes];
}

@end

#pragma mark - controller

@implementation ArtModeHUD {
    NSPanel *_panel;
    ArtModeHUDView *_view;
    NSTimer *_timer;
    // Bumped by every flash. The fade-out is an animation, so a flash that
    // arrives while one is running would otherwise be faded out by the
    // animation it did not start -- and then ordered out by its completion
    // handler. Only the generation that scheduled a fade may finish it.
    NSUInteger _generation;
}

+ (ArtModeHUD *)shared {
    static ArtModeHUD *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[ArtModeHUD alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSRect frame = NSMakeRect(0, 0, kHUDWidth, kHUDHeight);
        _panel = [[NSPanel alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskBorderless |
                                NSWindowStyleMaskNonactivatingPanel
                        backing:NSBackingStoreBuffered
                          defer:NO];
        _panel.floatingPanel = YES;
        // Same level as the candidate panel, for the same reason: a
        // pop-up-menu-level window is not above a full-screen presentation.
        // See ArtCandidateWindow.mm.
        _panel.level = CGShieldingWindowLevel();
        _panel.opaque = NO;
        _panel.backgroundColor = [NSColor clearColor];
        _panel.hasShadow = YES;
        _panel.hidesOnDeactivate = NO;
        _panel.ignoresMouseEvents = YES;
        _panel.collectionBehavior =
            NSWindowCollectionBehaviorCanJoinAllSpaces |
            NSWindowCollectionBehaviorStationary |
            NSWindowCollectionBehaviorFullScreenAuxiliary |
            NSWindowCollectionBehaviorIgnoresCycle;
        _view = [[ArtModeHUDView alloc] initWithFrame:frame];
        _panel.contentView = _view;
    }
    return self;
}

- (void)flashChinese:(BOOL)chinese nearRect:(NSRect)caretRect {
    if (NSIsEmptyRect(caretRect)) {
        // The card is a LABEL ON THE CARET: with no caret to label there is
        // nothing to show (2026-09-09, at the user's direction). Guessing
        // from the pointer put it next to things that are not text fields.
        // Same rule as the Windows card.
        return;
    }
    [_timer invalidate];
    _timer = nil;
    const NSUInteger generation = ++_generation;

    // Nothing stale may reach the screen, not even for one frame: a panel
    // that is still up from the previous flash would otherwise be moved to
    // the new position carrying the OLD glyph, and only redraw afterwards.
    // Order it out, draw synchronously, then bring it back.
    [_panel orderOut:nil];

    _view.glyph = chinese ? @"中" : @"英";
    _view.chinese = chinese;

    NSRect frame = _panel.frame;
    frame.size = NSMakeSize(kHUDWidth, kHUDHeight);
    frame.origin = [self originForRect:caretRect size:frame.size];
    [_panel setFrame:frame display:NO];
    _view.frame = NSMakeRect(0, 0, frame.size.width, frame.size.height);
    [_view setNeedsDisplay:YES];
    [_view display];

    _panel.alphaValue = 1.0;
    [_panel orderFront:nil];

    __weak ArtModeHUD *weakSelf = self;
    NSPanel *panel = _panel;
    _timer = [NSTimer scheduledTimerWithTimeInterval:kHoldSeconds
                                             repeats:NO
                                               block:^(NSTimer *timer) {
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
            context.duration = kFadeSeconds;
            panel.animator.alphaValue = 0.0;
        } completionHandler:^{
            ArtModeHUD *strongSelf = weakSelf;
            if (strongSelf == nil || strongSelf->_generation != generation) {
                return;  // a newer flash owns the panel now
            }
            [panel orderOut:nil];
            panel.alphaValue = 1.0;
        }];
    }];
}

- (NSPoint)originForRect:(NSRect)caretRect size:(NSSize)size {
    NSScreen *screen = [NSScreen mainScreen];
    for (NSScreen *candidate in [NSScreen screens]) {
        if (NSPointInRect(caretRect.origin, candidate.frame)) {
            screen = candidate;
            break;
        }
    }
    NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);

    NSPoint origin = NSMakePoint(NSMinX(caretRect) + kHUDGap,
                                 NSMinY(caretRect) - kHUDGap - size.height);
    if (origin.y < NSMinY(visible)) {
        origin.y = NSMaxY(caretRect) + kHUDGap;
    }
    if (origin.x + size.width > NSMaxX(visible)) {
        origin.x = NSMaxX(visible) - size.width;
    }
    if (origin.x < NSMinX(visible)) {
        origin.x = NSMinX(visible);
    }
    return origin;
}

@end
