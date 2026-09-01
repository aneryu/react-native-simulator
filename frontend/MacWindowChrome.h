#pragma once

struct SDL_Window;

struct SimulatorWindowChrome {
  // Left padding for the imgui toolbar so controls clear the traffic lights
  // when the content view draws under the native title bar.
  float leadingInset{12.0f};
  float toolbarHeight{0.0f};
  bool overlaysTitlebar{false};
};

// Dark Aqua chrome. Hides the native window title so the imgui toolbar is the
// only heading. When the SDL view fills the window frame, the toolbar shares
// the title-bar row with the traffic lights.
SimulatorWindowChrome applySimulatorWindowChrome(SDL_Window* window);

// Dock + window icon for the interactive host and Inspector. Safe to call
// with a null window to set only the application Dock image.
void applySimulatorAppIcon(SDL_Window* window);
