// See ArtModeHUD.h.

#import "ArtModeHUD.h"

static const NSTimeInterval kHoldSeconds = 0.7;
static const NSTimeInterval kFadeSeconds = 0.25;
static const CGFloat kHUDSize = 44.0;
static const CGFloat kHUDCorner = 10.0;
static const CGFloat kHUDGap = 6.0;
static const CGFloat kHUDFontSize = 24.0;

#pragma mark - view

@interface ArtModeHUDView : NSView
@property (nonatomic, copy) NSString *glyph;
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
    [[[NSColor windowBackgroundColor] colorWithAlphaComponent:0.95] setFill];
    [card fill];
    [[NSColor separatorColor] setStroke];
    card.lineWidth = 1.0;
    [card stroke];

    if (_glyph.length == 0) {
        return;
    }
    NSFont *font = [NSFont fontWithName:@"PingFang TC" size:kHUDFontSize]
                       ?: [NSFont systemFontOfSize:kHUDFontSize];
    NSDictionary *attributes = @{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName : [NSColor labelColor],
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
        NSRect frame = NSMakeRect(0, 0, kHUDSize, kHUDSize);
        _panel = [[NSPanel alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskBorderless |
                                NSWindowStyleMaskNonactivatingPanel
                        backing:NSBackingStoreBuffered
                          defer:NO];
        _panel.floatingPanel = YES;
        _panel.level = NSPopUpMenuWindowLevel;
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
    _view.glyph = chinese ? @"中" : @"英";
    [_view setNeedsDisplay:YES];

    NSRect frame = _panel.frame;
    frame.size = NSMakeSize(kHUDSize, kHUDSize);
    frame.origin = [self originForRect:caretRect size:frame.size];
    [_panel setFrame:frame display:YES];

    [_timer invalidate];
    _panel.alphaValue = 1.0;
    [_panel orderFront:nil];

    NSPanel *panel = _panel;
    _timer = [NSTimer scheduledTimerWithTimeInterval:kHoldSeconds
                                             repeats:NO
                                               block:^(NSTimer *timer) {
        [NSAnimationContext runAnimationGroup:^(NSAnimationContext *context) {
            context.duration = kFadeSeconds;
            panel.animator.alphaValue = 0.0;
        } completionHandler:^{
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

    if (NSIsEmptyRect(caretRect)) {
        return NSMakePoint(NSMidX(visible) - size.width / 2,
                           NSMinY(visible) + NSHeight(visible) * 0.25);
    }

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
