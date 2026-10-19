#include "options.h"

#include <SDL3/SDL_keyboard.h>

namespace osl {
namespace {

constexpr char kTimes[] = " \xC3\x97 ";

std::string Dimensions(int width, int height) {
    return std::to_string(width) + kTimes + std::to_string(height);
}

} // namespace

WindowSize WindowSizeOf(int height, const OslAspectRatio& aspect) {
    const long width = (static_cast<long>(height) * aspect.width * 2 + aspect.height) / (2 * aspect.height);
    return {static_cast<int>(width), height};
}

std::vector<WindowSize> WindowSizesFor(const OslAspectRatio& aspect) {
    std::vector<WindowSize> sizes;
    for (const int height : kWindowHeights) {
        sizes.push_back(WindowSizeOf(height, aspect));
    }
    return sizes;
}

const char* PageName(PageId page) {
    switch (page) {
    case PageId::kPlay: return "Play";
    case PageId::kDisplay: return "Display";
    case PageId::kGraphics: return "Graphics";
    case PageId::kGameplay: return "Gameplay";
    case PageId::kControls: return "Controls";
    case PageId::kTrophies: return "Trophies";
    case PageId::kAbout: return "About";
    }
    return "";
}

WindowSize FitGameWindow(WindowSize requested, const Display& display) {
    FitToDisplay(requested.width, requested.height, display);
    return requested;
}

bool WindowSizeFits(WindowSize size, const Display& display) {
    return FitGameWindow(size, display) == size;
}

std::string ResolutionLabel(const OslResolution& resolution) {
    return std::string(resolution.name) + " \xE2\x80\x94 " + Dimensions(resolution.width, resolution.height);
}

std::string WindowSizeLabel(WindowSize size) {
    std::string label = Dimensions(size.width, size.height);
    if (size == kGamePictureSize) {
        label += "   (the game's own size)";
    }
    return label;
}

std::string MonitorLabel(int monitor, const std::vector<Display>& displays) {
    const std::string number = std::to_string(monitor);
    if (monitor < 1 || monitor > static_cast<int>(displays.size())) {
        return "Display " + number + " (not connected)";
    }
    const Display& display = displays[static_cast<size_t>(monitor - 1)];
    std::string label = "Display " + number + (display.primary ? " (primary)" : "") + "   " +
                        Dimensions(display.bounds.w, display.bounds.h);
    if (!display.name.empty() && display.name != "Generic PnP Monitor") {
        label += "   " + display.name;
    }
    return label;
}

std::string KeyDisplayName(int scancode) {
    if (scancode == SDL_SCANCODE_RETURN) {
        return "Enter";
    }
    return osl_key_bindable(scancode) ? SDL_GetScancodeName(static_cast<SDL_Scancode>(scancode)) : std::string();
}

namespace {

std::string DefaultKeyName(OslKeyAction action) {
    char name[32];
    return osl_key_name(osl_key_actions[action].default_scancode, name, sizeof(name));
}

} // namespace

int SavedKey(const trg::SettingsStore& settings, OslKeyAction action) {
    const int saved = osl_key_from_name(settings.Get(osl_key_actions[action].ini_key).c_str());
    return saved ? saved : osl_key_actions[action].default_scancode;
}

void SetKey(trg::SettingsStore& settings, OslKeyAction action, int scancode) {
    char name[32];
    if (osl_key_name(scancode, name, sizeof(name))[0]) {
        settings.Set(osl_key_actions[action].ini_key, name);
    }
}

void ResetKeys(trg::SettingsStore& settings) {
    for (int action = 0; action < OSL_KEY_COUNT; ++action) {
        settings.Set(osl_key_actions[action].ini_key, DefaultKeyName(static_cast<OslKeyAction>(action)));
    }
}

} // namespace osl
