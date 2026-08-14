// Process entry point.
//
// Two modes:
//   (no arguments)  run as the input method server, which is how the system
//                   launches us out of ~/Library/Input Methods.
//   --install       register the bundle as an input source and enable it,
//                   so installing does not require a log out / log in.
//                   scripts/install.sh calls this on the *installed* copy.
//
// There is no nib.  The Xcode input-method template keeps its menu in
// MainMenu.nib, but ibtool ships with Xcode and this project is built with
// Command Line Tools only — so the menu is built in code, in
// -[ArtInputController menu].

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <InputMethodKit/InputMethodKit.h>

#import "ArtBridge.h"

static int ArtRegisterInputSource(void) {
    NSBundle *bundle = [NSBundle mainBundle];
    NSURL *url = bundle.bundleURL;

    OSStatus status = TISRegisterInputSource((__bridge CFURLRef)url);
    if (status != noErr) {
        // Already registered is the common case and is not an error worth
        // failing on; the enable pass below is what actually matters.
        fprintf(stderr, "TISRegisterInputSource(%s) returned %d\n",
                url.path.UTF8String, (int)status);
    }

    NSDictionary *filter =
        @{(__bridge NSString *)kTISPropertyBundleID : bundle.bundleIdentifier};
    CFArrayRef sources =
        TISCreateInputSourceList((__bridge CFDictionaryRef)filter, true);
    if (sources == NULL) {
        fprintf(stderr,
                "no input source found for %s — add it by hand in System "
                "Settings > Keyboard > Input Sources\n",
                bundle.bundleIdentifier.UTF8String);
        return 1;
    }

    CFIndex count = CFArrayGetCount(sources);
    for (CFIndex i = 0; i < count; ++i) {
        TISInputSourceRef source =
            (TISInputSourceRef)CFArrayGetValueAtIndex(sources, i);
        TISEnableInputSource(source);
    }
    CFRelease(sources);
    printf("registered and enabled %ld input source(s) for %s\n", (long)count,
           bundle.bundleIdentifier.UTF8String);
    return 0;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        if (argc > 1 && strcmp(argv[1], "--install") == 0) {
            return ArtRegisterInputSource();
        }

        NSBundle *bundle = [NSBundle mainBundle];
        NSString *connectionName =
            [bundle objectForInfoDictionaryKey:@"InputMethodConnectionName"];
        if (connectionName.length == 0) {
            ArtLogAlways(@"Info.plist has no InputMethodConnectionName; "
                         @"cannot start");
            return 1;
        }

        IMKServer *server =
            [[IMKServer alloc] initWithName:connectionName
                           bundleIdentifier:bundle.bundleIdentifier];
        if (server == nil) {
            ArtLogAlways(@"IMKServer '%@' would not start", connectionName);
            return 1;
        }

        // Map the language model now rather than during the first keystroke,
        // and get any load failure into Console.app immediately.
        (void)[ArtBridge shared];

        [[NSApplication sharedApplication] run];
    }
    return 0;
}
