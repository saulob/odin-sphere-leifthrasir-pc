#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "trophies/trophy_pack.h"
#include "trophies/trophy_view.h"

namespace osl {

enum class TrophyPageState { kNoGame, kNoList, kList };

struct SavedUnlock {
    int id = 0;
    int64_t unix_time = 0;
};

struct TrophyPageContent {
    bool key_configured = false;
    TrophyPageState state = TrophyPageState::kNoGame;
    trophies::PackStatus pack = trophies::PackStatus::kNoNpBind;
    std::string np_comm_id;
    std::vector<SavedUnlock> saved_unlocks;
    trophies::TrophyView view;
    trophies::TrophyView view_pt_br; // view with the Portuguese (Brazil) trophy text where it matches the pack
};

TrophyPageContent LoadTrophyPage(const std::filesystem::path& app0, const std::filesystem::path& user_dir);

const trophies::TrophyView& DisplayedTrophyView(const TrophyPageContent& content, bool portuguese_brazil);

} // namespace osl
