#include "trophy_content.h"

#include "trophies/trophy_store.h"
#include "trophy_translation.h"

namespace fs = std::filesystem;

namespace osl {
namespace {

constexpr int kTrophyUser = 1;

} // namespace

TrophyPageContent LoadTrophyPage(const fs::path& app0, const fs::path& user_dir) {
    TrophyPageContent content;
    if (app0.empty()) {
        return content;
    }
    content.key_configured = trophies::LoadTrophyKey(user_dir).has_value();
    const trophies::PackResult pack = trophies::PrepareTrophyPack(app0, user_dir);
    content.pack = pack.status;
    content.np_comm_id = pack.np_comm_id;
    content.state = TrophyPageState::kNoList;
    if (pack.np_comm_id.empty()) {
        return content;
    }
    trophies::TrophyStore store(trophies::TrophyDirectory(user_dir, pack.np_comm_id), pack.np_comm_id, kTrophyUser);
    store.Load();
    if (const std::optional<trophies::TrophySet> set = trophies::LoadTrophySet(user_dir, pack.np_comm_id)) {
        content.view = trophies::BuildTrophyView(*set, store);
        content.view_pt_br = trophies::TranslateTrophyView(content.view, *set, trophies::PortugueseBrazilTrophyText());
        content.state = TrophyPageState::kList;
    } else {
        for (const auto& [id, time] : store.Unlocked()) {
            content.saved_unlocks.push_back({id, time});
        }
    }
    return content;
}

const trophies::TrophyView& DisplayedTrophyView(const TrophyPageContent& content, bool portuguese_brazil) {
    return portuguese_brazil ? content.view_pt_br : content.view;
}

} // namespace osl
