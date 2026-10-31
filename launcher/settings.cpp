#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <fstream>
#include <initializer_list>
#include <system_error>
#include <utility>
#include <vector>

#include "osl_keys.h"
#include "osl_launcher.h"
#include "osl_resolution.h"
#include "game_files.h"
#include "utf8.h"

namespace osl {
namespace {

constexpr ManagedSetting kBaseSettings[] = {
    {kShowLauncherKey, "1"},
    {kDisplayModeKey, "windowed"},
    {kWindowWidthKey, "1920"},
    {kWindowHeightKey, "1080"},
    {kMonitorKey, "1"},
    {kVSyncKey, "1"},
    {kPresentationKey, kDefaultPresentation},
    {kUpscaleKey, "0"},
    {kSharpenKey, "1"},
    {kSharpenStrengthKey, "75"},
    {kRenderResolutionKey, kNativeResolution},
    {kAspectRatioKey, kDefaultAspectRatio},
    {kGraphicsCardKey, ""},
    {kAntiAliasingKey, "0"},
    {kAnisotropyKey, "0"},
    {kTextLanguageKey, "english"},
    {kSkipIntroKey, "0"},
    {kUnlockBookshelfKey, "0"},
    {kShowFpsTitleKey, "0"},
    {kShowResolutionTitleKey, "0"},
    {kShowFrameCounterKey, "0"},
    {kDetailedLoggingKey, "1"},
    {kControllerPromptsKey, "playstation"},
    {kLauncherXKey, ""},
    {kLauncherYKey, ""},
    {kLauncherWidthKey, ""},
    {kLauncherHeightKey, ""},
    {kLauncherMaximizedKey, "0"},
    {kLauncherFullscreenKey, "0"},
};

constexpr std::string_view kPlacementKeys[] = {kLauncherXKey,         kLauncherYKey,         kLauncherWidthKey,
                                                kLauncherHeightKey,    kLauncherMaximizedKey, kLauncherFullscreenKey};

constexpr char kFileHeader[] =
    "# Odin Sphere Leifthrasir user settings (the launcher and the game write this file).\n"
    "# Remove a line to use the default.\n";

} // namespace

std::span<const ManagedSetting> ManagedSettings() {
    static const std::vector<ManagedSetting> settings = [] {
        static std::deque<std::string> default_names;
        std::vector<ManagedSetting> all(std::begin(kBaseSettings), std::end(kBaseSettings));
        for (const OslKeyInfo& action : osl_key_actions) {
            char name[32];
            default_names.emplace_back(osl_key_name(action.default_scancode, name, sizeof(name)));
            all.push_back({action.ini_key, default_names.back()});
        }
        return all;
    }();
    return settings;
}

bool IsValidSetting(std::string_view key, std::string_view value) {
    const auto one_of = [&](std::initializer_list<std::string_view> values) {
        return std::find(values.begin(), values.end(), value) != values.end();
    };
    if (std::any_of(std::begin(osl_key_actions), std::end(osl_key_actions),
                    [&](const OslKeyInfo& action) { return key == action.ini_key; })) {
        return osl_key_from_name(std::string(value).c_str()) != 0;
    }
    if (key == kDisplayModeKey) {
        return one_of({"windowed", "borderless"});
    }
    if (key == kAnisotropyKey) {
        return one_of({"0", "2", "4", "8", "16"});
    }
    if (key == kControllerPromptsKey) {
        return one_of({"playstation", "xbox"});
    }
    if (key == kPresentationKey) {
        return std::any_of(std::begin(kPresentationChoices), std::end(kPresentationChoices),
                           [&](const PresentationChoice& choice) { return value == choice.key; });
    }
    if (key == kRenderResolutionKey) {
        return std::any_of(std::begin(osl_resolutions), std::end(osl_resolutions),
                           [&](const OslResolution& resolution) { return value == resolution.key; });
    }
    if (key == kAspectRatioKey) {
        return std::any_of(std::begin(osl_aspect_ratios), std::end(osl_aspect_ratios),
                           [&](const OslAspectRatio& aspect) { return value == aspect.key; });
    }
    if (key == kTextLanguageKey) {
        const std::span<const GameBuild> builds = SupportedBuilds();
        return std::any_of(builds.begin(), builds.end(), [&](const GameBuild& build) {
            return std::any_of(build.text_languages.begin(), build.text_languages.end(),
                               [&](const TextLanguage& language) { return value == language.key; });
        });
    }
    return true;
}

bool IsLauncherPlacementSetting(std::string_view key) {
    for (const std::string_view placement_key : kPlacementKeys) {
        if (placement_key == key) {
            return true;
        }
    }
    return false;
}

LauncherSettings::LauncherSettings(std::filesystem::path file) : file_(std::move(file)) {
    true_value = "1";
    false_value = "0";
    const trg::TextFileSettings stored(Utf8(file_));
    for (const ManagedSetting& setting : ManagedSettings()) {
        if (stored.Has(setting.key)) {
            saved_[std::string(setting.key)] = stored.Get(setting.key);
        }
    }
    pending_ = saved_;
}

std::string LauncherSettings::Get(std::string_view key, std::string_view def) const {
    const auto it = pending_.find(key);
    if (it == pending_.end() || it->second.empty() || !IsValidSetting(key, it->second)) {
        return std::string(def);
    }
    return it->second;
}

void LauncherSettings::Set(std::string_view key, std::string value) {
    pending_[std::string(key)] = std::move(value);
}

bool LauncherSettings::Save() {
    Values changes;
    for (const auto& [key, value] : pending_) {
        const auto saved = saved_.find(key);
        if (saved == saved_.end() || saved->second != value) {
            changes.emplace(key, value);
        }
    }
    if (changes.empty()) {
        return true;
    }
    if (!WriteFile(changes)) {
        return false;
    }
    for (const auto& [key, value] : changes) {
        saved_[key] = value;
    }
    return true;
}

bool LauncherSettings::SaveNow(const Values& values) {
    if (!WriteFile(values)) {
        return false;
    }
    for (const auto& [key, value] : values) {
        saved_[key] = value;
        pending_[key] = value;
    }
    return true;
}

void LauncherSettings::ResetToDefaults() {
    for (const ManagedSetting& setting : ManagedSettings()) {
        // The prompts follow the game files, which change (and save the setting) when the choice is made.
        if (!IsLauncherPlacementSetting(setting.key) && setting.key != kControllerPromptsKey) {
            pending_[std::string(setting.key)] = std::string(setting.default_value);
        }
    }
}

bool LauncherSettings::WriteFile(const Values& changes) {
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (!std::filesystem::exists(file_, ec)) {
        std::ofstream(file_) << kFileHeader;
    }
    trg::TextFileSettings file(Utf8(file_));
    for (const auto& [key, value] : changes) {
        file.Set(key, value);
    }
    return file.Save();
}

bool LauncherWanted(bool game_ready, bool show_setting, bool shift_held, bool skip_once) {
    if (!game_ready) {
        return true;
    }
    return !skip_once && (shift_held || show_setting);
}

} // namespace osl

extern "C" int osl_render_resolution(const char* settings_file, const char* image_dir) {
    using namespace osl;
    if (RestrictedToNativeResolution(BuildOfImageDir(image_dir))) {
        return 0;
    }
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return osl_resolution_index(settings.Get(kRenderResolutionKey, kNativeResolution).c_str());
}

extern "C" int osl_aspect_ratio(const char* settings_file, const char* image_dir) {
    using namespace osl;
    if (RestrictedToNativeResolution(BuildOfImageDir(image_dir))) {
        return 0;
    }
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return osl_aspect_index(settings.Get(kAspectRatioKey, kDefaultAspectRatio).c_str());
}

extern "C" int osl_detailed_logging(const char* settings_file) {
    using namespace osl;
    if (!settings_file || !*settings_file) {
        return 1;
    }
    const LauncherSettings settings(PathFromUtf8(settings_file));
    return settings.GetBool(kDetailedLoggingKey, true) ? 1 : 0;
}

extern "C" int osl_text_language(const char* settings_file, const char* image_dir) {
    using namespace osl;
    const GameBuild* build = BuildOfImageDir(image_dir);
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return ResolveTextLanguage(build, settings.Get(kTextLanguageKey)).ps4_id;
}

extern "C" void osl_log_gameplay_settings(const char* settings_file, const char* image_dir) {
    using namespace osl;
    const GameBuild* build = BuildOfImageDir(image_dir);
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    const TextLanguage& language = ResolveTextLanguage(build, settings.Get(kTextLanguageKey));
    std::printf("Gameplay: Menu & Subtitles %.*s\n", static_cast<int>(language.name.size()), language.name.data());
    std::printf("Gameplay: Skip intro %s\n", settings.GetBool(kSkipIntroKey, false) ? "on" : "off");
    std::printf("Gameplay: Unlock Bookshelf %s\n", settings.GetBool(kUnlockBookshelfKey, false) ? "on" : "off");
}

extern "C" int osl_skip_intro(const char* settings_file) {
    using namespace osl;
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return settings.GetBool(kSkipIntroKey, false) ? 1 : 0;
}

extern "C" int osl_unlock_bookshelf(const char* settings_file) {
    using namespace osl;
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    return settings.GetBool(kUnlockBookshelfKey, false) ? 1 : 0;
}
