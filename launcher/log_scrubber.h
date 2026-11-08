// Removes what would identify a player or this PC from text that is about to leave it. Everything else stays: frame
// counts, addresses in guest memory, hashes of game files and driver versions are what a report is for.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace osl {

// What only this PC knows. A name may be listed twice: logs can hold it in UTF-8 and in the Windows code page.
struct PrivateFacts {
    std::vector<std::string> user_names;
    std::vector<std::string> machine_names;
    std::vector<std::string> profile_folders;
    std::vector<std::string> install_folders;
};

// The running system's facts and `install_folders`.
PrivateFacts GatherPrivateFacts(const std::vector<std::filesystem::path>& install_folders);

// Valid UTF-8 with LF line ends and no other control characters than tabs, whatever `text` held.
std::string ScrubText(std::string_view text, const PrivateFacts& facts);

} // namespace osl
