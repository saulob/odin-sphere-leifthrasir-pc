// The displays as the Monitor setting numbers them (1-based): the primary display first, then SDL's order.
// The launcher's Monitor row and the game window both use this list.
#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <vector>

namespace osl {

struct Display {
    SDL_DisplayID id = 0;
    std::string name;
    SDL_Rect bounds{};
    SDL_Rect usable{};
    float scale = 1.0f;
    bool primary = false;
};

// Needs the SDL video subsystem.
inline std::vector<Display> ListDisplays() {
    std::vector<Display> displays;
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    for (int i = 0; ids && i < count; ++i) {
        Display display;
        display.id = ids[i];
        const char* name = SDL_GetDisplayName(ids[i]);
        display.name = name ? name : "";
        SDL_GetDisplayBounds(ids[i], &display.bounds);
        if (!SDL_GetDisplayUsableBounds(ids[i], &display.usable)) {
            display.usable = display.bounds;
        }
        const float scale = SDL_GetDisplayContentScale(ids[i]);
        display.scale = scale > 0.0f ? scale : 1.0f;
        display.primary = ids[i] == primary;
        displays.push_back(std::move(display));
    }
    SDL_free(ids);
    std::stable_partition(displays.begin(), displays.end(), [](const Display& d) { return d.primary; });
    return displays;
}

// Settings 0 and out-of-range values name the primary display.
inline const Display* DisplayForSetting(const std::vector<Display>& displays, int monitor) {
    if (displays.empty()) {
        return nullptr;
    }
    if (monitor >= 1 && monitor <= static_cast<int>(displays.size())) {
        return &displays[static_cast<size_t>(monitor - 1)];
    }
    return &displays.front();
}

// The launcher window stays within this share of the display's usable area, in pixels.
inline constexpr double kWindowDisplayShare = 0.9;

inline void FitWindowSize(int& width, int& height, const Display& display) {
    if (display.usable.w <= 0 || display.usable.h <= 0) {
        return;
    }
    width = std::min(width, static_cast<int>(display.usable.w * kWindowDisplayShare));
    height = std::min(height, static_cast<int>(display.usable.h * kWindowDisplayShare));
}

// Room the title bar and frame take at 100% scaling; it grows with the display scale.
inline constexpr int kWindowDecorationHeight = 40;

// A window of the requested size, shrunk to the display's usable area (title bar included) keeping its shape.
inline void FitToDisplay(int& width, int& height, const Display& display) {
    if (display.usable.w <= 0 || display.usable.h <= 0 || width <= 0 || height <= 0) {
        return;
    }
    const int max_width = display.usable.w;
    const int max_height = std::max(1, display.usable.h - static_cast<int>(kWindowDecorationHeight * display.scale + 0.5f));
    if (width <= max_width && height <= max_height) {
        return;
    }
    const double shrink = std::min(static_cast<double>(max_width) / width, static_cast<double>(max_height) / height);
    width = std::max(1, static_cast<int>(width * shrink));
    height = std::max(1, static_cast<int>(height * shrink));
}

} // namespace osl
