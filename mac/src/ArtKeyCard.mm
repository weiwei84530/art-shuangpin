// See ArtKeyCard.h.

#import "ArtKeyCard.h"

namespace {

// One keycap's annotations. Mirrors ime/SampleIME/KeyCard.cpp and
// web/tutorials.js (INITIALS, SINGLE_VOWELS, FINALS, CONTROLS); change all
// three together.
struct KeyCap {
    const char *label;
    const char *initial;    // orange, top right
    const char *vowel;      // small, under the initial: the key alone as a syllable
    const char *finals[3];  // teal, bottom right, top to bottom
    const char *control;    // grey, bottom left
};

const KeyCap kRow0[] = {
    {"1", nullptr, nullptr, {}, "ˉ"},
    {"2", nullptr, nullptr, {}, "ˊ"},
    {"3", nullptr, nullptr, {}, "ˇ"},
    {"4", nullptr, nullptr, {}, "ˋ"},
    {"5", nullptr, nullptr, {}, "˙"},
    {"6", nullptr, nullptr, {}, "⌫"},
    {"7", nullptr, nullptr, {}, "ˋ 頁"},
    {"8", nullptr, nullptr, {}, "ˇ 選"},
    {"9", nullptr, nullptr, {}, "ˊ ◂"},
    {"0", nullptr, nullptr, {}, "ˉ ▸"},
};

const KeyCap kRow1[] = {
    {"Q", "ㄑ", "ㄧ", {"ㄧㄡ"}, nullptr},
    {"W", "ㄨ", nullptr, {"ㄧㄚ", "ㄨㄚ"}, nullptr},
    {"E", nullptr, nullptr, {"ㄜ"}, nullptr},
    {"R", "ㄖ", nullptr, {"ㄦ", "ㄨㄢ", "ㄩㄢ"}, nullptr},
    {"T", "ㄊ", "ㄜ", {"ㄩㄝ"}, nullptr},
    {"Y", "ㄧ", nullptr, {"ㄨㄞ", "ㄩ"}, nullptr},
    {"U", "ㄕ", nullptr, {"ㄨ", "ㄩ"}, nullptr},
    {"I", "ㄔ", nullptr, {"ㄧ"}, nullptr},
    {"O", nullptr, nullptr, {"ㄛ", "ㄨㄛ"}, nullptr},
    {"P", "ㄆ", "ㄛ", {"ㄨㄣ", "ㄩㄣ"}, nullptr},
};

const KeyCap kRow2[] = {
    {"A", nullptr, nullptr, {"ㄚ"}, nullptr},
    {"S", "ㄙ", nullptr, {"ㄨㄥ", "ㄩㄥ"}, nullptr},
    {"D", "ㄉ", "ㄜ", {"ㄧㄤ", "ㄨㄤ"}, nullptr},
    {"F", "ㄈ", "ㄛ", {"ㄣ"}, nullptr},
    {"G", "ㄍ", "ㄜ", {"ㄥ"}, nullptr},
    {"H", "ㄏ", "ㄜ", {"ㄤ"}, nullptr},
    {"J", "ㄐ", "ㄧ", {"ㄢ"}, nullptr},
    {"K", "ㄎ", "ㄜ", {"ㄠ"}, nullptr},
    {"L", "ㄌ", "ㄜ", {"ㄞ"}, nullptr},
    {";", nullptr, nullptr, {"ㄧㄥ"}, nullptr},
};

const KeyCap kRow3[] = {
    {"Z", "ㄗ", nullptr, {"ㄟ"}, nullptr},
    {"X", "ㄒ", "ㄧ", {"ㄧㄝ"}, nullptr},
    {"C", "ㄘ", nullptr, {"ㄧㄠ"}, nullptr},
    {"V", "ㄓ", nullptr, {"ㄨㄟ", "ㄩㄝ"}, nullptr},
    {"B", "ㄅ", "ㄛ", {"ㄡ"}, nullptr},
    {"N", "ㄋ", "ㄜ", {"ㄧㄣ"}, nullptr},
    {"M", "ㄇ", "ㄛ", {"ㄧㄢ"}, nullptr},
    {",", nullptr, nullptr, {}, "，"},
    {".", nullptr, nullptr, {}, "。"},
    {"/", nullptr, nullptr, {}, "、"},
};

struct Row {
    const KeyCap *keys;
    int count;
    int offsetQuarters;  // stagger, in quarters of a key pitch
};

const Row kRows[] = {
    {kRow0, 10, 0},
    {kRow1, 10, 2},
    {kRow2, 10, 3},
    {kRow3, 10, 5},
};
const int kRowCount = 4;

// Points; the Windows card uses the same numbers as 96-dpi pixels.
const CGFloat kKeyWidth = 66;
const CGFloat kKeyHeight = 76;
const CGFloat kKeyGap = 5;
const CGFloat kPadding = 14;
const CGFloat kTitleHeight = 26;
const CGFloat kCornerRadius = 10;
const CGFloat kKeyRadius = 6;
const CGFloat kKeyInset = 5;
const CGFloat kZhuyinLine = 16;

const CGFloat kLetterSize = 17;
const CGFloat kZhuyinSize = 14;
const CGFloat kVowelSize = 12;
const CGFloat kTitleSize = 13;

NSString *S(const char *utf8) {
    return [NSString stringWithUTF8String:utf8];
}

// Darker than the tutorial site's colours: those sit on a grey keycap, these
// on a light one. Dynamic, so the card follows dark mode like the candidate
// panel does.
NSColor *InitialColor() { return [NSColor systemOrangeColor]; }
NSColor *FinalColor() { return [NSColor systemTealColor]; }
NSColor *ControlColor() { return [NSColor secondaryLabelColor]; }

int TotalColumnsQuarters() {
    int widest = 0;
    for (int r = 0; r < kRowCount; ++r) {
        widest = MAX(widest, kRows[r].offsetQuarters + kRows[r].count * 4);
    }
    return widest;
}

NSSize CardSize() {
    const CGFloat pitch = kKeyWidth + kKeyGap;
    return NSMakeSize(kPadding * 2 + pitch * TotalColumnsQuarters() / 4.0 - kKeyGap,
                      kPadding * 2 + kTitleHeight + kRowCount * (kKeyHeight + kKeyGap) - kKeyGap);
}

NSFont *CardFont(CGFloat size, NSFontWeight weight) {
    NSFont *font = [NSFont fontWithName:@"PingFang TC" size:size];
    if (font != nil && weight >= NSFontWeightSemibold) {
        font = [[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSBoldFontMask];
    }
    return font ?: [NSFont systemFontOfSize:size weight:weight];
}

}  // namespace

#pragma mark - view

@interface ArtKeyCardView : NSView
@end

@implementation ArtKeyCardView

- (BOOL)isFlipped {
    return YES;
}

static void DrawRight(NSString *text, NSDictionary *attributes, CGFloat right, CGFloat y) {
    NSSize size = [text sizeWithAttributes:attributes];
    [text drawAtPoint:NSMakePoint(right - size.width, y) withAttributes:attributes];
}

- (void)drawRect:(NSRect)dirtyRect {
    NSBezierPath *card = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 0.5, 0.5)
                                                         xRadius:kCornerRadius
                                                         yRadius:kCornerRadius];
    [[[NSColor windowBackgroundColor] colorWithAlphaComponent:0.97] setFill];
    [card fill];
    [[NSColor separatorColor] setStroke];
    card.lineWidth = 1.0;
    [card stroke];

    NSFont *titleFont = CardFont(kTitleSize, NSFontWeightRegular);
    NSFont *letterFont = CardFont(kLetterSize, NSFontWeightSemibold);
    NSFont *zhuyinFont = CardFont(kZhuyinSize, NSFontWeightBold);
    NSFont *vowelFont = CardFont(kVowelSize, NSFontWeightBold);

    // Title line doubles as the legend.
    CGFloat x = kPadding;
    const CGFloat titleY = kPadding;
    struct Run { NSString *text; NSColor *color; };
    const Run legend[] = {
        {@"鍵位提示　", [NSColor labelColor]},
        {@"■ 聲母　", InitialColor()},
        {@"■ 韻母　", FinalColor()},
        {@"■ 聲調／功能", ControlColor()},
    };
    for (const Run &run : legend) {
        NSDictionary *a = @{NSFontAttributeName : titleFont, NSForegroundColorAttributeName : run.color};
        [run.text drawAtPoint:NSMakePoint(x, titleY) withAttributes:a];
        x += [run.text sizeWithAttributes:a].width;
    }
    DrawRight(@"按任意鍵關閉",
              @{NSFontAttributeName : titleFont,
                NSForegroundColorAttributeName : [NSColor tertiaryLabelColor]},
              NSWidth(self.bounds) - kPadding, titleY);

    NSDictionary *letterAttr = @{NSFontAttributeName : letterFont,
                                 NSForegroundColorAttributeName : [NSColor labelColor]};
    NSDictionary *initialAttr = @{NSFontAttributeName : zhuyinFont,
                                  NSForegroundColorAttributeName : InitialColor()};
    NSDictionary *vowelAttr = @{NSFontAttributeName : vowelFont,
                                NSForegroundColorAttributeName : InitialColor()};
    NSDictionary *finalAttr = @{NSFontAttributeName : zhuyinFont,
                                NSForegroundColorAttributeName : FinalColor()};
    NSDictionary *controlAttr = @{NSFontAttributeName : zhuyinFont,
                                  NSForegroundColorAttributeName : ControlColor()};

    const CGFloat pitchX = kKeyWidth + kKeyGap;
    const CGFloat pitchY = kKeyHeight + kKeyGap;
    const CGFloat lineHeight = ceil(zhuyinFont.ascender - zhuyinFont.descender);

    for (int r = 0; r < kRowCount; ++r) {
        const Row &row = kRows[r];
        const CGFloat top = kPadding + kTitleHeight + r * pitchY;
        for (int i = 0; i < row.count; ++i) {
            const KeyCap &cap = row.keys[i];
            const CGFloat left = kPadding + pitchX * row.offsetQuarters / 4.0 + i * pitchX;
            NSRect key = NSMakeRect(left, top, kKeyWidth, kKeyHeight);

            NSBezierPath *keyPath = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(key, 0.5, 0.5)
                                                                    xRadius:kKeyRadius
                                                                    yRadius:kKeyRadius];
            [[NSColor controlBackgroundColor] setFill];
            [keyPath fill];
            [[NSColor separatorColor] setStroke];
            [keyPath stroke];

            const NSRect inner = NSInsetRect(key, kKeyInset, kKeyInset - 2);
            [S(cap.label) drawAtPoint:inner.origin withAttributes:letterAttr];

            if (cap.initial != nullptr) {
                DrawRight(S(cap.initial), initialAttr, NSMaxX(inner), NSMinY(inner) + 1);
                if (cap.vowel != nullptr) {
                    DrawRight(S(cap.vowel), vowelAttr, NSMaxX(inner), NSMinY(inner) + 1 + kZhuyinLine);
                }
            }

            int finalsCount = 0;
            while (finalsCount < 3 && cap.finals[finalsCount] != nullptr) {
                ++finalsCount;
            }
            for (int f = 0; f < finalsCount; ++f) {
                const CGFloat y = NSMaxY(inner) + 1 - lineHeight - (finalsCount - 1 - f) * kZhuyinLine;
                DrawRight(S(cap.finals[f]), finalAttr, NSMaxX(inner), y);
            }

            if (cap.control != nullptr) {
                [S(cap.control) drawAtPoint:NSMakePoint(NSMinX(inner), NSMaxY(inner) + 1 - lineHeight)
                             withAttributes:controlAttr];
            }
        }
    }
}

@end

#pragma mark - controller

@implementation ArtKeyCard {
    NSPanel *_panel;
    ArtKeyCardView *_view;
}

+ (ArtKeyCard *)shared {
    static ArtKeyCard *instance = nil;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        instance = [[ArtKeyCard alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        NSRect frame = NSMakeRect(0, 0, CardSize().width, CardSize().height);
        _panel = [[NSPanel alloc] initWithContentRect:frame
                                            styleMask:NSWindowStyleMaskBorderless |
                                                      NSWindowStyleMaskNonactivatingPanel
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
        // Same window setup as the candidate panel and the mode HUD: above
        // full-screen presentations, on every space, never takes focus or a
        // click. See ArtCandidateWindow.mm for why the level is this one.
        _panel.floatingPanel = YES;
        _panel.level = CGShieldingWindowLevel();
        _panel.opaque = NO;
        _panel.backgroundColor = [NSColor clearColor];
        _panel.hasShadow = YES;
        _panel.hidesOnDeactivate = NO;
        _panel.ignoresMouseEvents = YES;
        _panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                    NSWindowCollectionBehaviorStationary |
                                    NSWindowCollectionBehaviorFullScreenAuxiliary |
                                    NSWindowCollectionBehaviorIgnoresCycle;
        _view = [[ArtKeyCardView alloc] initWithFrame:frame];
        _panel.contentView = _view;
    }
    return self;
}

- (BOOL)isVisible {
    return _panel.isVisible;
}

- (void)showNearRect:(NSRect)caretRect {
    NSPoint probe = NSIsEmptyRect(caretRect) ? [NSEvent mouseLocation] : caretRect.origin;
    NSScreen *screen = [NSScreen mainScreen];
    for (NSScreen *candidate in [NSScreen screens]) {
        if (NSPointInRect(probe, candidate.frame)) {
            screen = candidate;
            break;
        }
    }
    const NSRect visible = screen ? screen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
    const NSSize size = CardSize();
    const NSRect frame = NSMakeRect(NSMidX(visible) - size.width / 2,
                                    NSMidY(visible) - size.height / 2,
                                    size.width, size.height);
    [_panel setFrame:frame display:NO];
    _view.frame = NSMakeRect(0, 0, size.width, size.height);
    [_view setNeedsDisplay:YES];
    [_view display];
    [_panel orderFront:nil];
}

- (void)hide {
    [_panel orderOut:nil];
}

@end
