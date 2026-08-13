// Draws the menu-bar icon into a multi-representation TIFF (@1x and @2x), so
// the repo carries no opaque binary blob for something this small.
// Compiled and run by the Makefile on the Mac; never on the Windows host.
//
//   make_icon <output.tiff> [aspect]
//
// Red rounded rectangle, white 特 — the same identity as the Windows build's
// icon (scripts/make_icon.py in art-shuangpin), in the shape macOS uses for
// every other input source.
//
// The geometry is not guessed. Measured off a @2x screenshot of the input
// menu with ABC, Squirrel and this input method listed together:
//
//   ABC        44 x 32 px  =  22 x 16 pt   corner reaches full width 3 px in
//   Squirrel   44 x 32 px  =  22 x 16 pt   (identical)
//   this, old  30 x 30 px  =  15 x 15 pt   corner 5 px in
//
// So the old icon was both square and a point short. 16 pt tall and 22 pt
// wide, filling the canvas edge to edge, reproduces Apple's tile exactly.
// The aspect is still an argument: pass a different one to `make` (see
// ICON_ASPECT in the Makefile) and the icon is redrawn without a rebuild.

#import <Cocoa/Cocoa.h>

#include <math.h>
#include <stdlib.h>

// Apple's own input-source tiles are 16 pt tall. Do not change this to make
// the icon bigger — the menu bar scales it back down and it only gets blurry.
static const CGFloat kPointHeight = 16.0;

// Width / height. 1.375 = 22:16, measured above.
static const CGFloat kDefaultAspect = 1.375;

// Corner radius as a fraction of the height. ABC reaches its full width
// 3 px (@2x) below its top edge, so its radius is ~1.75 pt on a 16 pt tile;
// this ratio lands there once antialiasing is accounted for.
static const CGFloat kCornerRatio = 0.14;

// Glyph size as a fraction of the height.
static const CGFloat kGlyphRatio = 0.72;

static NSBitmapImageRep *ArtDrawIconRep(CGFloat aspect, CGFloat scale) {
    CGFloat pointWidth = kPointHeight * aspect;
    NSInteger wide = (NSInteger)lround(pointWidth * scale);
    NSInteger high = (NSInteger)lround(kPointHeight * scale);

    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:NULL
                      pixelsWide:wide
                      pixelsHigh:high
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                     bytesPerRow:0
                    bitsPerPixel:0];
    if (rep == nil) {
        return nil;
    }

    // The rep's size still equals its pixel dimensions here, so the context
    // below is one unit per pixel. The @2x marking (size in points) is
    // applied after drawing.
    NSGraphicsContext *context =
        [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    if (context == nil) {
        return nil;
    }
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];

    // Full bleed: Apple's tiles measure the full 44 x 32 px, so any inset
    // here would make this one visibly smaller than the ones beside it.
    NSRect card = NSMakeRect(0, 0, wide, high);
    CGFloat radius = high * kCornerRatio;
    NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:card
                                                        xRadius:radius
                                                        yRadius:radius];
    [[NSColor colorWithSRGBRed:0.78 green:0.13 blue:0.13 alpha:1.0] setFill];
    [path fill];

    NSString *glyph = @"特";
    CGFloat fontSize = high * kGlyphRatio;
    NSFont *font = [NSFont fontWithName:@"PingFang TC" size:fontSize]
                       ?: [NSFont boldSystemFontOfSize:fontSize];
    NSDictionary *attributes = @{
        NSFontAttributeName : font,
        NSForegroundColorAttributeName : [NSColor whiteColor],
    };
    NSSize glyphSize = [glyph sizeWithAttributes:attributes];
    [glyph drawAtPoint:NSMakePoint((wide - glyphSize.width) / 2,
                                   (high - glyphSize.height) / 2)
        withAttributes:attributes];

    [NSGraphicsContext restoreGraphicsState];

    rep.size = NSMakeSize(pointWidth, kPointHeight);
    return rep;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSString *output = argc > 1 ? @(argv[1]) : @"ArtShuangpin.tiff";
        CGFloat aspect = argc > 2 ? atof(argv[2]) : kDefaultAspect;
        if (!(aspect > 0.1 && aspect < 10.0)) {
            fprintf(stderr, "make_icon: implausible aspect %g, using %g\n",
                    aspect, kDefaultAspect);
            aspect = kDefaultAspect;
        }

        NSBitmapImageRep *low = ArtDrawIconRep(aspect, 1.0);
        NSBitmapImageRep *high = ArtDrawIconRep(aspect, 2.0);
        if (low == nil || high == nil) {
            fprintf(stderr, "make_icon: could not create a bitmap context\n");
            return 1;
        }

        NSData *tiff = [NSBitmapImageRep
            representationOfImageRepsInArray:@[ low, high ]
                                   usingType:NSBitmapImageFileTypeTIFF
                                  properties:@{}];
        NSError *error = nil;
        if (![tiff writeToFile:output options:NSDataWritingAtomic
                         error:&error]) {
            fprintf(stderr, "make_icon: %s\n",
                    error.localizedDescription.UTF8String);
            return 1;
        }
        fprintf(stderr, "make_icon: %.0f x %.0f pt (aspect %g)\n",
                (double)(kPointHeight * aspect), (double)kPointHeight,
                (double)aspect);
    }
    return 0;
}
