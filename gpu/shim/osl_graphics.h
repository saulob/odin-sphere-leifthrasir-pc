// SPDX-License-Identifier: GPL-2.0-or-later
// Graphics options as osl.ini stores them, shared by the launcher, the window and the renderer.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace OslGraphics {

enum DisplayMode : int { Windowed = 0, Borderless = 1, DisplayModeCount };
inline constexpr std::array<std::string_view, DisplayModeCount> DisplayModeKeys = {"windowed", "borderless"};
inline constexpr std::array<const char*, DisplayModeCount> DisplayModeNames = {"Windowed", "Borderless Fullscreen"};

template <typename T, std::size_t N>
constexpr int IndexOf(const std::array<T, N>& values, const T& value, int fallback) {
    for (std::size_t i = 0; i < N; ++i) {
        if (values[i] == value) return int(i);
    }
    return fallback;
}

inline int ParseDisplayMode(std::string_view text) {
    return IndexOf(DisplayModeKeys, text, Windowed);
}

/// Letterbox keeps the frame's aspect ratio in the window; Stretch fills the window with it.
enum Presentation : int { Letterbox = 0, Stretch = 1, PresentationCount };
inline constexpr std::array<std::string_view, PresentationCount> PresentationKeys = {"letterbox", "stretch"};

inline int ParsePresentation(std::string_view text) {
    return IndexOf(PresentationKeys, text, Letterbox);
}
/// RCAS strength in percent; invalid values fall back to 75.
inline int ParseRcasStrength(int value) {
    return value >= 0 && value <= 100 ? value : 75;
}
/// Forced anisotropic filtering: 0 is the game's own, else 2, 4, 8 or 16 times.
inline constexpr std::array<int, 5> Anisotropy = {0, 2, 4, 8, 16};
inline int ParseAnisotropy(int value) {
    return IndexOf(Anisotropy, value, -1) >= 0 ? value : 0;
}
/// The Graphics card list: `names` (one per Vulkan device, empty for a device the renderer cannot use) taken in
/// `order`, best device first, each name once.
inline std::vector<std::string> ListGpus(const std::vector<std::string>& names, const std::vector<std::size_t>& order) {
    std::vector<std::string> listed;
    for (const std::size_t device : order) {
        if (device < names.size() && !names[device].empty() &&
            std::find(listed.begin(), listed.end(), names[device]) == listed.end()) {
            listed.push_back(names[device]);
        }
    }
    return listed;
}
/// The listed card named `request`, else the first listed card (no choice, the retired "automatic" or a card that is
/// no longer there). Empty only when nothing is listed.
inline std::string ResolveGpu(const std::vector<std::string>& listed, std::string_view request) {
    if (listed.empty()) return {};
    const auto found = std::find(listed.begin(), listed.end(), request);
    return found != listed.end() ? *found : listed.front();
}
/// The Vulkan device a listed card runs on: the first in `order` with that name; -1 for none.
inline int GpuDevice(const std::vector<std::string>& names, const std::vector<std::size_t>& order,
                     std::string_view card) {
    for (const std::size_t device : order) {
        if (device < names.size() && !card.empty() && names[device] == card) return int(device);
    }
    return -1;
}
/// FSR RCAS attenuation for a strength: 0 is the sharpest, 2 the softest.
inline float RcasAttenuation(int strength) {
    return 2.0f * (1.0f - float(ParseRcasStrength(strength)) / 100.0f);
}
/// Whether the FSR pass runs: FSR 1 when the window is larger than the game's image, RCAS
/// alone when they are the same size. A smaller window is left to the regular scaling.
inline bool FsrPassRuns(bool fsr1, bool rcas, int in_w, int in_h, int out_w, int out_h) {
    const bool upscale = in_w < out_w && in_h < out_h;
    const bool same = in_w == out_w && in_h == out_h;
    return (fsr1 && upscale) || (rcas && same);
}

} // namespace OslGraphics
