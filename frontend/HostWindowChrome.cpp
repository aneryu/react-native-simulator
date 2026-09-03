#include "MacWindowChrome.h"

#include "AppIconPng.h"

#include <SDL3/SDL.h>

void applySimulatorAppIcon(SDL_Window* window) {
  if (window == nullptr) {
    return;
  }
  SDL_IOStream* stream = SDL_IOFromConstMem(kRnsimAppIconPng, kRnsimAppIconPng_len);
  if (stream == nullptr) {
    return;
  }
  SDL_Surface* surface = SDL_LoadPNG_IO(stream, true);
  if (surface == nullptr) {
    return;
  }
  SDL_SetWindowIcon(window, surface);
  SDL_DestroySurface(surface);
}

SimulatorWindowChrome applySimulatorWindowChrome(SDL_Window* window) {
  SimulatorWindowChrome chrome;
  applySimulatorAppIcon(window);
  chrome.leadingInset = 12.0f;
  chrome.toolbarHeight = 0.0f;
  chrome.overlaysTitlebar = false;
  return chrome;
}
