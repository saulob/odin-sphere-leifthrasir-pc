// The in-game cheat menu's model: what it lists and says, how it scales, and which row is selected. Logic only.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

#include "osl_cheats.h"
#include "osl_menu_input.h"

namespace osl::cheats {

inline constexpr char kMenuTitle[] = "CHEATS";
inline constexpr char kMenuNote[] = "Not saved to the launcher";

enum class RowState { Off, On, Unavailable };

constexpr RowState StateOf(bool available, bool enabled) {
    return !available ? RowState::Unavailable : (enabled ? RowState::On : RowState::Off);
}

constexpr const char* StateText(RowState state) {
    return state == RowState::On ? "On" : (state == RowState::Off ? "Off" : "Unavailable");
}

// "F10 Close", with the key the player bound.
inline std::string CloseHint(const std::string& key) {
    return key + " Close";
}

// The line the run log gets when the menu changes a cheat.
inline std::string ToggleLogLine(const char* label, bool on) {
    return std::string("Cheats: ") + label + (on ? " On" : " Off") + " (this session)";
}

enum class RowKind { Toggle, Experience };

struct MenuRow {
    OslCheat cheat; // OSL_CHEAT_COUNT for the Experience Multiplier
    const char* label;
    RowKind kind = RowKind::Toggle;
};

inline constexpr size_t kMenuRowCount = OSL_CHEAT_COUNT + 1;

// One row per cheat, in the table's order, then the Experience Multiplier.
inline std::array<MenuRow, kMenuRowCount> MenuRows() {
    std::array<MenuRow, kMenuRowCount> rows{};
    for (size_t i = 0; i < OSL_CHEAT_COUNT; ++i) {
        rows[i] = {osl_cheats[i].cheat, osl_cheats[i].label};
    }
    rows[OSL_CHEAT_COUNT] = {OSL_CHEAT_COUNT, osl_experience_multiplier.label, RowKind::Experience};
    return rows;
}

// The Experience Multiplier row's value: its choice, or Unavailable.
inline const char* ExperienceText(bool available, int multiplier) {
    return available ? osl_experience_label(multiplier) : StateText(RowState::Unavailable);
}

// Lit like an On cheat whenever it multiplies.
constexpr bool ExperienceLit(bool available, int multiplier) {
    return available && multiplier != OSL_EXPERIENCE_OFF;
}

inline std::string ExperienceLogLine(int multiplier) {
    return std::string("Cheats: ") + osl_experience_multiplier.label + " " + osl_experience_label(multiplier) +
           " (this session)";
}

// The panel is kMenuWidthEm wide. It grows with the display's content scale, a step below it (kMenuScaleStep) and never
// under kMenuMinScale, and shrinks to fit kMenuMaxWidthShare of the screen down to the 1x floor.
inline constexpr float kMenuWidthEm = 21.0f;
inline constexpr float kMenuMaxScale = 3.0f;
inline constexpr float kMenuScaleStep = 0.75f;
inline constexpr float kMenuMinScale = 1.25f;
inline constexpr float kMenuMaxWidthShare = 0.45f;

constexpr float MenuScale(float display_scale, float screen_width, float em_pixels) {
    const float display = display_scale < 1.0f ? 1.0f : (display_scale > kMenuMaxScale ? kMenuMaxScale : display_scale);
    const float stepped = display * kMenuScaleStep;
    const float wanted = stepped < kMenuMinScale ? kMenuMinScale : stepped;
    const float fits = kMenuMaxWidthShare * screen_width / (kMenuWidthEm * em_pixels);
    return wanted < fits ? wanted : (fits > 1.0f ? fits : 1.0f);
}

// The scrollable rows only: each row is row_height tall, with item_spacing between them.
constexpr float MenuBodyHeight(size_t row_count, float row_height, float item_spacing) {
    return row_count == 0 ? 0.0f : row_count * row_height + (row_count - 1) * item_spacing;
}

// The row content can be up to a pixel shorter than the calculated height because ImGui truncates layout positions.
constexpr float kMenuScrollbarTolerance = 1.0f;

constexpr bool MenuNeedsScrollbar(float body_height, float child_height) {
    return body_height > child_height + kMenuScrollbarTolerance;
}

// The least scroll movement that keeps [row_top, row_bottom] inside [view_top, view_bottom].
constexpr float MenuScrollToShowRow(float row_top, float row_bottom, float view_top, float view_bottom) {
    if (row_top < view_top) {
        return row_top - view_top;
    }
    if (row_bottom > view_bottom) {
        return row_bottom - view_bottom;
    }
    return 0.0f;
}

constexpr float MenuClampScroll(float scroll, float max_scroll) {
    const float top = max_scroll > 0.0f ? max_scroll : 0.0f;
    return scroll < 0.0f ? 0.0f : (scroll > top ? top : scroll);
}

// The selected row and when the list scrolls to it. Only a controller or keyboard step brings the selection into view;
// the mouse wheel, the scrollbar and clicks leave the scroll where the user put it.
struct MenuList {
    int selected = 0;
    bool reveal = true;

    void Open() {
        selected = 0;
        reveal = true;
    }

    void Step(int step, int count) {
        selected = count <= 0 ? 0 : std::clamp(selected + step, 0, count - 1);
        reveal = true;
    }

    void Click(int row, int count) { selected = count <= 0 ? 0 : std::clamp(row, 0, count - 1); }

    void Clamp(int count) { selected = count <= 0 ? 0 : std::clamp(selected, 0, count - 1); }

    float Resolve(float scroll, float max_scroll, float row_top, float row_bottom, float view_top, float view_bottom) {
        if (!reveal) {
            return MenuClampScroll(scroll, max_scroll);
        }
        reveal = false;
        return MenuClampScroll(scroll + MenuScrollToShowRow(row_top, row_bottom, view_top, view_bottom), max_scroll);
    }
};

using menu::CloseGate;
using menu::PadNav;
using menu::TogglePress;

} // namespace osl::cheats
