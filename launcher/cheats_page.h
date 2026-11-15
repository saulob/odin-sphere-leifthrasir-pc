// The launcher's Cheats page: its text and rows, from the one cheat table (osl_cheats.h).
#pragma once

#include <cstring>
#include <vector>

#include "game_files.h"
#include "game_profile.h"
#include "osl_cheats.h"

namespace osl {

inline constexpr char kCheatsSubtitle[] = "Optional gameplay modifiers";
inline constexpr char kCheatsHotkeyName[] = "F10";
inline constexpr char kCheatsHelp[] =
    "Sets the state used when the game starts. Press F10 while playing to change cheats for the current session. No "
    "game file is modified; a cheat whose code is not found in this game build shows as Unavailable.";
inline constexpr char kCheatsUnavailableHelp[] = "Cheats are available once a supported game build is installed.";
inline constexpr bool kCheatDefault = false;
inline constexpr char kExperienceSection[] = "Progression";

inline bool CheatsAvailable(GameFileState files, const GameBuild* build) {
    return files == GameFileState::kValid && build != nullptr;
}

// A section heading, a cheat's Off / On row, or the Experience Multiplier's choices, always the last row.
struct CheatPageRow {
    const char* heading = nullptr;
    const OslCheatInfo* cheat = nullptr;
    const OslSettingInfo* setting = nullptr;
};

inline std::vector<CheatPageRow> CheatPageRows() {
    std::vector<CheatPageRow> rows;
    const char* section = nullptr;
    for (const OslCheatInfo& cheat : osl_cheats) {
        if (!section || std::strcmp(section, cheat.section) != 0) {
            section = cheat.section;
            rows.push_back({section, nullptr});
        }
        rows.push_back({nullptr, &cheat});
    }
    rows.push_back({kExperienceSection, nullptr});
    rows.push_back({nullptr, nullptr, &osl_experience_multiplier});
    return rows;
}

} // namespace osl
