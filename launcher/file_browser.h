// The folder listing behind the launcher's own file chooser, used where the desktop offers none (Linux without zenity
// or kdialog) and wherever OSL_FILE_CHOOSER=builtin asks for it. No window needed.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace osl {

struct FolderEntry {
    std::string name; // UTF-8
    bool directory = false;
    uint64_t size = 0;
};

struct FolderListing {
    std::filesystem::path folder;
    std::vector<FolderEntry> entries;
    bool readable = false;
};

// Folders first, then files, each sorted without regard to case. A file is listed when its extension is `extension`
// (".pkg"; empty lists every file). Names that start with a dot are listed only when `show_hidden` is set.
FolderListing ListFolder(const std::filesystem::path& folder, std::string_view extension, bool show_hidden);

struct Place {
    std::string label;
    std::filesystem::path path;
};

// Folders worth jumping to that exist: the program's folder, the home folder and its usual subfolders, and mounted
// drives such as the Steam Deck's SD card.
std::vector<Place> StandardPlaces(const std::filesystem::path& program_folder);

// The first of the places that holds a file with `extension`, else the first place, else the current folder.
std::filesystem::path StartingFolder(const std::vector<Place>& places, std::string_view extension);

// "3.4 GB", "512 KB".
std::string FormatSize(uint64_t bytes);

// "native", "builtin" or "" from OSL_FILE_CHOOSER.
std::string_view FileChooserPreference();

} // namespace osl
