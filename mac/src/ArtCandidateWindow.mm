// See ArtCandidateWindow.h.

#import "ArtCandidateWindow.h"

#import "ArtBridge.h"  // ArtLog

// Visual constants. Deliberately all in one place: the look is the last
// thing to be tuned and it should be tunable without reading the layout
// code (spec §6 白底圓角卡片、細邊框、選中列淡藍、灰色數字標籤、右下角頁碼).
static const CGFloat kCornerRadius = 8.0;
static const CGFloat kBorderWidth = 1.0;
static const CGFloat kPanelPaddingX = 10.0;
static const CGFloat kPanelPaddingY = 7.0;
static const CGFloat kRowPaddingY = 3.0;
static const CGFloat kLabelGap = 8.0;
static const CGFloat kAnchorGap = 3.0;   // between the text line and the panel
static const CGFloat kCandidateFontSize = 18.0;
static const CGFloat kLabelFontSize = 12.0;
static const CGFloat kMinPanelWidth = 90.0;

#pragma mark - view

@interface ArtCandidateView : NSView
@property (nonatomic, copy) NSArray<NSString *> *candidates;
@property (nonatomic) NSUInteger pageIndex;
@property (nonatomic) NSUInteger pageCount;
@property (nonatomic, weak) id<ArtCandidateWindowDelegate> delegate;
@property (nonatomic, readonly) NSSize preferredSize;
@end

@implementation ArtCandidateView {
    CGFloat _rowHeight;
    CGFloat _labelWidth;
    NSSize _preferredSize;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _candidates = @[];
    }
    return self;
}

// Top-down layout; NSString drawing honours this.
- (BOOL)isFlipped {
    return YES;
}

- (NSFont *)candidateFont {
    return [NSFont fontWithName:@"PingFang TC" size:kCandidateFontSize]
               ?: [NSFont systemFontOfSize:kCandidateFontSize];
}

- (NSFont *)labelFont {
    return [NSFont monospacedDigitSystemFontOfSize:kLabelFontSize
                                            weight:NSFontWeightRegular];
}

- (NSDictionary *)candidateAttributes {
    return @{
        NSFontAttributeName : [self candidateFont],
        NSForegroundColorAttributeName : [NSColor labelColor],
    };
}

- (NSDictionary *)labelAttributes {
    return @{
        NSFontAttributeName : [self labelFont],
        NSForegroundColorAttributeName : [NSColor secondaryLabelColor],
    };
}

- (NSString *)labelForRow:(NSUInteger)row {
    return [NSString stringWithFormat:@"%lu", (unsigned long)(row + 1)];
}

- (NSString *)pageText {
    if (_pageCount == 0) {
        return @"";
    }
    return [NSString stringWithFormat:@"%lu/%lu", (unsigned long)(_pageIndex + 1),
                                      (unsigned long)_pageCount];
}

- (void)relayout {
    NSDictionary *candidateAttrs = [self candidateAttributes];
    NSDictionary *labelAttrs = [self labelAttributes];

    _labelWidth = 0;
    CGFloat textWidth = 0;
    CGFloat lineHeight = [@"字" sizeWithAttributes:candidateAttrs].height;
    for (NSUInteger i = 0; i < _candidates.count; ++i) {
        NSSize label = [[self labelForRow:i] sizeWithAttributes:labelAttrs];
        NSSize text = [_candidates[i] sizeWithAttributes:candidateAttrs];
        _labelWidth = MAX(_labelWidth, label.width);
        textWidth = MAX(textWidth, text.width);
        lineHeight = MAX(lineHeight, text.height);
    }
    _rowHeight = ceil(lineHeight) + kRowPaddingY * 2;

    NSSize page = [[self pageText] sizeWithAttributes:labelAttrs];
    CGFloat width = kPanelPaddingX * 2 + _labelWidth + kLabelGap + textWidth;
    width = MAX(width, kPanelPaddingX * 2 + page.width + 24);
    width = MAX(width, kMinPanelWidth);

    CGFloat height = kPanelPaddingY * 2 + _rowHeight * _candidates.count;
    if (page.width > 0) {
        height += ceil(page.height) + 2;
    }
    _preferredSize = NSMakeSize(ceil(width), ceil(height));
}

- (NSSize)preferredSize {
    return _preferredSize;
}

- (NSRect)rectForRow:(NSUInteger)row {
    return NSMakeRect(kBorderWidth, kPanelPaddingY + _rowHeight * row,
                      NSWidth(self.bounds) - kBorderWidth * 2, _rowHeight);
}

- (void)drawRect:(NSRect)dirtyRect {
    NSRect bounds = NSInsetRect(self.bounds, kBorderWidth / 2, kBorderWidth / 2);
    NSBezierPath *card = [NSBezierPath bezierPathWithRoundedRect:bounds
                                                        xRadius:kCornerRadius
                                                        yRadius:kCornerRadius];
    [[NSColor controlBackgroundColor] setFill];
    [card fill];
    [[NSColor separatorColor] setStroke];
    card.lineWidth = kBorderWidth;
    [card stroke];

    NSDictionary *candidateAttrs = [self candidateAttributes];
    NSDictionary *labelAttrs = [self labelAttributes];

    // The composer has no moving cursor inside the menu — selection is by
    // digit — so the emphasis stays on the first row, matching the Windows
    // presenter's initial selection.
    if (_candidates.count > 0) {
        NSRect row = [self rectForRow:0];
        [[[NSColor selectedContentBackgroundColor] colorWithAlphaComponent:0.25]
            setFill];
        NSRectFillUsingOperation(row, NSCompositingOperationSourceOver);
    }

    for (NSUInteger i = 0; i < _candidates.count; ++i) {
        NSRect row = [self rectForRow:i];
        NSString *label = [self labelForRow:i];
        NSSize labelSize = [label sizeWithAttributes:labelAttrs];
        NSSize textSize = [_candidates[i] sizeWithAttributes:candidateAttrs];

        CGFloat labelX = kPanelPaddingX + (_labelWidth - labelSize.width);
        [label drawAtPoint:NSMakePoint(labelX,
                                       NSMinY(row) +
                                           (NSHeight(row) - labelSize.height) / 2)
            withAttributes:labelAttrs];

        CGFloat textX = kPanelPaddingX + _labelWidth + kLabelGap;
        [_candidates[i]
              drawAtPoint:NSMakePoint(textX, NSMinY(row) +
                                                 (NSHeight(row) -
                                                  textSize.height) / 2)
           withAttributes:candidateAttrs];
    }

    NSString *page = [self pageText];
    if (page.length > 0) {
        NSSize size = [page sizeWithAttributes:labelAttrs];
        [page drawAtPoint:NSMakePoint(NSWidth(self.bounds) - kPanelPaddingX -
                                          size.width,
                                      NSHeight(self.bounds) - kPanelPaddingY -
                                          size.height + 2)
           withAttributes:labelAttrs];
    }
}

#pragma mark mouse

- (BOOL)acceptsFirstMouse:(NSEvent *)event {
    return YES;
}

- (void)mouseUp:(NSEvent *)event {
    NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    for (NSUInteger i = 0; i < _candidates.count; ++i) {
        if (NSPointInRect(point, [self rectForRow:i])) {
            [self.delegate candidateWindowDidSelectRow:i];
            return;
        }
    }
}

@end

#pragma mark - window

@implementation ArtCandidateWindow {
    NSPanel *_panel;
    ArtCandidateView *_view;
    // Whether the panel is meant to be on screen right now, as distinct from
    // whether it currently is. Needed because a space change can order it
    // out from underneath us — see -observeSpaceChanges.
    BOOL _wantsVisible;
}

+ (ArtCandidateWindow *)shared {
    static ArtCandidateWindow *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[ArtCandidateWindow alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [self buildPanel];
        [self observeSpaceChanges];
    }
    return self;
}

// Switching spaces — Mission Control, a full-screen app, or a window manager
// bound to ⌥+arrow — can leave the panel behind on the space it was ordered
// front on, even with NSWindowCollectionBehaviorCanJoinAllSpaces. Re-ordering
// it front on the new space is cheap and idempotent; doing it only when the
// panel is supposed to be up means an idle input method does nothing at all.
- (void)observeSpaceChanges {
    [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(NSNotification *note) {
                    if (self->_wantsVisible) {
                        [self->_panel orderFront:nil];
                    }
                }];
}

- (void)buildPanel {
    NSRect frame = NSMakeRect(0, 0, kMinPanelWidth, 40);
    _panel = [[NSPanel alloc]
        initWithContentRect:frame
                  styleMask:NSWindowStyleMaskBorderless |
                            NSWindowStyleMaskNonactivatingPanel
                    backing:NSBackingStoreBuffered
                      defer:NO];
    // A non-activating panel owned by a background-only process: it shows
    // above everything, never takes focus, and follows the user across
    // spaces and into full-screen apps.
    //
    // The level is CGShieldingWindowLevel(), not NSPopUpMenuWindowLevel
    // (2026-09-08). A pop-up-menu-level panel is above ordinary windows but
    // NOT above the window server's full-screen presentation, so a host that
    // has taken over a display — a full-screen Electron app such as Cursor,
    // or a game — draws over the candidate list and the user is picking
    // characters blind. Squirrel fixed the same report the same way
    // (rime/squirrel cee5c5d, "display panel on top level in the proper
    // way"); this is the level a candidate window is expected to sit at.
    _panel.floatingPanel = YES;
    _panel.level = CGShieldingWindowLevel();
    _panel.opaque = NO;
    _panel.backgroundColor = [NSColor clearColor];
    _panel.hasShadow = YES;
    _panel.hidesOnDeactivate = NO;
    _panel.becomesKeyOnlyIfNeeded = YES;
    _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorFullScreenAuxiliary |
                                NSWindowCollectionBehaviorIgnoresCycle;

    _view = [[ArtCandidateView alloc] initWithFrame:frame];
    _panel.contentView = _view;
}

- (BOOL)isVisible {
    return [_panel isVisible];
}

- (void)setDelegate:(id<ArtCandidateWindowDelegate>)delegate {
    _delegate = delegate;
    _view.delegate = delegate;
}

- (void)showCandidates:(NSArray<NSString *> *)candidates
             pageIndex:(NSUInteger)pageIndex
             pageCount:(NSUInteger)pageCount
            anchorRect:(NSRect)anchorRect {
    if (candidates.count == 0) {
        [self hide];
        return;
    }
    _view.candidates = candidates;
    _view.pageIndex = pageIndex;
    _view.pageCount = pageCount;
    [_view relayout];

    NSSize size = _view.preferredSize;
    NSRect frame = _panel.frame;
    frame.size = size;
    // Height changes with the page, so the position has to be recomputed
    // every refresh, not only on first show.
    frame.origin = [self originForSize:size anchorRect:anchorRect
                              fallback:frame.origin];
    [_panel setFrame:frame display:YES];
    _view.frame = NSMakeRect(0, 0, size.width, size.height);
    [_view setNeedsDisplay:YES];
    _wantsVisible = YES;
    [_panel orderFront:nil];
}

- (NSPoint)originForSize:(NSSize)size
              anchorRect:(NSRect)anchorRect
                fallback:(NSPoint)fallback {
    if (NSIsEmptyRect(anchorRect)) {
        // The client would not report a rectangle. Keeping the previous
        // position is the least surprising thing we can do; jumping to the
        // corner of the screen is not.
        //
        // Unless the previous position is no longer ON a screen (2026-09-08).
        // That is what "the candidate window disappeared" turned out to mean
        // after a display or space change: the panel was still ordered front,
        // still at the coordinates of a caret that had since moved to another
        // display, and therefore nowhere the user could see it. A host that
        // reports no rectangle — Electron ones frequently do not — never
        // corrects it, so the stale position persists for the whole
        // selection. When it has gone off-screen, the pointer is the best
        // guess left about where the user is looking.
        NSRect card = NSMakeRect(fallback.x, fallback.y, size.width, size.height);
        for (NSScreen *screen in [NSScreen screens]) {
            if (NSContainsRect(screen.visibleFrame, card)) {
                return fallback;
            }
        }
        NSPoint mouse = [NSEvent mouseLocation];
        NSScreen *screen = [self screenForPoint:mouse];
        NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
        NSPoint origin = NSMakePoint(mouse.x, mouse.y - kAnchorGap - size.height);
        if (origin.y < NSMinY(visible))                origin.y = NSMinY(visible);
        if (origin.x + size.width > NSMaxX(visible))   origin.x = NSMaxX(visible) - size.width;
        if (origin.x < NSMinX(visible))                origin.x = NSMinX(visible);
        return origin;
    }

    NSScreen *screen = [self screenForPoint:anchorRect.origin];
    NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);

    // Directly under the anchor character (spec §6 「候選窗開在游標錨點字的
    // 正下方」). Screen coordinates grow upward, so "below" is smaller y.
    NSPoint origin = NSMakePoint(NSMinX(anchorRect),
                                 NSMinY(anchorRect) - kAnchorGap - size.height);

    if (origin.y < NSMinY(visible)) {
        // Not enough room underneath: flip above the anchor.
        origin.y = NSMaxY(anchorRect) + kAnchorGap;
    }
    if (origin.y + size.height > NSMaxY(visible)) {
        origin.y = NSMaxY(visible) - size.height;
    }
    if (origin.x + size.width > NSMaxX(visible)) {
        origin.x = NSMaxX(visible) - size.width;
    }
    if (origin.x < NSMinX(visible)) {
        origin.x = NSMinX(visible);
    }
    return origin;
}

- (NSScreen *)screenForPoint:(NSPoint)point {
    for (NSScreen *screen in [NSScreen screens]) {
        if (NSPointInRect(point, screen.frame)) {
            return screen;
        }
    }
    return [NSScreen mainScreen];
}

- (void)hide {
    _wantsVisible = NO;
    if ([_panel isVisible]) {
        [_panel orderOut:nil];
    }
}

@end
