// What the Display page offers and how it labels it.
#pragma once

#include <string>
#include <vector>

#include "osl_displays.h"
#include "osl_keys.h"
#include "trg/settings.h"

namespace osl {

struct WindowSize {
    int width = 0;
    int height = 0;
    bool operator==(const WindowSize&) const = default;
};

// The game's own picture is 1920 x 1080; window sizes keep 16:9.
inline constexpr int kWindowHeights[] = {720, 900, 1080, 1440, 2160};
inline constexpr WindowSize kGamePictureSize{1920, 1080};

WindowSize WindowSizeOf(int height);
std::vector<WindowSize> WindowSizes();

enum class PageId { kPlay, kDisplay, kGraphics, kControls, kAbout };

inline constexpr PageId kPageOrder[] = {PageId::kPlay, PageId::kDisplay, PageId::kGraphics, PageId::kControls,
                                        PageId::kAbout};

const char* PageName(PageId page);

// The window a requested size becomes on this display: unchanged when it fits, else shrunk to fit, keeping its shape.
WindowSize FitGameWindow(WindowSize requested, const Display& display);

// Whether the size fits the display, so the game window is not larger than the screen.
bool WindowSizeFits(WindowSize size, const Display& display);

// "1920 × 1080", with a note on the game's own picture size.
std::string WindowSizeLabel(WindowSize size);

// "Display 2 (primary)   2560 × 1440   Name". Monitor settings are 1-based.
std::string MonitorLabel(int monitor, const std::vector<Display>& displays);

// The Controls page's bindings, stored one key name per action in the settings (osl_keys.h).
// "Space", "Left Shift", "Enter", "F10".
std::string KeyDisplayName(int scancode);
// The saved key of an action; the default when the setting is missing or names no usable key.
int SavedKey(const trg::SettingsStore& settings, OslKeyAction action);
void SetKey(trg::SettingsStore& settings, OslKeyAction action, int scancode);
// Puts every keyboard control and port hotkey back to its default and nothing else.
void ResetKeys(trg::SettingsStore& settings);

} // namespace osl
