#pragma once

#include "trg/launcher.h"

#include "updater/update_controller.h"

namespace trg::update {

enum class UpdateUiAction { kNone, kDownload, kRetry, kRestart };

// `reserved_left`: pixels from the window's left edge that the banner leaves free (the header's title block).
UpdateUiAction DrawUpdateBanner(const UpdateSnapshot& snapshot, const trg::Theme& theme, const trg::Fonts& fonts,
                                float reserved_left = 0.0f);

}  // namespace trg::update
