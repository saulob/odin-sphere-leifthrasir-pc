#include "osl_launcher.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cfloat>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <imgui.h>

#include "trg/headers.h"
#include "trg/launcher.h"
#include "trg/platform.h"

#include "osl_accent.h"
#include "osl_displays.h"
#include "osl_resolution.h"
#include "game_files.h"
#include "game_install.h"
#include "host.h"
#include "launcher_quit.h"
#include "options.h"
#include "package_browser.h"
#include "placement.h"
#include "play_gate.h"
#include "problem_dialog.h"
#include "settings.h"
#include "shift_key.h"
#include "trophy_page.h"
#include "utf8.h"

#ifndef OSL_PORT_VERSION
#define OSL_PORT_VERSION "unknown"
#endif
#define OSL_TRG_LAUNCHER_VERSION "1.1.0"

// Dear ImGui's SDL3 backend; it is not declared in its header.
ImGuiKey ImGui_ImplSDL3_KeyEventToImGuiKey(SDL_Keycode keycode, SDL_Scancode scancode);

namespace osl {
namespace {

ImVec4 PaletteColor(int red, int green, int blue, float alpha = 1.0f) {
    return ImVec4(red / 255.0f, green / 255.0f, blue / 255.0f, alpha);
}

const ImVec4 kAccent = PaletteColor(kAccentRed, kAccentGreen, kAccentBlue);
const ImVec4 kAccentHot = PaletteColor(kAccentHotRed, kAccentHotGreen, kAccentHotBlue);
const ImVec4 kAccentPressed = PaletteColor(kAccentPressedRed, kAccentPressedGreen, kAccentPressedBlue);
const ImVec4 kAccentSubtle = PaletteColor(kAccentSubtleRed, kAccentSubtleGreen, kAccentSubtleBlue);
constexpr double kPlayFeedbackSeconds = 0.4;
constexpr char kPortTitle[] = "Odin Sphere Leifthrasir PC";
const std::string kPortUrl = std::string("https://") + kPortRepository;

ImGuiKey ImGuiKeyOf(int scancode) {
    const SDL_Keycode keycode = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(scancode), SDL_KMOD_NONE, false);
    return ImGui_ImplSDL3_KeyEventToImGuiKey(keycode, static_cast<SDL_Scancode>(scancode));
}

// The physical key behind a key ImGui reported; 0 when none is bindable.
int ScancodeOf(ImGuiKey key) {
    for (int scancode = 1; scancode < SDL_SCANCODE_COUNT; ++scancode) {
        if (osl_key_bindable(scancode) && ImGuiKeyOf(scancode) == key) {
            return scancode;
        }
    }
    return 0;
}

std::string GameFilesHelp() {
    return "Select your own legally obtained Odin Sphere Leifthrasir package (.pkg). Supported: " + SupportedBuildsText() +
           ". The game is unpacked into the game folder next to the program (about 4 GB). No game files are included. "
           "Reinstalling keeps your saves and settings.";
}
#ifdef _WIN32
constexpr char kShowLauncherHelp[] = "Off starts the game directly. Hold Shift while starting to bring it back.";
#else
constexpr char kShowLauncherHelp[] =
    "Off starts the game directly. Hold Shift while starting, or start the program with --launcher, to bring it back.";
#endif
constexpr char kWindowSizeHelp[] =
    "The game window's size in pixels in Windowed mode. The picture keeps its shape: other window shapes get black bars.";
constexpr char kGraphicsCardHelp[] =
    "The graphics card the game runs on. Automatic prefers a discrete card. A card that is no longer there falls back "
    "to Automatic.";
constexpr char kMonitorHelp[] = "The display the game opens on.";
constexpr char kVSyncHelp[] = "On prevents tearing. Off allows it, for the lowest latency and G-Sync/FreeSync.";
constexpr char kPresentationHelp[] =
    "Controls how the rendered image fits a window or display with a different aspect ratio.";
constexpr char kUpscaleHelp[] =
    "Enlarges the game's picture to a bigger window with AMD FidelityFX Super Resolution 1. It has no effect when the "
    "window is the size of the render resolution or smaller.";
constexpr char kSharpenHelp[] =
    "Sharpens the final picture (AMD RCAS). It applies when the window is the size of the render resolution, or "
    "together with FSR 1 in a bigger window.";
constexpr char kAntiAliasingHelp[] =
    "SMAA smooths jagged edges on the finished picture, at a small cost in performance.";
constexpr char kAnisotropyHelp[] =
    "Sharper textures on floors and walls seen at an angle. Game uses the game's own filtering.";
constexpr char kAspectRatioHelp[] = "Sets the game's target rendering shape and the Window size choices.";
constexpr char kRestrictedAspectRatioHelp[] =
    "This game build renders at 16:9; the other aspect ratios are not available for it.";
constexpr char kRenderResolutionHelp[] =
    "The resolution the game renders at. It is separate from the window size: the picture is scaled to the window. "
    "1440p and above need a powerful graphics card.";
constexpr char kRestrictedRenderResolutionHelp[] =
    "This game build renders at its original size; the custom render resolutions are not available for it.";
constexpr char kShowFpsTitleHelp[] = "Shows the frame rate in the window title: Odin Sphere Leifthrasir | FPS: 60.00";
constexpr char kShowResolutionTitleHelp[] =
    "Shows the game's render resolution in the window title: Odin Sphere Leifthrasir | 1920x1080";
constexpr char kFrameCounterHelp[] = "Shows the frame rate and frame time in a corner of the game.";
constexpr char kMenuKeyHelp[] = "Opens the port's in-game menu, with the display and graphics options.";

std::vector<std::string> ListGpuNames() {
    char names[16][256] = {};
    const int count = osl_gpu_names(names, 16);
    return {names, names + count};
}

trg::Theme LauncherTheme() {
    trg::Theme theme = trg::Theme::Midnight().WithAccent(kAccent);
    theme.accent_hot = kAccentHot;
    theme.on_accent = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    theme.frame_active = kAccentPressed;
    theme.popup = ImVec4(kAccentSubtle.x, kAccentSubtle.y, kAccentSubtle.z, 0.99f);
    // Warnings stay red-orange so they are never mistaken for the ruby accent.
    theme.warn = ImVec4(0.47f, 0.18f, 0.10f, 1.0f);
    theme.warn_dot = ImVec4(1.0f, 0.45f, 0.30f, 1.0f);
    return theme;
}

trg::HeaderPainter LauncherHeader() {
    return trg::headers::Gradient(IM_COL32(10, 12, 24, 255),
                                  IM_COL32(kAccentSubtleRed, kAccentSubtleGreen, kAccentSubtleBlue, 255),
                                  IM_COL32(kAccentPressedRed / 2, kAccentPressedGreen / 2, kAccentPressedBlue / 2, 255),
                                  IM_COL32(6, 8, 14, 255));
}

__attribute__((format(printf, 1, 2))) void Log(const char* format, ...) {
    std::printf("Launcher: ");
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
}

const char* StatusTitle(GameFileState state) {
    switch (state) {
    case GameFileState::kValid: return "Ready to play";
    case GameFileState::kMissing: return "Game files needed";
    case GameFileState::kDifferentVersion: return "Different version of Odin Sphere Leifthrasir";
    case GameFileState::kReadError: return "Could not read the game files";
    }
    return "";
}

std::string StatusDetail(GameFileState state) {
    switch (state) {
    case GameFileState::kValid: return "";
    case GameFileState::kMissing: return "Install the game from your own .pkg file.";
    case GameFileState::kDifferentVersion: return "Install a supported build from your own .pkg file.";
    case GameFileState::kReadError: return "Check that the game folder can be read, then install again.";
    }
    return "";
}

GameCheck CheckAndLog(const GamePaths& paths) {
    const GameCheck check = CheckGameFiles(paths);
    Log("game files in %s: %s%s%s%s", Utf8(paths.app0).c_str(), StateName(check.state), check.detail.empty() ? "" : " (",
        check.detail.c_str(), check.detail.empty() ? "" : ")");
    if (check.build) {
        Log("build %s", check.build->label);
    }
    if (!check.eboot_sha256.empty()) {
        Log("eboot.bin SHA-256 %s", check.eboot_sha256.c_str());
    }
    return check;
}

std::filesystem::path SettingsPath(const OslLauncherConfig& config) {
    if (config.settings_file && *config.settings_file) {
        return PathFromUtf8(config.settings_file);
    }
    return PathFromUtf8(config.user_dir ? config.user_dir : "user") / "osl.ini";
}

enum class Outcome { kRunning, kPlay, kQuit };

class LauncherApp {
public:
    LauncherApp(const OslLauncherConfig& config, LauncherSettings& settings, GamePaths paths, GameCheck check)
        : config_(config), settings_(settings), paths_(std::move(paths)), files_(check.state), build_(check.build) {}

    // Runs the launcher window until Play or Quit. False when no window could be opened.
    bool Run(Outcome& outcome) {
        HostOptions options;
        options.title = kGameTitle;
        const WindowPlacement saved = ReadPlacement(settings_);
        if (PlacementIsSet(saved)) {
            options.restore = saved;
        }
        // OSL_LAUNCHER_SHOTS=<folder>: every page into the folder, hidden, then quit (development aid).
        const char* shots = std::getenv("OSL_LAUNCHER_SHOTS");
        options.hidden = shots != nullptr;
        if (!host_.Open(options)) {
            return false;
        }
        displays_ = ListDisplays();
        gpu_names_ = ListGpuNames();
        placement_ = PlacementTracker(host_.Restored());
        fonts_ = trg::LoadSystemFonts(18.0f);
        trophy_page_ = std::make_unique<TrophyPage>(host_, UserDir());
        launcher_ = std::make_unique<trg::Launcher>(MakeConfig());
        Log("shown");

        outcome = Outcome::kRunning;
        while (outcome == Outcome::kRunning) {
            if (!host_.PumpEvents()) {
                outcome = Outcome::kQuit;
                break;
            }
            if (host_.Minimized()) {
                SDL_Delay(30);
                continue;
            }
            host_.BeginFrame();
            outcome = Frame();
            std::string shot;
            if (shots) {
                constexpr int kFramesPerPage = 8;
                const int page = frames_ / kFramesPerPage;
                if (page >= static_cast<int>(std::size(kPageOrder))) {
                    outcome = Outcome::kQuit;
                } else if (frames_ % kFramesPerPage == 0) {
                    launcher_->GoToPage(page);
                } else if (frames_ % kFramesPerPage == kFramesPerPage - 1) {
                    shot = std::string(shots) + "/page_" + std::to_string(page) + "_" + PageName(kPageOrder[page]) + ".png";
                }
                ++frames_;
            }
            host_.EndFrame(LauncherTheme().bg, shot.empty() ? nullptr : shot.c_str());
        }
        SavePlacement(placement_.Flush());
        launcher_.reset();
        trophy_page_.reset();
        host_.Close();
        Log(outcome == Outcome::kPlay ? "closed, starting the game" : "closed");
        return true;
    }

private:
    Outcome Frame() {
        TrackPlacement();
        PollInstall();
        if (play_.starting() && ImGui::GetTime() - play_started_ >= kPlayFeedbackSeconds) {
            return Outcome::kPlay;
        }
        ImGuiIO& io = ImGui::GetIO();
        if (play_.starting()) {
            io.ClearInputKeys();
            io.ClearInputMouse();
        }
        ImGui::BeginDisabled(play_.starting());
        const trg::Result result = launcher_->Frame();
        ImGui::EndDisabled();
        if (const std::optional<std::filesystem::path> picked = browser_.Draw(fonts_.regular, fonts_.size, LauncherTheme())) {
            StartInstall(Utf8(*picked));
        }
        if (play_.starting()) {
            DrawStartingMessage();
        }
        problem_.Draw(fonts_, LauncherTheme());
        switch (result) {
        case trg::Result::kPlay:
            RequestPlay();
            break;
        case trg::Result::kQuit:
            if (!play_.starting() && LauncherQuitAllowed(ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
                return Outcome::kQuit;
            }
            break;
        case trg::Result::kNone:
            break;
        }
        return Outcome::kRunning;
    }

    // PLAY saves the settings once here, then starts the transition. A failed save keeps the launcher open.
    void RequestPlay() {
        if (play_.starting()) {
            return;
        }
        Recheck();
        if (files_ == GameFileState::kValid && !GraphicsUsable()) {
            return;
        }
        switch (play_.Request(install_.running(), files_ == GameFileState::kValid, [this] { return launcher_->Save(); })) {
        case PlayRequest::kStarting:
            play_started_ = ImGui::GetTime();
            launcher_->SetStatus("", 0.0);
            Log("settings saved, starting the game");
            break;
        case PlayRequest::kFilesNotReady:
            launcher_->SetStatus(std::string(StatusTitle(files_)) + ". Install the game files first.", 6.0);
            break;
        case PlayRequest::kBusy:
            launcher_->SetStatus("Wait for the game file changes to finish.", 6.0);
            break;
        case PlayRequest::kSaveFailed:
            Log("could not save the settings");
            break;
        }
    }

    bool GraphicsUsable() {
        if (!config_.check_graphics) {
            return true;
        }
        char cause[320] = "", advice[320] = "";
        if (config_.check_graphics(cause, sizeof(cause), advice, sizeof(advice))) {
            return true;
        }
        problem_.Show(cause, advice);
        Log("graphics unavailable, staying open");
        return false;
    }

    void Recheck() {
        const GameCheck check = CheckAndLog(paths_);
        files_ = check.state;
        build_ = check.build;
    }

    void TrackPlacement() {
        const auto state = host_.State();
        if (!state) {
            return;
        }
        placement_.Observe(*state, host_.Fullscreen(), ImGui::GetTime());
        SavePlacement(placement_.TakeSettled(ImGui::GetTime()));
    }

    void SavePlacement(const std::optional<WindowPlacement>& placement) {
        if (placement && !settings_.SaveNow(PlacementValues(*placement))) {
            Log("could not save the launcher window placement");
        }
    }

    void DrawStartingMessage() {
        constexpr const char* kText = "Settings saved. Starting game...";
        ImGui::PushFont(fonts_.semibold, fonts_.size * 1.25f);
        const float size = ImGui::GetFontSize();
        const ImVec2 text_size = ImGui::CalcTextSize(kText);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 pos(viewport->Pos.x + (viewport->Size.x - text_size.x) * 0.5f,
                         viewport->Pos.y + (viewport->Size.y - text_size.y) * 0.5f);
        ImGui::GetForegroundDrawList()->AddText(ImGui::GetFont(), size, pos, ImGui::GetColorU32(kAccent), kText);
        ImGui::PopFont();
    }

    trg::LauncherConfig MakeConfig() {
        trg::LauncherConfig c;
        c.settings = &settings_;
        c.save_on_play = false;
        c.theme = LauncherTheme();
        c.fonts = fonts_;
        c.branding.title = "ODIN SPHERE LEIFTHRASIR";
        c.branding.subtitle_color = kAccent;
        c.branding.subtitle_segments = {
            {"PC PORT   \xC2\xB7   ", ""},
            {"BY SAULOB", kAuthorUrl},
        };
        c.branding.background = LauncherHeader();
        c.hints = "Enter  Play     Alt+F4  Quit     Ctrl+S  Save";
        c.note = "Play applies the changes. Save keeps them without starting.";
        for (const PageId page : kPageOrder) {
            c.pages.push_back(MakePage(page));
        }
        c.can_play = [this] {
            if (install_.running()) {
                return trg::PlayCheck{false, "Wait for the installation to finish.", 0};
            }
            if (files_ == GameFileState::kValid) {
                return trg::PlayCheck{};
            }
            return trg::PlayCheck{false, "The game files are not ready: see the Play page.", 0};
        };
        return c;
    }

    trg::Page MakePage(PageId page) {
        const std::string name(PageName(page));
        switch (page) {
        case PageId::kPlay:
            return {name, "Check the game files and start playing.", [this](trg::Ui& ui) { PagePlay(ui); },
                    [this] { return files_ != GameFileState::kValid; }};
        case PageId::kDisplay:
            return {name, "Window, monitor, scaling and presentation.", [this](trg::Ui& ui) { PageDisplay(ui); },
                    nullptr};
        case PageId::kGraphics:
            return {name, "Resolution, anti-aliasing and texture filtering.", [this](trg::Ui& ui) { PageGraphics(ui); },
                    nullptr};
        case PageId::kGameplay:
            return {name, "Readouts.", [this](trg::Ui& ui) { PageGameplay(ui); }, nullptr};
        case PageId::kControls:
            return {name, "Controller and keyboard.", [this](trg::Ui& ui) { PageControls(ui); }, nullptr};
        case PageId::kTrophies:
            return {name, "Your trophies on this PC.", [this](trg::Ui& ui) { PageTrophies(ui); }, nullptr};
        case PageId::kAbout:
            return {name, "This port, and where your files and settings live.", [this](trg::Ui& ui) { PageAbout(ui); },
                    nullptr};
        }
        return {};
    }

    void PagePlay(trg::Ui& ui) {
        const bool ready = files_ == GameFileState::kValid;
        const std::string title = ready && build_ ? std::string(StatusTitle(files_)) + " \xC2\xB7 " + build_->label
                                                  : StatusTitle(files_);
        const std::string detail = ready && build_ ? BuildMetadataText(*build_) : StatusDetail(files_);
        ui.StatusCard(ready ? trg::Status::kReady : trg::Status::kAttention, title.c_str(), detail.c_str());
        if (ready && build_) {
            if (const GameBuild* update = UpdateBuildOf(*build_)) {
                const std::string text = "Install the " + std::string(update->label) + " update package to play it.";
                ui.Info("Update available", text.c_str());
            }
        }
        if (!install_failure_.empty()) {
            ui.Spacer(4);
            ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
            ImGui::TextUnformatted(install_failure_.c_str());
            ImGui::PopStyleColor();
            ui.Help(install_failure_text_.c_str());
            ui.Spacer(4);
        }
        const std::string files_help = GameFilesHelp();
        ui.Row("Game files", files_help.c_str());
        if (!ui.TaskProgress(install_)) {
            const bool clicked = ready ? ImGui::Button("Reinstall game...", ImVec2(-FLT_MIN, 0))
                                       : ui.AccentButton("Install game...");
            if (clicked) {
                BrowseAndInstall();
            }
        }
        ui.Toggle("Show this launcher", kShowLauncherHelp, kShowLauncherKey, true, "Off", "On");
        ui.EndRows();
    }

    // The desktop's chooser (Windows' own, zenity or kdialog) unless there is none or OSL_FILE_CHOOSER=builtin.
    static bool UseBuiltInChooser() {
#ifdef _WIN32
        return false;
#else
        const std::string_view preference = FileChooserPreference();
        if (preference != "") {
            return preference == "builtin";
        }
        return std::system("command -v zenity >/dev/null 2>&1 || command -v kdialog >/dev/null 2>&1") != 0;
#endif
    }

    void BrowseAndInstall() {
        if (install_.running() || browser_.active()) {
            return;
        }
        if (UseBuiltInChooser()) {
            browser_.Open(paths_.app0.parent_path().parent_path().parent_path());
            return;
        }
        StartInstall(trg::BrowseForFile("Select your Odin Sphere Leifthrasir package", {{"PS4 packages (*.pkg)", "*.pkg"}}));
    }

    void StartInstall(const std::string& picked) {
        if (picked.empty()) {
            return;
        }
        install_failure_.clear();
        install_failure_text_.clear();
        outcome_ = std::make_shared<InstallResult>();
        const bool started = install_.Start(
            "Checking the package...",
            [outcome = outcome_, source = PathFromUtf8(picked), roots = RootsOf(paths_),
             replace = files_ == GameFileState::kValid](trg::Task& task) {
                InstallHooks hooks;
                hooks.replace_active = replace;
                hooks.phase = [&task](const char* text) {
                    task.SetLabel(text);
                    task.Progress(0, 0);
                };
                hooks.progress = [&task](uint64_t done, uint64_t total) {
                    task.Progress(static_cast<long long>(done), static_cast<long long>(total));
                    return !task.cancelled();
                };
                *outcome = InstallGame(source, roots, hooks);
                return outcome->ok() ? std::string() : std::string(DescribeInstall(outcome->status).title);
            });
        if (started) {
            Log("installing the game from %s", picked.c_str());
        }
    }

    // Runs every frame so a finished install is picked up whichever page is open.
    void PollInstall() {
        if (install_.Finish() == trg::Task::State::kIdle) {
            return;
        }
        const std::shared_ptr<InstallResult> outcome = std::move(outcome_);
        if (!outcome) {
            return;
        }
        const InstallMessage message = DescribeInstall(outcome->status);
        if (outcome->ok()) {
            if (outcome->status == InstallStatus::kUpToDate) {
                Log("game already up to date: %s", outcome->detail.c_str());
            } else if (outcome->status == InstallStatus::kActivated) {
                Log("game activated: %s", outcome->build->label);
            } else {
                Log("game installed: %zu files (%zu linked), %llu bytes", outcome->files, outcome->linked,
                    static_cast<unsigned long long>(outcome->bytes));
            }
            paths_ = ActiveBuildPaths(RootsOf(paths_));
            Recheck();
            if (trophy_page_) {
                trophy_page_->Invalidate();
            }
            launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
            return;
        }
        if (outcome->status == InstallStatus::kCancelled) {
            Log("game install cancelled");
            launcher_->SetStatus(std::string(message.title) + ". " + message.text, 6.0);
            return;
        }
        Log("game install failed: %s (%s)", message.title.c_str(), outcome->detail.c_str());
        install_failure_ = message.title;
        install_failure_text_ = message.text;
        launcher_->SetStatus(std::string("Install failed: ") + install_failure_, 8.0);
    }

    const Display* SelectedDisplay() const { return DisplayForSetting(displays_, settings_.GetInt(kMonitorKey, 1)); }

    const OslAspectRatio& SavedAspectRatio() const {
        return osl_aspect_ratios[osl_aspect_index(settings_.Get(kAspectRatioKey, kDefaultAspectRatio).c_str())];
    }

    void PageDisplay(trg::Ui& ui) {
        ui.Choice("Display mode", "Borderless Fullscreen fills the monitor. F11 switches while playing.", kDisplayModeKey,
                  "windowed", {{"windowed", "Windowed"}, {"borderless", "Borderless Fullscreen"}}, 2);
        const OslAspectRatio& previous_aspect = SavedAspectRatio();
        ui.Row("Window size", kWindowSizeHelp);
        WindowSizeCombo();
        ui.Row("Monitor", kMonitorHelp);
        MonitorCombo();
        std::vector<trg::Option> aspects;
        const bool custom = !RestrictedToNativeResolution(build_);
        for (const OslAspectRatio& aspect : osl_aspect_ratios) {
            if (custom || std::string_view(aspect.key) == kDefaultAspectRatio) {
                aspects.push_back({aspect.key, aspect.name});
            }
        }
        ui.Combo("Aspect Ratio", custom ? kAspectRatioHelp : kRestrictedAspectRatioHelp, kAspectRatioKey,
                 kDefaultAspectRatio, aspects);
        if (&SavedAspectRatio() != &previous_aspect) {
            const int height = settings_.GetInt(kWindowHeightKey, kDefaultWindowHeight);
            settings_.SetInt(kWindowWidthKey, WindowSizeOf(height, SavedAspectRatio()).width);
        }
        ui.Row("Graphics card", kGraphicsCardHelp);
        GraphicsCardCombo();
        ui.Toggle("VSync", kVSyncHelp, kVSyncKey, true, "Off", "On");
        std::vector<trg::Option> presentations;
        for (const PresentationChoice& choice : kPresentationChoices) {
            presentations.push_back({std::string(choice.key), std::string(choice.name)});
        }
        ui.Choice("Presentation", kPresentationHelp, kPresentationKey, kDefaultPresentation, presentations, 2);
        ui.Heading("Scaling and sharpening");
        ui.Toggle("Upscaling", kUpscaleHelp, kUpscaleKey, false, "Off", "FSR 1");
        ui.Toggle("Sharpening", kSharpenHelp, kSharpenKey, true, "Off", "On");
        ImGui::BeginDisabled(!settings_.GetBool(kSharpenKey, true));
        ui.SliderInt("Sharpening strength", "How strong the sharpening is.", kSharpenStrengthKey, 75, 0, 100, "%d%%", 5);
        ImGui::EndDisabled();
        ui.EndRows();
    }

    void WindowSizeCombo() {
        int width = settings_.GetInt(kWindowWidthKey, kDefaultWindowWidth);
        int height = settings_.GetInt(kWindowHeightKey, kDefaultWindowHeight);
        if (width <= 0 || height <= 0) {
            width = kDefaultWindowWidth;
            height = kDefaultWindowHeight;
        }
        const WindowSize current{width, height};
        const Display* display = SelectedDisplay();
        if (ImGui::BeginCombo("##window_size", WindowSizeLabel(current).c_str())) {
            for (const WindowSize size : WindowSizesFor(SavedAspectRatio())) {
                const bool selected = size == current;
                if (!selected && display && !WindowSizeFits(size, *display)) {
                    continue;
                }
                if (ImGui::Selectable(WindowSizeLabel(size).c_str(), selected) && !selected) {
                    settings_.SetInt(kWindowWidthKey, size.width);
                    settings_.SetInt(kWindowHeightKey, size.height);
                }
            }
            ImGui::EndCombo();
        }
    }

    void GraphicsCardCombo() {
        const std::string current = settings_.Get(kGraphicsCardKey, kAutomaticGpu);
        const bool listed = current == kAutomaticGpu ||
                            std::find(gpu_names_.begin(), gpu_names_.end(), current) != gpu_names_.end();
        const std::string shown = current == kAutomaticGpu ? "Automatic" : listed ? current : current + " (not available)";
        if (ImGui::BeginCombo("##graphics_card", shown.c_str())) {
            if (ImGui::Selectable("Automatic", current == kAutomaticGpu) && current != kAutomaticGpu) {
                settings_.Set(kGraphicsCardKey, kAutomaticGpu);
            }
            for (const std::string& name : gpu_names_) {
                if (ImGui::Selectable(name.c_str(), name == current) && name != current) {
                    settings_.Set(kGraphicsCardKey, name);
                }
            }
            ImGui::EndCombo();
        }
    }

    void MonitorCombo() {
        const int current = settings_.GetInt(kMonitorKey, 1);
        if (ImGui::BeginCombo("##monitor", MonitorLabel(current, displays_).c_str())) {
            for (int i = 1; i <= static_cast<int>(displays_.size()); ++i) {
                if (ImGui::Selectable(MonitorLabel(i, displays_).c_str(), i == current) && i != current) {
                    settings_.SetInt(kMonitorKey, i);
                }
            }
            ImGui::EndCombo();
        }
    }

    void PageGraphics(trg::Ui& ui) {
        const bool custom = !RestrictedToNativeResolution(build_);
        std::vector<trg::Option> options;
        for (const OslResolution& resolution : osl_resolutions) {
            if (custom || std::string_view(resolution.key) == kNativeResolution) {
                options.push_back({resolution.key, ResolutionLabel(resolution)});
            }
        }
        ui.Combo("Render resolution", custom ? kRenderResolutionHelp : kRestrictedRenderResolutionHelp,
                 kRenderResolutionKey, kNativeResolution, options);
        ui.Toggle("Anti-aliasing", kAntiAliasingHelp, kAntiAliasingKey, false, "Off", "SMAA");
        ui.Combo("Texture filtering", kAnisotropyHelp, kAnisotropyKey, "0",
                 {{"0", "Game"}, {"2", "2x"}, {"4", "4x"}, {"8", "8x"}, {"16", "16x"}});
        ui.EndRows();
    }

    void PageGameplay(trg::Ui& ui) {
        ui.Toggle("Frame rate in title", kShowFpsTitleHelp, kShowFpsTitleKey, false, "Off", "On");
        ui.Toggle("Render resolution in title", kShowResolutionTitleHelp, kShowResolutionTitleKey, false, "Off", "On");
        ui.Toggle("Frame counter", kFrameCounterHelp, kShowFrameCounterKey, false, "Hidden", "Shown");
        ui.EndRows();
    }

    void PageControls(trg::Ui& ui) {
        ui.Info("Controller", "Automatic: Xbox controllers work without setup.");
        ui.Info("Keyboard", "Always on, together with the controller.");
        if (ui.ConfirmRow("Keyboard defaults", "Puts the keyboard controls and the port hotkeys back to their defaults.",
                          "Reset to defaults", "Put every key back to its default?", "Reset")) {
            ResetKeys(settings_);
            ui.SetStatus("Keys reset to defaults.");
        }
        if (ui.Section("Keyboard controls", true)) {
            ui.Help("Click a key, then press the new one. Esc cancels. A key can be used for more than one control.");
            for (int action = 0; action < OSL_KEY_FIRST_HOTKEY; ++action) {
                KeyRow(ui, static_cast<OslKeyAction>(action));
            }
        }
        if (ui.Section("Port hotkeys", true)) {
            ui.Help(kMenuKeyHelp);
            for (int action = OSL_KEY_FIRST_HOTKEY; action < OSL_KEY_COUNT; ++action) {
                KeyRow(ui, static_cast<OslKeyAction>(action));
            }
        }
    }

    void KeyRow(trg::Ui& ui, OslKeyAction action) {
        const std::optional<ImGuiKey> key =
            ui.KeyBindRow(osl_key_actions[action].label, nullptr, KeyDisplayName(SavedKey(settings_, action)));
        if (!key) {
            return;
        }
        if (const int scancode = ScancodeOf(*key)) {
            SetKey(settings_, action, scancode);
        } else {
            ui.SetStatus("That key cannot be used.");
        }
    }

    std::filesystem::path UserDir() const { return PathFromUtf8(config_.user_dir ? config_.user_dir : "user"); }

    void PageTrophies(trg::Ui& ui) {
        trophy_page_->SetGame(files_ == GameFileState::kValid ? paths_.app0 : std::filesystem::path());
        trophy_page_->Draw(ui);
    }

    void PageAbout(trg::Ui& ui) {
        ui.Paragraph("Odin Sphere Leifthrasir for PlayStation 4, running natively on this PC. No game files are included; "
                     "the game runs from the user's own copy.");
        ui.Spacer(4);
        std::error_code ec;
        if (std::filesystem::is_directory(paths_.app0, ec)) {
            ui.FolderRow("Game files", "Where the prepared game is read from.", "Open game folder", Utf8(paths_.app0));
        }
        const std::filesystem::path user = UserDir();
        std::filesystem::create_directories(user, ec);
        ui.FolderRow("Saves and settings", "Settings, saves and caches.", "Open user folder", Utf8(user));
        if (ui.ButtonRow("Settings file", "Launcher choices stored as plain text.", "Show settings file") &&
            launcher_->Save()) {
            trg::OpenInFileManager(Utf8(std::filesystem::exists(settings_.file(), ec) ? settings_.file() : user));
        }
        if (ui.ConfirmRow("Reset settings", "Put the launcher and display settings back to their defaults.",
                          "Reset all settings", "Put every setting back to its default?", "Reset")) {
            settings_.ResetToDefaults();
            launcher_->SetStatus("Settings reset to defaults. Save or press Play to keep them.", 6.0);
            Log("settings reset to defaults (pending)");
        }
        ui.Heading("Diagnostics");
        ui.Toggle("Detailed logging", "Keeps logs for normal sessions. Crash diagnostics are always preserved.",
                  kDetailedLoggingKey, true);
        if (ui.ButtonRow("Logs folder", "Open the folder containing session and crash logs", "Open logs folder")) {
            OpenLogsFolder();
        }
        ui.Info("Port version", OSL_PORT_VERSION);
        const std::string game = InstalledGameName(files_, build_);
        ui.Info("Game", game.c_str());
        ui.Info("Renderer", "Vulkan");
#ifdef _WIN32
        ui.Info("Platform", "Windows x64");
#else
        ui.Info("Platform", "Linux x86-64 (experimental)");
#endif
        char libraries[96];
        std::snprintf(libraries, sizeof(libraries), "SDL %d.%d.%d  \xC2\xB7  Dear ImGui %s  \xC2\xB7  TRG Launcher %s",
                      SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
                      SDL_VERSIONNUM_MICRO(SDL_GetVersion()), IMGUI_VERSION, OSL_TRG_LAUNCHER_VERSION);
        ui.Info("Libraries", libraries);
        ui.Link(kPortRepository, kPortUrl);
    }

    void OpenLogsFolder() {
        if (!config_.logs_dir || !*config_.logs_dir) {
            Log("could not open the logs folder: no logs directory is configured");
            launcher_->SetStatus("Could not open the logs folder.", 6.0);
            return;
        }
        std::error_code ec;
        std::filesystem::path folder = PathFromUtf8(config_.logs_dir);
        std::filesystem::create_directories(folder, ec);
        if (!std::filesystem::is_directory(folder, ec)) {
            Log("could not create the logs folder %s: %s", Utf8(folder).c_str(), ec.message().c_str());
            const std::filesystem::path parent = folder.parent_path();
            ec.clear();
            if (parent.empty() || !std::filesystem::is_directory(parent, ec)) {
                launcher_->SetStatus("Could not open the logs folder.", 6.0);
                return;
            }
            folder = parent;
        }
        trg::OpenInFileManager(Utf8(folder));
    }

    const OslLauncherConfig& config_;
    LauncherSettings& settings_;
    GamePaths paths_;
    GameFileState files_;
    const GameBuild* build_ = nullptr;
    LauncherHost host_;
    std::vector<Display> displays_;
    std::vector<std::string> gpu_names_;
    trg::Fonts fonts_;
    std::unique_ptr<trg::Launcher> launcher_;
    std::unique_ptr<TrophyPage> trophy_page_;
    PlayGate play_;
    ProblemDialog problem_;
    PlacementTracker placement_;
    int frames_ = 0;
    double play_started_ = 0.0;
    std::shared_ptr<InstallResult> outcome_;
    trg::Task install_;
    PackageBrowser browser_;
    std::string install_failure_;
    std::string install_failure_text_;
};

// Play without the launcher on screen: the same check, explained in a window of its own.
OslLauncherResult PlayOrExplain(const OslLauncherConfig& config) {
    char cause[320] = "", advice[320] = "";
    if (!config.check_graphics || config.check_graphics(cause, sizeof(cause), advice, sizeof(advice))) {
        return OSL_LAUNCHER_PLAY;
    }
    LauncherHost host;
    HostOptions options;
    options.title = kPortTitle;
    options.width = 640;
    options.height = 300;
    options.min_width = 360;
    options.min_height = 220;
    if (!host.Open(options)) {
        std::fprintf(stderr, "Graphics: %s %s\n", cause, advice);
        return OSL_LAUNCHER_BLOCKED;
    }
    const trg::Fonts fonts = trg::LoadSystemFonts(18.0f);
    const trg::Theme theme = LauncherTheme();
    ProblemDialog dialog;
    dialog.Show(cause, advice);
    while (dialog.open() && host.PumpEvents()) {
        if (host.Minimized()) {
            SDL_Delay(30);
            continue;
        }
        host.BeginFrame();
        dialog.Draw(fonts, theme);
        host.EndFrame(theme.bg);
    }
    return OSL_LAUNCHER_BLOCKED;
}

} // namespace
} // namespace osl

extern "C" OslLauncherResult osl_launcher_run(const OslLauncherConfig* config) {
    using namespace osl;
    const GamePaths paths{PathFromUtf8(config->app0 ? config->app0 : ""), PathFromUtf8(config->image_dir ? config->image_dir : "")};
    LauncherSettings settings(SettingsPath(*config));
    const GameCheck check = CheckAndLog(paths);
    const bool ready = check.state == GameFileState::kValid;
    const bool show_setting = settings.GetBool(kShowLauncherKey, true);
    const bool shift = ShiftHeldAtStart() || config->force_show != 0;
    if (!LauncherWanted(ready, show_setting, shift, config->skip_once != 0)) {
        Log("skipped (show_launcher %s%s)", show_setting ? "on" : "off", config->skip_once ? ", skip requested" : "");
        return PlayOrExplain(*config);
    }
    Log("settings file %s", Utf8(settings.file()).c_str());
    Log("opening: %s%s%s", ready ? "game ready" : "game files not ready", show_setting ? ", show_launcher on" : "",
        shift ? ", Shift held or --launcher" : "");

    LauncherApp app(*config, settings, paths, check);
    Outcome outcome = Outcome::kRunning;
    if (!app.Run(outcome)) {
        std::fprintf(stderr, "Launcher: could not open its window; %s\n", ready ? "starting the game" : "game files are not ready");
        return ready ? PlayOrExplain(*config) : OSL_LAUNCHER_QUIT;
    }
    return outcome == Outcome::kPlay ? OSL_LAUNCHER_PLAY : OSL_LAUNCHER_QUIT;
}
