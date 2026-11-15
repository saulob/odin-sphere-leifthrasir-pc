// The in-game cheat menu without a window: the session model, the runtime state the hooks read, the menu's rows and
// scaling, the F10 hotkey, the keyboard, mouse and controller steps, the close gate, and what is never written.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>

#include "cheats_test_support.h"
#include "options.h"
#include "osl_cheat_menu.h"
#include "osl_cheat_session.h"
#include "osl_cheats.h"
#include "osl_keys.h"
#include "osl_launcher.h"
#include "osl_menu_input.h"
#include "runtime_cheats.h"
#include "settings.h"
#include "test_support.h"

namespace cheat_test {
namespace {

namespace fs = std::filesystem;
using namespace osl;
using osl_test::Contains;
using osl_test::ReadFile;
using osl_test::TempDir;

constexpr unsigned kAll = OSL_CHEAT_ALL;
constexpr OslCheat kCheats[] = {OSL_CHEAT_MAX_GOLD_GAIN, OSL_CHEAT_MAX_PHOZON_GAIN, OSL_CHEAT_NO_AP_CONSUMPTION,
                                OSL_CHEAT_INFINITE_PP,   OSL_CHEAT_INFINITE_POW,    OSL_CHEAT_INFINITE_HEALTH,
                                OSL_CHEAT_INFINITE_ARROWS};

void TestSession() {
    cheats::SessionCheats session;
    session.Start(0);
    for (const OslCheat cheat : kCheats) {
        CHECK(!session.Effective(cheat));
        CHECK(session.Toggle(cheat) && session.Effective(cheat));
        CHECK(!session.Toggle(cheat) && !session.Effective(cheat));
    }
    session.Start(kAll);
    for (const OslCheat cheat : kCheats) {
        CHECK(session.Effective(cheat));
        CHECK(!session.Toggle(cheat) && !session.Effective(cheat) && session.Saved(cheat));
        CHECK(session.Toggle(cheat) && session.Effective(cheat));
    }

    // A change to one cheat leaves the others as saved, and a new session removes every override.
    session.Start(osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) | osl_cheat_bit(OSL_CHEAT_INFINITE_POW));
    session.Toggle(OSL_CHEAT_MAX_GOLD_GAIN);
    session.Toggle(OSL_CHEAT_INFINITE_ARROWS);
    CHECK(!session.Effective(OSL_CHEAT_MAX_GOLD_GAIN) && session.Effective(OSL_CHEAT_INFINITE_POW) &&
          session.Effective(OSL_CHEAT_INFINITE_ARROWS) && !session.Effective(OSL_CHEAT_INFINITE_PP));
    CHECK(session.Saved(OSL_CHEAT_MAX_GOLD_GAIN) && !session.Saved(OSL_CHEAT_INFINITE_ARROWS));
    session.Start(osl_cheat_bit(OSL_CHEAT_MAX_GOLD_GAIN) | osl_cheat_bit(OSL_CHEAT_INFINITE_POW));
    CHECK(session.Effective(OSL_CHEAT_MAX_GOLD_GAIN) && session.Effective(OSL_CHEAT_INFINITE_POW) &&
          !session.Effective(OSL_CHEAT_INFINITE_ARROWS));
    session.Start(0xffffffffu);
    CHECK(session.Effective(OSL_CHEAT_INFINITE_ARROWS));
}

void TestRuntimeState() {
    alignas(8) volatile uint8_t flags[OSL_CHEAT_COUNT] = {};
    runtime_cheats_start(0);
    runtime_cheats_set_available(kAll, flags);
    CHECK(runtime_cheats_available_mask() == kAll);
    for (const OslCheat cheat : kCheats) {
        CHECK(runtime_cheat_available(cheat) && !runtime_cheat_enabled(cheat) && flags[cheat] == 0);
        CHECK(runtime_cheat_toggle(cheat) == 1 && runtime_cheat_enabled(cheat) && flags[cheat] == 1);
        CHECK(runtime_cheat_toggle(cheat) == 0 && !runtime_cheat_enabled(cheat) && flags[cheat] == 0);
    }

    // Six independent switches, several at once.
    runtime_cheat_toggle(OSL_CHEAT_MAX_GOLD_GAIN);
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_POW);
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_ARROWS);
    for (const OslCheat cheat : kCheats) {
        const bool on = cheat == OSL_CHEAT_MAX_GOLD_GAIN || cheat == OSL_CHEAT_INFINITE_POW || cheat == OSL_CHEAT_INFINITE_ARROWS;
        CHECK(runtime_cheat_enabled(cheat) == (on ? 1 : 0) && flags[cheat] == (on ? 1 : 0));
    }
    for (const OslCheat cheat : kCheats) runtime_cheat_toggle(cheat);
    CHECK(!runtime_cheat_enabled(OSL_CHEAT_MAX_GOLD_GAIN) && runtime_cheat_enabled(OSL_CHEAT_INFINITE_PP) &&
          runtime_cheat_enabled(OSL_CHEAT_NO_AP_CONSUMPTION));

    // Saved On: the menu can turn it Off for the session; starting again restores it.
    const unsigned saved = osl_cheat_bit(OSL_CHEAT_INFINITE_POW) | osl_cheat_bit(OSL_CHEAT_MAX_PHOZON_GAIN);
    runtime_cheats_start(saved);
    CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) && flags[OSL_CHEAT_INFINITE_POW] == 1 &&
          !runtime_cheat_enabled(OSL_CHEAT_INFINITE_PP) && flags[OSL_CHEAT_INFINITE_PP] == 0);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_POW) == 0 && flags[OSL_CHEAT_INFINITE_POW] == 0);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_POW) == 1 && flags[OSL_CHEAT_INFINITE_POW] == 1);
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_POW);
    runtime_cheat_toggle(OSL_CHEAT_INFINITE_PP);
    runtime_cheats_start(saved);
    CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) && !runtime_cheat_enabled(OSL_CHEAT_INFINITE_PP));

    // A cheat without its hooks cannot be switched, shows no On state, and keeps its saved value.
    const unsigned without_pp = kAll & ~osl_cheat_bit(OSL_CHEAT_INFINITE_PP);
    runtime_cheats_start(kAll);
    runtime_cheats_set_available(without_pp, flags);
    CHECK(!runtime_cheat_available(OSL_CHEAT_INFINITE_PP) && runtime_cheat_toggle(OSL_CHEAT_INFINITE_PP) == -1);
    CHECK(!runtime_cheat_enabled(OSL_CHEAT_INFINITE_PP) && flags[OSL_CHEAT_INFINITE_PP] == 0);
    CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) && flags[OSL_CHEAT_INFINITE_POW] == 1);
    runtime_cheats_set_available(kAll, flags);
    CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_PP) && flags[OSL_CHEAT_INFINITE_PP] == 1);
    runtime_cheats_set_available(0, flags);
    for (const OslCheat cheat : kCheats) {
        CHECK(runtime_cheat_toggle(cheat) == -1 && !runtime_cheat_enabled(cheat) && flags[cheat] == 0);
    }
    CHECK(runtime_cheat_toggle(-1) == -1 && runtime_cheat_toggle(OSL_CHEAT_COUNT) == -1 && !runtime_cheat_enabled(99));
    runtime_cheats_set_available(0, nullptr);
    runtime_cheats_start(0);
}

void TestSessionNeverWritesSettings() {
    const fs::path dir = TempDir("cheats-session");
    const fs::path file = dir / "osl.ini";
    for (const bool saved_on : {false, true}) {
        {
            std::ofstream out(file, std::ios::trunc | std::ios::binary);
            out << "# comment\nshow_launcher = 1\ncheat_infinite_pow=" << (saved_on ? 1 : 0) << "\ncheat_max_gold_gain=0\n";
        }
        const std::string before = ReadFile(file);
        const auto time = fs::last_write_time(file);
        const std::string path = file.string();
        alignas(8) volatile uint8_t flags[OSL_CHEAT_COUNT] = {};
        runtime_cheats_start(osl_cheats_enabled(path.c_str()));
        runtime_cheats_set_available(kAll, flags);
        CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) == (saved_on ? 1 : 0));
        runtime_cheat_toggle(OSL_CHEAT_INFINITE_POW);
        runtime_cheat_toggle(OSL_CHEAT_MAX_GOLD_GAIN);
        CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) == (saved_on ? 0 : 1) && runtime_cheat_enabled(OSL_CHEAT_MAX_GOLD_GAIN));
        CHECK(ReadFile(file) == before && fs::last_write_time(file) == time);

        // The next game session follows what the launcher saved.
        runtime_cheats_start(osl_cheats_enabled(path.c_str()));
        CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_POW) == (saved_on ? 1 : 0) && !runtime_cheat_enabled(OSL_CHEAT_MAX_GOLD_GAIN));
        CHECK(ReadFile(file) == before);
    }
    runtime_cheats_set_available(0, nullptr);
    runtime_cheats_start(0);

    const fs::path root(OSL_SOURCE_DIR);
    const std::string runtime = ReadFile(root / "src" / "runtime_cheats.cpp");
    for (const char* forbidden : {"fopen", "ofstream", "fwrite", "osl.ini", "mutex", "Save"}) CHECK(!Contains(runtime, forbidden));
    CHECK(Contains(runtime, "__ATOMIC_RELEASE") && Contains(runtime, "std::atomic"));
    const std::string overlay = ReadFile(root / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(!Contains(overlay, "SaveDisplayMode") && !Contains(overlay, "WriteEntries"));
}

void TestMenuModel() {
    const auto rows = cheats::MenuRows();
    CHECK(rows.size() == 8 && cheats::kMenuRowCount == 8);
    const char* const labels[] = {"Max Gold Gain", "Max Phozon Gain", "No AP Consumption", "Infinite PP",
                                  "Infinite POW", "Infinite Health", "Infinite Arrows"};
    CHECK(rows[7].kind == cheats::RowKind::Experience && std::string(rows[7].label) == "Experience Multiplier");
    for (size_t i = 0; i < 7; ++i) {
        CHECK(rows[i].kind == cheats::RowKind::Toggle);
        CHECK(rows[i].cheat == kCheats[i] && std::string(rows[i].label) == labels[i]);
        CHECK(std::string(rows[i].label) == osl_cheats[i].label);
    }
    CHECK(std::string(cheats::kMenuTitle) == "CHEATS" && std::string(cheats::kMenuNote) == "Not saved to the launcher");
    CHECK(cheats::CloseHint("F10") == "F10 Close" && cheats::CloseHint("F9") == "F9 Close");
    CHECK(std::string(cheats::StateText(cheats::StateOf(true, false))) == "Off");
    CHECK(std::string(cheats::StateText(cheats::StateOf(true, true))) == "On");
    CHECK(std::string(cheats::StateText(cheats::StateOf(false, true))) == "Unavailable");
    CHECK(std::string(cheats::StateText(cheats::StateOf(false, false))) == "Unavailable");
    CHECK(cheats::ToggleLogLine("Infinite POW", true) == "Cheats: Infinite POW On (this session)");
    CHECK(cheats::ToggleLogLine("Infinite POW", false) == "Cheats: Infinite POW Off (this session)");
    CHECK(cheats::ToggleLogLine(osl_cheats[OSL_CHEAT_INFINITE_HEALTH].label, true) == "Cheats: Infinite Health On (this session)");
    CHECK(cheats::ToggleLogLine(osl_cheats[OSL_CHEAT_INFINITE_HEALTH].label, false) == "Cheats: Infinite Health Off (this session)");

    // No restart wording: the menu's live controls say nothing about the next start.
    const std::string header = ReadFile(fs::path(OSL_SOURCE_DIR) / "src" / "osl_cheat_menu.h");
    const std::string overlay = ReadFile(fs::path(OSL_SOURCE_DIR) / "gpu" / "shim" / "osl_overlay.cpp");
    for (const char* forbidden : {"next start", "restart", "Restart", "applies next"}) CHECK(!Contains(header, forbidden));
    CHECK(!Contains(overlay, "restart") && !Contains(overlay, "next start"));
}

void TestMenuScaling() {
    // 100%, 150% and 225% display scale at 720p, 1080p, 1440p and 4K.
    const float widths[] = {1280, 1920, 2560, 3840};
    const float heights[] = {720, 1080, 1440, 2160};
    for (const float display : {1.0f, 1.5f, 2.25f}) {
        for (size_t i = 0; i < 4; ++i) {
            const float em_pixels = 18.0f * std::max(heights[i] / 1080.0f, 0.75f);
            const float scale = cheats::MenuScale(display, widths[i], em_pixels);
            CHECK(scale >= 1.0f && scale <= cheats::kMenuMaxScale);
            const float panel = cheats::kMenuWidthEm * em_pixels * scale;
            CHECK(panel <= cheats::kMenuMaxWidthShare * widths[i] + 0.5f || scale == 1.0f);
            const float em = em_pixels * scale;
            const float row = em + 2.0f * 4.0f * scale + 4.0f * scale;
            const float body = cheats::MenuBodyHeight(cheats::kMenuRowCount, row, 4.0f * scale);
            const float margin = em * 0.8f;
            const float available = heights[i] - 2.0f * margin;
            const float chrome = em * 3.0f + em;
            CHECK(!cheats::MenuNeedsScrollbar(body, std::min(body, available - chrome)));
        }
    }
    CHECK(cheats::MenuScale(1.0f, 1920, 18.0f) == cheats::kMenuMinScale);
    CHECK(cheats::MenuScale(2.25f, 3840, 18.0f) > cheats::MenuScale(1.0f, 3840, 18.0f));
    CHECK(cheats::MenuScale(100.0f, 100000, 18.0f) == cheats::kMenuMaxScale * cheats::kMenuScaleStep);
    CHECK(cheats::MenuScale(1.0f, 100, 18.0f) == 1.0f);
    CHECK(cheats::MenuBodyHeight(0, 30, 4) == 0 && cheats::MenuBodyHeight(7, 30, 4) == 234);
    CHECK(cheats::MenuNeedsScrollbar(500, 300) && !cheats::MenuNeedsScrollbar(300.5f, 300));
    CHECK(cheats::MenuScrollToShowRow(10, 40, 20, 100) == -10 && cheats::MenuScrollToShowRow(90, 120, 20, 100) == 20 &&
          cheats::MenuScrollToShowRow(30, 60, 20, 100) == 0);
    CHECK(cheats::MenuClampScroll(-5, 50) == 0 && cheats::MenuClampScroll(80, 50) == 50 && cheats::MenuClampScroll(5, -3) == 0);

    cheats::MenuList list;
    const int count = static_cast<int>(cheats::kMenuRowCount);
    CHECK(list.selected == 0 && list.reveal);
    list.Step(-1, count);
    CHECK(list.selected == 0);
    for (int i = 0; i < 10; ++i) list.Step(1, count);
    CHECK(list.selected == count - 1);
    CHECK(list.Resolve(0, 100, 120, 150, 0, 100) == 50 && !list.reveal);
    CHECK(list.Resolve(35, 100, 120, 150, 0, 100) == 35); // a wheel scroll is left where it is
    list.Click(2, count);
    CHECK(list.selected == 2 && !list.reveal);
    list.Click(99, count);
    CHECK(list.selected == count - 1);
    list.Click(-3, count);
    CHECK(list.selected == 0);
    list.Clamp(0);
    CHECK(list.selected == 0);
    list.Open();
    CHECK(list.selected == 0 && list.reveal);
}

void TestHotkeys() {
    CHECK(osl_key_actions[OSL_KEY_HOTKEY_CHEATS].default_scancode == SDL_SCANCODE_F10);
    CHECK(osl_key_actions[OSL_KEY_HOTKEY_WINDOW_MODE].default_scancode == SDL_SCANCODE_F11);
    CHECK(std::string(osl_key_actions[OSL_KEY_HOTKEY_CHEATS].ini_key) == "hotkey_cheats" &&
          std::string(osl_key_actions[OSL_KEY_HOTKEY_CHEATS].label) == "Cheats menu" &&
          std::string(osl_key_actions[OSL_KEY_HOTKEY_WINDOW_MODE].label) == "Window mode");
    CHECK(OSL_KEY_FIRST_HOTKEY == OSL_KEY_HOTKEY_CHEATS && OSL_KEY_HOTKEY_WINDOW_MODE == OSL_KEY_HOTKEY_CHEATS + 1 &&
          OSL_KEY_COUNT == OSL_KEY_HOTKEY_WINDOW_MODE + 1);
    std::set<int> defaults;
    for (const OslKeyInfo& action : osl_key_actions) CHECK(defaults.insert(action.default_scancode).second);

    OslKeyMap map;
    osl_keys_defaults(&map);
    CHECK(map.scancode[OSL_KEY_HOTKEY_CHEATS] == SDL_SCANCODE_F10);
    CHECK(osl_keys_apply(&map, "hotkey_cheats", "f9") && map.scancode[OSL_KEY_HOTKEY_CHEATS] == SDL_SCANCODE_F9 &&
          map.scancode[OSL_KEY_HOTKEY_WINDOW_MODE] == SDL_SCANCODE_F11);
    CHECK(!osl_keys_apply(&map, "hotkey_cheats", "escape") && map.scancode[OSL_KEY_HOTKEY_CHEATS] == SDL_SCANCODE_F9);

    // Saved, reloaded, rebound and reset like every other binding.
    const fs::path file = TempDir("cheats-hotkey") / "osl.ini";
    {
        LauncherSettings settings(file);
        CHECK(SavedKey(settings, OSL_KEY_HOTKEY_CHEATS) == SDL_SCANCODE_F10);
        SetKey(settings, OSL_KEY_HOTKEY_CHEATS, SDL_SCANCODE_F9);
        CHECK(SavedKey(settings, OSL_KEY_HOTKEY_CHEATS) == SDL_SCANCODE_F9 && settings.Save());
    }
    CHECK(Contains(ReadFile(file), "hotkey_cheats"));
    {
        LauncherSettings reloaded(file);
        CHECK(SavedKey(reloaded, OSL_KEY_HOTKEY_CHEATS) == SDL_SCANCODE_F9);
        OslKeyMap loaded;
        osl_keys_defaults(&loaded);
        osl_keys_load(file.string().c_str(), &loaded);
        CHECK(loaded.scancode[OSL_KEY_HOTKEY_CHEATS] == SDL_SCANCODE_F9);
        ResetKeys(reloaded);
        CHECK(SavedKey(reloaded, OSL_KEY_HOTKEY_CHEATS) == SDL_SCANCODE_F10 &&
              SavedKey(reloaded, OSL_KEY_HOTKEY_WINDOW_MODE) == SDL_SCANCODE_F11);
        reloaded.ResetToDefaults();
        CHECK(reloaded.Get("hotkey_cheats") == "f10");
        CHECK(IsValidSetting("hotkey_cheats", "f9") && !IsValidSetting("hotkey_cheats", "not a key"));
    }
    const auto managed = ManagedSettings();
    CHECK(std::any_of(managed.begin(), managed.end(),
                      [](const ManagedSetting& m) { return m.key == "hotkey_cheats" && m.default_value == "f10"; }));
    CHECK(KeyDisplayName(SDL_SCANCODE_F10) == "F10");

    // The runtime reads the saved key, never a hardcoded F10, and shows both hotkeys on the Controls page.
    const fs::path root(OSL_SOURCE_DIR);
    const std::string settings_source = ReadFile(root / "gpu" / "shim" / "osl_settings.cpp");
    CHECK(Contains(settings_source, "v.cheat_hotkey = scancode;") &&
          Contains(settings_source, "osl_key_actions[OSL_KEY_HOTKEY_CHEATS].ini_key"));
    const std::string overlay = ReadFile(root / "gpu" / "shim" / "osl_overlay.cpp");
    const std::string window = ReadFile(root / "gpu" / "shim" / "window.cpp");
    CHECK(Contains(overlay, "OslSettings::Get().cheat_hotkey") && !Contains(overlay, "SDL_SCANCODE_F10"));
    CHECK(Contains(window, "event.key.scancode == OslSettings::Get().cheat_hotkey") && !Contains(window, "SDL_SCANCODE_F10"));
    CHECK(Contains(window, "frame_counter_shortcut = event.key.scancode == SDL_SCANCODE_F2") &&
          Contains(window, "window_shortcut = event.key.scancode == OslSettings::Get().window_mode_key"));
    const std::string launcher = ReadFile(root / "launcher" / "launcher.cpp");
    CHECK(Contains(launcher, "ui.Section(\"Port hotkeys\", true)") &&
          Contains(launcher, "for (int action = OSL_KEY_FIRST_HOTKEY; action < OSL_KEY_COUNT; ++action)") &&
          Contains(launcher, "Cheats menu opens the in-game cheats"));
}

void TestKeyboardIntents() {
    using menu::KeyIntent;
    CHECK(menu::KeyIntentOf(SDLK_UP, false) == KeyIntent::Up && menu::KeyIntentOf(SDLK_W, false) == KeyIntent::Up);
    CHECK(menu::KeyIntentOf(SDLK_DOWN, false) == KeyIntent::Down && menu::KeyIntentOf(SDLK_S, false) == KeyIntent::Down);
    CHECK(menu::KeyIntentOf(SDLK_UP, true) == KeyIntent::Up && menu::KeyIntentOf(SDLK_DOWN, true) == KeyIntent::Down);
    for (const SDL_Keycode key : {SDLK_RETURN, SDLK_KP_ENTER, SDLK_SPACE}) {
        CHECK(menu::KeyIntentOf(key, false) == KeyIntent::Activate && menu::KeyIntentOf(key, true) == KeyIntent::None);
    }
    CHECK(menu::KeyIntentOf(SDLK_ESCAPE, false) == KeyIntent::Close && menu::KeyIntentOf(SDLK_ESCAPE, true) == KeyIntent::None);
    for (const SDL_Keycode key : {SDLK_A, SDLK_F2, SDLK_F11, SDLK_F10, SDLK_TAB}) CHECK(menu::KeyIntentOf(key, false) == KeyIntent::None);
}

void TestHotkeyPress() {
    using menu::ToggleAction;
    CHECK(menu::TogglePress(false, false, false) == ToggleAction::Open);
    CHECK(menu::TogglePress(false, true, false) == ToggleAction::Close);
    CHECK(menu::TogglePress(true, false, false) == ToggleAction::None && menu::TogglePress(true, true, false) == ToggleAction::None);
    // A text dialog, the save list or the loading screen: the menu does not open over them.
    CHECK(menu::TogglePress(false, false, true) == ToggleAction::None);
    CHECK(menu::TogglePress(true, false, true) == ToggleAction::None);

    const fs::path overlay_path = fs::path(OSL_SOURCE_DIR) / "gpu" / "shim" / "osl_overlay.cpp";
    const std::string overlay = ReadFile(overlay_path);
    CHECK(Contains(overlay, "const bool busy = text_entry_active || choice_active || loading;"));
    CHECK(Contains(overlay, "osl::cheats::TogglePress(repeat, cheat_open, busy)"));
    // The port's own shortcuts keep working with the menu open, and other keys never reach the game through it.
    CHECK(Contains(overlay, "code == OslSettings::Get().window_mode_key || code == SDL_SCANCODE_F2"));
    CHECK(Contains(overlay, "if (!cheat_open) {\n        return false;\n    }"));
}

void TestPadSteps() {
    using namespace menu;
    PadNav pad;
    double t = 0;
    CHECK(!pad.Poll(0, t).activate);
    NavStep step = pad.Poll(kPadCross, t += 0.016);
    CHECK(step.activate && !step.close && step.move == 0 && pad.Held() == kPadCross);
    CHECK(!pad.Poll(kPadCross, t += 0.016).activate); // held: no repeat
    CHECK(!pad.Poll(0, t += 0.016).activate && pad.Held() == 0);
    step = pad.Poll(kPadCircle, t += 0.016);
    CHECK(step.close && !step.activate && pad.Held() == kPadCircle);
    pad.Poll(0, t += 0.016);

    // Up and down step once, then repeat after the first delay.
    PadNav list;
    t = 0;
    list.Poll(0, t);
    CHECK(list.Poll(kPadUp, t += 0.016).move == -1);
    CHECK(list.Poll(kPadUp, t += 0.2).move == 0);
    CHECK(list.Poll(kPadUp, t += 0.3).move == -1);
    CHECK(list.Poll(kPadUp, t += 0.05).move == 0);
    CHECK(list.Poll(kPadUp, t += 0.1).move == -1);
    list.Poll(0, t += 0.016);
    CHECK(list.Poll(kPadDown, t += 0.016).move == 1);
    list.Poll(0, t += 0.016);

    // Buttons held when the menu opened (the game's own) are ignored until released.
    PadNav opened;
    CHECK(!opened.Poll(kPadCross | kPadDown, 0).activate && opened.Held() == 0);
    CHECK(opened.Poll(kPadCross | kPadDown, 0.5).move == 0);
    opened.Poll(kPadDown, 0.6);
    CHECK(opened.Poll(kPadDown | kPadCross, 0.7).activate);
    // Buttons the menu does not use never count.
    PadNav other;
    other.Poll(0, 0);
    const NavStep ignored = other.Poll(0x1000 | 0x8000 | 0x1, 0.1);
    CHECK(ignored.move == 0 && !ignored.activate && !ignored.close && other.Held() == 0);

    const std::string overlay = ReadFile(fs::path(OSL_SOURCE_DIR) / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(Contains(overlay, "cheat_pad.Poll(buttons, NowSeconds())") && Contains(overlay, "cheat_open && choice_input ? choice_input() : 0"));
    // Left stick up and down arrive as up and down in the runtime's own pad state.
    const std::string pad_source = ReadFile(fs::path(OSL_SOURCE_DIR) / "src" / "runtime_pad.c");
    CHECK(Contains(pad_source, "if (y<-16000) buttons|=BTN_UP;") && Contains(pad_source, "else if (y>16000) buttons|=BTN_DOWN;"));
    CHECK(Contains(pad_source, "oslgpu_choice_set_input(list_buttons);"));
}

void TestMouseAndDrawing() {
    const std::string overlay = ReadFile(fs::path(OSL_SOURCE_DIR) / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(Contains(overlay, "ImGui::Selectable(\"##row\"") && Contains(overlay, "ActivateCheatRow(size_t(clicked));"));
    CHECK(Contains(overlay, "io.AddMousePosEvent(") && Contains(overlay, "io.AddMouseButtonEvent(button") &&
          Contains(overlay, "io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);"));
    CHECK(Contains(overlay, "osl::cheats::kMenuTitle") && Contains(overlay, "osl::cheats::kMenuNote") && Contains(overlay, "CheatCloseHint()"));
    CHECK(Contains(overlay, "ImGuiWindowFlags_NoNav") && Contains(overlay, "AlwaysVerticalScrollbar"));
    // The existing context, font and accent: no second ImGui.
    CHECK(Contains(overlay, "AccentColor()") && std::count(overlay.begin(), overlay.end(), '\n') > 100);
    size_t contexts = 0;
    for (size_t at = overlay.find("ImGui::CreateContext"); at != std::string::npos; at = overlay.find("ImGui::CreateContext", at + 1)) ++contexts;
    CHECK(contexts == 1);
    size_t fonts = 0;
    for (size_t at = overlay.find("AddFontFromMemoryTTF"); at != std::string::npos; at = overlay.find("AddFontFromMemoryTTF", at + 1)) ++fonts;
    CHECK(fonts == 1);
    // Input capture and drawing follow the open menu.
    CHECK(Contains(overlay, "return cheat_open || text_entry_active || choice_active;"));
    CHECK(Contains(overlay, "loading || background_total || cheat_open || text_entry_active"));
    CHECK(Contains(overlay, "PollCheatPad(cheat_buttons);\n        CheatMenu();"));
    CHECK(Contains(overlay, "return HandleCheatEvent(event);"));
    const std::string pad_source = ReadFile(fs::path(OSL_SOURCE_DIR) / "src" / "runtime_pad.c");
    CHECK(Contains(pad_source, "if (oslgpu_ui_captures_input()) { s->hold_after_capture=1; return; }") &&
          Contains(pad_source, "if (oslgpu_ui_captures_input()) return; /* game dialog open: neutral input */"));
}

// The frames of a menu session: what the game would see while the menu is open or closing.
struct Session {
    menu::PadNav pad;
    menu::CloseGate gate;
    std::set<int> keys;
    bool open = true;
    int activations = 0;
    double now = 0;

    // True while the menu still holds the game's input.
    bool Frame(uint32_t buttons) {
        now += 0.016;
        const menu::NavStep step = pad.Poll(buttons, now);
        if (!gate.requested()) {
            activations += step.activate ? 1 : 0;
            if (step.close) {
                gate.Request();
            }
        } else if (gate.Step(!keys.empty() || pad.Held() != 0, now)) {
            open = false;
        }
        return open;
    }
};

void TestCloseGate() {
    // Cross activates a row, then Circle closes: neither reaches the game until both are let go.
    Session pad;
    CHECK(pad.Frame(0));
    CHECK(pad.Frame(menu::kPadCross) && pad.activations == 1);
    CHECK(pad.Frame(menu::kPadCross) && pad.activations == 1);
    CHECK(pad.Frame(0));
    CHECK(pad.Frame(menu::kPadCircle) && pad.gate.requested());
    CHECK(pad.Frame(menu::kPadCircle));
    CHECK(pad.Frame(menu::kPadCircle));
    CHECK(!pad.Frame(0) && !pad.open); // released: the input goes back to the game

    // Cross closing at the same time as a held Cross: nothing leaks either.
    Session cross;
    cross.Frame(0);
    cross.Frame(menu::kPadCross);
    cross.gate.Request();
    CHECK(cross.Frame(menu::kPadCross));
    CHECK(!cross.Frame(0));

    // F10 held through the close: the key counts as held until it comes up, and a held key never reopens.
    Session key;
    key.keys.insert(SDL_SCANCODE_F10);
    key.gate.Request();
    CHECK(key.Frame(0) && key.Frame(0));
    key.keys.clear();
    CHECK(!key.Frame(0));
    CHECK(menu::TogglePress(true, false, false) == menu::ToggleAction::None);
    CHECK(menu::TogglePress(false, false, false) == menu::ToggleAction::Open);

    // A button that is never reported released cannot hold the game's input for ever.
    Session stuck;
    stuck.keys.insert(SDL_SCANCODE_RETURN);
    stuck.gate.Request();
    bool held = true;
    int frames = 0;
    while (held && frames < 200) {
        held = stuck.Frame(0);
        ++frames;
    }
    CHECK(!held && stuck.now >= menu::kReleaseTimeoutSeconds && frames < 100);
    CHECK(menu::InputReleased(false, 0, 1) && !menu::InputReleased(true, 0.5, 1) && menu::InputReleased(true, 1.0, 1));
    menu::CloseGate idle;
    CHECK(!idle.Step(false, 1.0) && !idle.requested());

    const std::string overlay = ReadFile(fs::path(OSL_SOURCE_DIR) / "gpu" / "shim" / "osl_overlay.cpp");
    CHECK(Contains(overlay, "cheat_close.Step(!cheat_keys_held.empty() || cheat_pad.Held() != 0, NowSeconds())"));
    CHECK(Contains(overlay, "cheat_keys_held.insert(int(code))") && Contains(overlay, "cheat_keys_held.erase(int(code))"));
}

void TestHealthRow() {
    const auto rows = cheats::MenuRows();
    CHECK(rows[4].cheat == OSL_CHEAT_INFINITE_POW && rows[5].cheat == OSL_CHEAT_INFINITE_HEALTH &&
          rows[6].cheat == OSL_CHEAT_INFINITE_ARROWS);
    // The sixth row, reached by stepping, toggles Infinite Health and nothing else; a click selects it too.
    alignas(8) volatile uint8_t flags[OSL_CHEAT_COUNT] = {};
    runtime_cheats_start(0);
    runtime_cheats_set_available(kAll, flags);
    cheats::MenuList list;
    const int count = static_cast<int>(cheats::kMenuRowCount);
    list.Open();
    for (int i = 0; i < 5; ++i) list.Step(1, count);
    CHECK(list.selected == 5);
    CHECK(runtime_cheat_toggle(rows[size_t(list.selected)].cheat) == 1);
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(flags[c] == (c == OSL_CHEAT_INFINITE_HEALTH ? 1 : 0));
    list.Click(5, count);
    CHECK(runtime_cheat_toggle(rows[size_t(list.selected)].cheat) == 0 && flags[OSL_CHEAT_INFINITE_HEALTH] == 0);
    // Saved On: starts On, the session turns it Off, a new session brings it back.
    runtime_cheats_start(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
    CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_HEALTH) && flags[OSL_CHEAT_INFINITE_HEALTH] == 1);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH) == 0 && flags[OSL_CHEAT_INFINITE_HEALTH] == 0);
    runtime_cheats_start(osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
    CHECK(runtime_cheat_enabled(OSL_CHEAT_INFINITE_HEALTH) && flags[OSL_CHEAT_INFINITE_HEALTH] == 1);
    // Without its hooks it is Unavailable and cannot be switched; the others still can.
    runtime_cheats_set_available(kAll & ~osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH), flags);
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_HEALTH) == -1 && flags[OSL_CHEAT_INFINITE_HEALTH] == 0);
    CHECK(std::string(cheats::StateText(cheats::StateOf(runtime_cheat_available(OSL_CHEAT_INFINITE_HEALTH) != 0, true))) ==
          "Unavailable");
    CHECK(runtime_cheat_toggle(OSL_CHEAT_INFINITE_ARROWS) == 1 && flags[OSL_CHEAT_INFINITE_ARROWS] == 1);
    runtime_cheats_set_available(0, nullptr);
    runtime_cheats_start(0);

    // Saved in osl.ini like the others, Off by default and after Reset.
    const fs::path file = TempDir("cheats-health") / "osl.ini";
    {
        std::ofstream out(file, std::ios::trunc);
        out << "cheat_infinite_health = 1\n";
    }
    CHECK(osl_cheats_enabled(file.string().c_str()) == osl_cheat_bit(OSL_CHEAT_INFINITE_HEALTH));
    LauncherSettings settings(file);
    CHECK(settings.GetBool("cheat_infinite_health", false));
    settings.ResetToDefaults();
    CHECK(settings.Save() && osl_cheats_enabled(file.string().c_str()) == 0 &&
          Contains(ReadFile(file), "cheat_infinite_health"));
}

void TestThreadSafety() {
    alignas(8) volatile uint8_t flags[OSL_CHEAT_COUNT] = {};
    runtime_cheats_start(0);
    runtime_cheats_set_available(kAll, flags);
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0}, reads{0};
    std::thread game([&] {
        while (!stop.load()) {
            for (int c = 0; c < OSL_CHEAT_COUNT; ++c) {
                const uint8_t flag = __atomic_load_n(&flags[c], __ATOMIC_ACQUIRE);
                const int enabled = runtime_cheat_enabled(c);
                bad += flag > 1 || enabled < 0 || enabled > 1;
                ++reads;
            }
        }
    });
    while (reads == 0) std::this_thread::yield();
    for (int i = 0; i < 20000; ++i) runtime_cheat_toggle(i % OSL_CHEAT_COUNT);
    const int seen = reads;
    while (reads <= seen) std::this_thread::yield();
    stop = true;
    game.join();
    CHECK(bad == 0 && reads > 0);
    for (int c = 0; c < OSL_CHEAT_COUNT; ++c) CHECK(flags[c] == runtime_cheat_enabled(c));
    runtime_cheats_set_available(0, nullptr);
    runtime_cheats_start(0);
}

} // namespace

void RunMenuTests() {
    TestSession();
    TestRuntimeState();
    TestSessionNeverWritesSettings();
    TestMenuModel();
    TestMenuScaling();
    TestHotkeys();
    TestKeyboardIntents();
    TestHotkeyPress();
    TestPadSteps();
    TestMouseAndDrawing();
    TestCloseGate();
    TestHealthRow();
    TestThreadSafety();
}

} // namespace cheat_test
