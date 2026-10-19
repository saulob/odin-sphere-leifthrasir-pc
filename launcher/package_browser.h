// The launcher's own "choose a file" window: a modal list of folders and .pkg files, usable with the mouse, the
// keyboard and a controller. It stands in for the desktop's chooser where there is none.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "file_browser.h"
#include "trg/launcher.h"

namespace osl {

class PackageBrowser {
public:
    // Starts at the folder that holds a .pkg (the program's folder, Downloads, a drive...), else the program's folder.
    void Open(const std::filesystem::path& program_folder);
    bool active() const { return active_; }

    // Draws the window while it is open; returns the file once the player chose one. `font` is nullptr for the
    // current font.
    std::optional<std::filesystem::path> Draw(ImFont* font, float font_size, const trg::Theme& theme);

private:
    void Go(const std::filesystem::path& folder);

    bool active_ = false;
    bool popup_open_ = false;
    bool focus_list_ = false;
    bool show_all_ = false;
    std::filesystem::path folder_;
    std::vector<Place> places_;
    FolderListing listing_;
};

} // namespace osl
