// See ArtNavigation.h.

#import "ArtNavigation.h"

#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>  // kVK_* virtual key codes

#import "ArtBridge.h"  // ArtLogAlways

// Set once the "not trusted" complaint has been made, cleared as soon as a
// key is posted successfully, so a grant that lapses later complains again.
static BOOL sWarnedUntrusted = NO;

@implementation ArtNavigation

+ (BOOL)isTrusted {
    return AXIsProcessTrusted() ? YES : NO;
}

+ (void)promptForTrustIfNeeded {
    static BOOL prompted = NO;
    if (AXIsProcessTrusted()) {
        return;
    }
    if (prompted) {
        return;
    }
    prompted = YES;
    NSDictionary *options =
        @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
    AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

+ (void)openAccessibilitySettings {
    NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple."
                                      @"preference.security?Privacy_"
                                      @"Accessibility"];
    [[NSWorkspace sharedWorkspace] openURL:url];
}

+ (BOOL)inject:(ArtNavigationKey)key shiftHeld:(BOOL)shiftHeld {
    if (!AXIsProcessTrusted()) {
        // Always logged, debug flag or not. This is the most confusing
        // failure in the project: the entry in System Settings can be present
        // AND ticked and still not apply, because an ad-hoc signature is
        // identified by its code directory hash — which changes on every
        // rebuild — so the tick belongs to an earlier build while the list
        // goes on showing it. Say that here rather than leave ten dead keys.
        if (!sWarnedUntrusted) {
            sWarnedUntrusted = YES;
            ArtLogAlways(@"the idle digit-row editing layer is off: this build "
                         @"is not trusted for Accessibility. An entry already "
                         @"ticked in System Settings > Privacy & Security > "
                         @"Accessibility may belong to a previous build — "
                         @"remove it with the − button and grant it again, or "
                         @"run: tccutil reset Accessibility %@",
                         [[NSBundle mainBundle] bundleIdentifier] ?: @"");
        }
        [self promptForTrustIfNeeded];
        return NO;
    }
    sWarnedUntrusted = NO;

    CGKeyCode code = 0;
    CGEventFlags flags = 0;
    switch (key) {
        case ArtNavigationKeyLineStart:
            code = (CGKeyCode)kVK_LeftArrow;
            flags |= kCGEventFlagMaskCommand;
            break;
        case ArtNavigationKeyLineEnd:
            code = (CGKeyCode)kVK_RightArrow;
            flags |= kCGEventFlagMaskCommand;
            break;
        case ArtNavigationKeySelectToLineStart:
            code = (CGKeyCode)kVK_LeftArrow;
            flags |= kCGEventFlagMaskCommand | kCGEventFlagMaskShift;
            break;
        case ArtNavigationKeySelectToLineEnd:
            code = (CGKeyCode)kVK_RightArrow;
            flags |= kCGEventFlagMaskCommand | kCGEventFlagMaskShift;
            break;
        case ArtNavigationKeyForwardDelete:
            code = (CGKeyCode)kVK_ForwardDelete;
            break;
        case ArtNavigationKeyBackspace:
            code = (CGKeyCode)kVK_Delete;
            break;
        case ArtNavigationKeyUp:
            code = (CGKeyCode)kVK_UpArrow;
            break;
        case ArtNavigationKeyDown:
            code = (CGKeyCode)kVK_DownArrow;
            break;
        case ArtNavigationKeyLeft:
            code = (CGKeyCode)kVK_LeftArrow;
            break;
        case ArtNavigationKeyRight:
            code = (CGKeyCode)kVK_RightArrow;
            break;
    }
    if (shiftHeld) {
        flags |= kCGEventFlagMaskShift;
    }

    CGEventSourceRef source =
        CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    CGEventRef down = CGEventCreateKeyboardEvent(source, code, true);
    CGEventRef up = CGEventCreateKeyboardEvent(source, code, false);
    if (down == NULL || up == NULL) {
        if (down) CFRelease(down);
        if (up) CFRelease(up);
        if (source) CFRelease(source);
        return NO;
    }
    // Set the flags explicitly on both halves: the synthetic event must not
    // inherit whatever the physical modifiers happen to be at post time.
    CGEventSetFlags(down, flags);
    CGEventSetFlags(up, flags);

    // Posted at the HID level, i.e. as if typed. It therefore re-enters this
    // input method first; arrows are passed straight through while idle, so
    // that round trip is a no-op. Same shape as the Windows SendInput path.
    CGEventPost(kCGHIDEventTap, down);
    CGEventPost(kCGHIDEventTap, up);

    CFRelease(down);
    CFRelease(up);
    if (source) {
        CFRelease(source);
    }
    return YES;
}

@end
