// Generates the BG3 Camera Unlock application icon.
//
// Every pixel here is drawn from vector primitives defined in this file.
// The tool reads no input image and touches no installed game asset: the
// icon must be free of Larian Studios artwork, wordmarks, and trade dress,
// because it ships in every release archive.
//
// The mark is a camera at the pivot of a graduated angular gauge, set in an
// ornate gilt frame. The gauge is not
// decorative - it is drawn between the mod's own default pitch limits,
// -45 degrees and +85 degrees, so the icon states what the mod does. The
// open ends of the sweep carry the "unlocked" reading.
//
// Usage:
//   make_app_icon <output.iconset>
//
// Produces every resolution macOS asks for, each rendered from the vectors
// at its native size rather than downsampled, so small sizes stay crisp.
// Convert the result with:
//   iconutil -c icns <output.iconset>

#import <AppKit/AppKit.h>

#include <cmath>
#include <initializer_list>
#include <cstdio>

namespace {

// The whole design is authored in a 1024x1024 space and scaled per output
// size. Keeping one authoring space means proportions cannot drift between
// resolutions.
constexpr CGFloat kCanvas = 1024.0;

// The mod's default pitch limits, in degrees. The sweep is drawn between
// them, so changing the defaults would visibly change the icon.
constexpr CGFloat kPitchMinimumDegrees = -45.0;
constexpr CGFloat kPitchMaximumDegrees = 85.0;

NSColor* Rgb(const CGFloat red, const CGFloat green, const CGFloat blue) {
    return [NSColor colorWithSRGBRed:red green:green blue:blue alpha:1.0];
}

// Deep burgundy and aged gold, with a parchment highlight.
//
// This palette and the ornate framing are generic high-fantasy CRPG cues -
// gilt borders, dark red leather, an engraved measuring instrument. None of
// it reproduces anyone's logo, wordmark, emblem, or trade dress, which is
// the line that matters: the icon ships in every release archive.
NSColor* PlateCentre() { return Rgb(0.430, 0.110, 0.082); }
NSColor* PlateEdge() { return Rgb(0.130, 0.038, 0.032); }
NSColor* Gold() { return Rgb(0.878, 0.682, 0.325); }
NSColor* GoldDeep() { return Rgb(0.639, 0.443, 0.169); }
NSColor* GoldShadow() { return Rgb(0.310, 0.196, 0.071); }
NSColor* Parchment() { return Rgb(0.925, 0.882, 0.792); }
NSColor* LensDark() { return Rgb(0.086, 0.055, 0.047); }

CGFloat Radians(const CGFloat degrees) {
    return degrees * static_cast<CGFloat>(M_PI) / 180.0;
}

NSPoint Polar(const NSPoint origin, const CGFloat degrees, const CGFloat radius) {
    const CGFloat radians = Radians(degrees);
    return NSMakePoint(origin.x + std::cos(radians) * radius,
                       origin.y + std::sin(radians) * radius);
}

// A four-pointed gilt star, the corner ornament of the frame.
void DrawFlourish(const NSPoint centre, const CGFloat radius) {
    NSBezierPath* star = [NSBezierPath bezierPath];
    const CGFloat waist = radius * 0.30;
    [star moveToPoint:NSMakePoint(centre.x, centre.y + radius)];
    [star lineToPoint:NSMakePoint(centre.x + waist, centre.y + waist)];
    [star lineToPoint:NSMakePoint(centre.x + radius, centre.y)];
    [star lineToPoint:NSMakePoint(centre.x + waist, centre.y - waist)];
    [star lineToPoint:NSMakePoint(centre.x, centre.y - radius)];
    [star lineToPoint:NSMakePoint(centre.x - waist, centre.y - waist)];
    [star lineToPoint:NSMakePoint(centre.x - radius, centre.y)];
    [star lineToPoint:NSMakePoint(centre.x - waist, centre.y + waist)];
    [star closePath];
    [Gold() setFill];
    [star fill];
}

// Dark leather plate inside a double gilt border with corner ornaments.
void DrawBackground() {
    const NSRect plate = NSMakeRect(14.0, 14.0, 996.0, 996.0);
    NSBezierPath* panel = [NSBezierPath bezierPathWithRoundedRect:plate
                                                          xRadius:196.0
                                                          yRadius:196.0];

    // Radial, so the centre glows and the edges fall away. The vignette is
    // what makes a flat colour read as tooled leather rather than plastic.
    NSGradient* gradient =
        [[NSGradient alloc] initWithStartingColor:PlateCentre()
                                      endingColor:PlateEdge()];
    [gradient drawInBezierPath:panel
        relativeCenterPosition:NSMakePoint(0.0, 0.12)];

    // Outer band, with a darker line just inside it so the gilt has depth
    // instead of looking like a flat outline.
    [GoldShadow() setStroke];
    panel.lineWidth = 30.0;
    [panel stroke];
    [Gold() setStroke];
    panel.lineWidth = 20.0;
    [panel stroke];

    // Inner keyline, the second rule of the frame.
    NSBezierPath* keyline = [NSBezierPath
        bezierPathWithRoundedRect:NSMakeRect(62.0, 62.0, 900.0, 900.0)
                          xRadius:158.0
                          yRadius:158.0];
    keyline.lineWidth = 7.0;
    [GoldDeep() setStroke];
    [keyline stroke];

    const CGFloat inset = 150.0;
    for (const NSPoint corner : {
             NSMakePoint(inset, inset),
             NSMakePoint(1024.0 - inset, inset),
             NSMakePoint(inset, 1024.0 - inset),
             NSMakePoint(1024.0 - inset, 1024.0 - inset)}) {
        DrawFlourish(corner, 30.0);
    }
}

// The pitch gauge: a graduated arc, read like an astrolabe or a quadrant.
// The graduations are every 15 degrees across the mod's real travel, so the
// instrument is measuring something true rather than decorative.
void DrawPitchSweep(const NSPoint pivot) {
    constexpr CGFloat kSweepRadius = 322.0;

    // Graduations sit outside the band, pointing outward.
    // Stop well short of the upper limit: the arrowhead marks that end, and
    // graduations running into it read as a smudge rather than a scale.
    for (CGFloat degrees = kPitchMinimumDegrees;
         degrees <= kPitchMaximumDegrees - 25.0;
         degrees += 15.0) {
        const bool major = std::fabs(degrees - kPitchMinimumDegrees) < 0.01;
        NSBezierPath* tick = [NSBezierPath bezierPath];
        [tick moveToPoint:Polar(pivot, degrees, kSweepRadius + 30.0)];
        [tick lineToPoint:Polar(pivot, degrees,
                                kSweepRadius + (major ? 82.0 : 56.0))];
        tick.lineWidth = major ? 26.0 : 14.0;
        tick.lineCapStyle = NSLineCapStyleRound;
        [(major ? Gold() : GoldDeep()) setStroke];
        [tick stroke];
    }

    // The band itself, with a shadow pass beneath for engraved depth.
    NSBezierPath* sweep = [NSBezierPath bezierPath];
    [sweep appendBezierPathWithArcWithCenter:pivot
                                      radius:kSweepRadius
                                  startAngle:kPitchMinimumDegrees
                                    endAngle:kPitchMaximumDegrees];
    sweep.lineCapStyle = NSLineCapStyleRound;
    sweep.lineWidth = 46.0;
    [GoldShadow() setStroke];
    [sweep stroke];
    sweep.lineWidth = 32.0;
    [GoldDeep() setStroke];
    [sweep stroke];

    // Arrowhead at the upper limit: the direction the base game will not go.
    const NSPoint tip = Polar(pivot, kPitchMaximumDegrees, kSweepRadius + 104.0);
    const NSPoint base = Polar(pivot, kPitchMaximumDegrees, kSweepRadius - 14.0);
    const CGFloat spread = Radians(kPitchMaximumDegrees - 90.0);
    const CGFloat halfWidth = 68.0;

    NSBezierPath* head = [NSBezierPath bezierPath];
    [head moveToPoint:tip];
    [head lineToPoint:NSMakePoint(base.x + std::cos(spread) * halfWidth,
                                  base.y + std::sin(spread) * halfWidth)];
    [head lineToPoint:NSMakePoint(base.x - std::cos(spread) * halfWidth,
                                  base.y - std::sin(spread) * halfWidth)];
    [head closePath];
    [Gold() setFill];
    [head fill];
}

// A plain camera silhouette at the pivot of the gauge. Deliberately generic:
// the universal camera glyph, rimmed in gold to match the frame, not a
// depiction of any product or in-game object.
void DrawCamera(const NSPoint pivot) {
    // The hump goes down first and is then overdrawn by the body, so the
    // edge where the two meet is hidden rather than stroked across.
    NSBezierPath* hump = [NSBezierPath
        bezierPathWithRoundedRect:NSMakeRect(pivot.x - 88.0, pivot.y + 92.0,
                                             156.0, 84.0)
                          xRadius:24.0
                          yRadius:24.0];
    [Parchment() setFill];
    [hump fill];
    [GoldDeep() setStroke];
    hump.lineWidth = 12.0;
    [hump stroke];

    NSBezierPath* shell = [NSBezierPath
        bezierPathWithRoundedRect:NSMakeRect(pivot.x - 198.0, pivot.y - 138.0,
                                             396.0, 276.0)
                          xRadius:48.0
                          yRadius:48.0];
    [Parchment() setFill];
    [shell fill];
    [GoldDeep() setStroke];
    shell.lineWidth = 12.0;
    [shell stroke];

    NSBezierPath* lens = [NSBezierPath
        bezierPathWithOvalInRect:NSMakeRect(pivot.x - 108.0, pivot.y - 108.0,
                                            216.0, 216.0)];
    [LensDark() setFill];
    [lens fill];
    [Gold() setStroke];
    lens.lineWidth = 16.0;
    [lens stroke];

    NSBezierPath* iris = [NSBezierPath
        bezierPathWithOvalInRect:NSMakeRect(pivot.x - 58.0, pivot.y - 58.0,
                                            116.0, 116.0)];
    [GoldDeep() setFill];
    [iris fill];

    NSBezierPath* glint = [NSBezierPath
        bezierPathWithOvalInRect:NSMakeRect(pivot.x - 42.0, pivot.y + 4.0,
                                            40.0, 40.0)];
    [[NSColor colorWithSRGBRed:1.0 green:0.96 blue:0.86 alpha:0.75] setFill];
    [glint fill];
}

void DrawIcon() {
    // Sits slightly low so the sweep and arrowhead have room above it.
    const NSPoint pivot = NSMakePoint(512.0, 452.0);
    DrawBackground();
    DrawPitchSweep(pivot);
    DrawCamera(pivot);
}

bool WritePng(const NSString* directory, const char* name, const int pixels) {
    NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr
                      pixelsWide:pixels
                      pixelsHigh:pixels
                   bitsPerSample:8
                 samplesPerPixel:4
                        hasAlpha:YES
                        isPlanar:NO
                  colorSpaceName:NSCalibratedRGBColorSpace
                     bytesPerRow:0
                    bitsPerPixel:0];
    if (rep == nil) {
        return false;
    }

    NSGraphicsContext* context =
        [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
    if (context == nil) {
        return false;
    }

    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:context];
    context.imageInterpolation = NSImageInterpolationHigh;
    context.shouldAntialias = YES;

    // Render the vectors at this exact size rather than scaling a bitmap.
    NSAffineTransform* scale = [NSAffineTransform transform];
    const CGFloat factor = static_cast<CGFloat>(pixels) / kCanvas;
    [scale scaleBy:factor];
    [scale concat];

    DrawIcon();

    [NSGraphicsContext restoreGraphicsState];

    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                    properties:@{}];
    if (png == nil) {
        return false;
    }

    NSString* path = [directory stringByAppendingPathComponent:
                                    [NSString stringWithUTF8String:name]];
    return [png writeToFile:path atomically:YES];
}

struct IconVariant {
    const char* name;
    int pixels;
};

// The set macOS expects in an .iconset directory.
constexpr IconVariant kVariants[] = {
    {"icon_16x16.png", 16},     {"icon_16x16@2x.png", 32},
    {"icon_32x32.png", 32},     {"icon_32x32@2x.png", 64},
    {"icon_128x128.png", 128},  {"icon_128x128@2x.png", 256},
    {"icon_256x256.png", 256},  {"icon_256x256@2x.png", 512},
    {"icon_512x512.png", 512},  {"icon_512x512@2x.png", 1024},
};

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        if (argc != 2) {
            std::fprintf(stderr, "usage: make_app_icon <output.iconset>\n");
            return 2;
        }

        NSString* directory = [NSString stringWithUTF8String:argv[1]];
        NSError* error = nil;
        if (![NSFileManager.defaultManager
                createDirectoryAtPath:directory
                withIntermediateDirectories:YES
                                 attributes:nil
                                      error:&error]) {
            std::fprintf(stderr, "could not create %s: %s\n", argv[1],
                         error.localizedDescription.UTF8String);
            return 1;
        }

        for (const IconVariant& variant : kVariants) {
            if (!WritePng(directory, variant.name, variant.pixels)) {
                std::fprintf(stderr, "could not write %s\n", variant.name);
                return 1;
            }
        }

        std::fprintf(stdout, "wrote %zu images to %s\n",
                     sizeof(kVariants) / sizeof(kVariants[0]), argv[1]);
        return 0;
    }
}
