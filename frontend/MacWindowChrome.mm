#include "MacWindowChrome.h"

#include "AppIconPng.h"

#import <Cocoa/Cocoa.h>

#include <SDL3/SDL.h>

#include <cmath>

void applySimulatorAppIcon(SDL_Window* /*window*/) {
  @autoreleasepool {
    NSData* data = [NSData dataWithBytes:kRnsimAppIconPng
                                  length:kRnsimAppIconPng_len];
    NSImage* image = [[NSImage alloc] initWithData:data];
    if (image == nil) {
      return;
    }
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    // CLI processes are not masked by Dock. The PNG already has a squircle
    // alpha; draw it as-is. Do not use dockTile.contentView (widget shadow)
    // or SDL_SetWindowIcon (same API, easy to feed an opaque square).
    [app setApplicationIconImage:image];
  }
}

SimulatorWindowChrome applySimulatorWindowChrome(SDL_Window* window) {
  SimulatorWindowChrome chrome;
  applySimulatorAppIcon(window);
  if (window == nullptr) {
    return chrome;
  }
  NSWindow* nsWindow = (__bridge NSWindow*)SDL_GetPointerProperty(
      SDL_GetWindowProperties(window),
      SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
      nullptr);
  if (nsWindow == nil) {
    return chrome;
  }
  NSAppearance* dark =
      [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  nsWindow.appearance = dark;
  NSApp.appearance = dark;
  nsWindow.titlebarAppearsTransparent = YES;
  nsWindow.titleVisibility = NSWindowTitleHidden;
  nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
  if ([nsWindow respondsToSelector:@selector(setTitlebarSeparatorStyle:)]) {
    nsWindow.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
  }
  nsWindow.backgroundColor =
      [NSColor colorWithCalibratedRed:7.0 / 255.0
                               green:8.0 / 255.0
                                blue:10.0 / 255.0
                               alpha:1.0];

  const CGFloat titlebarHeight =
      NSHeight(nsWindow.frame) - NSHeight(nsWindow.contentLayoutRect);
  if (titlebarHeight > 1.0) {
    chrome.toolbarHeight = static_cast<float>(titlebarHeight);
  }

  NSButton* zoomButton =
      [nsWindow standardWindowButton:NSWindowZoomButton];
  NSView* contentView = nsWindow.contentView;
  if (zoomButton != nil && contentView != nil) {
    const NSRect zoomInContent =
        [zoomButton.superview convertRect:zoomButton.frame toView:contentView];
    if (std::isfinite(NSMaxX(zoomInContent)) && NSMaxX(zoomInContent) > 0) {
      chrome.leadingInset = static_cast<float>(NSMaxX(zoomInContent) + 16.0);
    }
  }

  const CGFloat viewHeight = NSHeight(contentView.bounds);
  const CGFloat frameHeight = NSHeight(nsWindow.frame);
  chrome.overlaysTitlebar = std::abs(viewHeight - frameHeight) < 2.0;
  if (!chrome.overlaysTitlebar) {
    chrome.leadingInset = 12.0f;
  }
  return chrome;
}
