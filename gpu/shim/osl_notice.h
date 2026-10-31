// SPDX-License-Identifier: GPL-2.0-or-later
// Startup Gameplay hints ("Skip Intro: On"): pure state, no ImGui/SDL, so it is covered by the
// headless tests. The overlay (osl_overlay.cpp) owns the queue and draws whatever is Visible().
#pragma once

#include <string>

namespace OslNotice {

// The frame counter is drawn once it is on and has a measured rate; shared with the startup
// notice gate below so both agree on what "ready" means.
constexpr bool FrameCounterVisible(bool enabled, double frame_ms_avg) {
    return enabled && frame_ms_avg > 0.0;
}

// A startup notice waits for the frame counter when it is enabled, so both appear on the same
// frame instead of the notice flashing before the counter has a reading.
constexpr bool StartupNoticeReady(bool frame_counter_enabled, bool frame_counter_visible) {
    return !frame_counter_enabled || frame_counter_visible;
}

// One startup confirmation ("Skip Intro: On"), queued at most once per launch and shown for a
// fixed duration starting the first time it is Visible() with `ready` true.
class StartupNotice {
public:
    void QueueStartup(std::string text) {
        if (queued_) {
            return;
        }
        queued_ = true;
        text_ = std::move(text);
        state_ = State::kQueued;
    }

    // `now` and `seconds` in the same unit (seconds); `ready` gates leaving kQueued.
    bool Visible(double now, bool ready, double seconds) {
        if (state_ == State::kQueued && ready) {
            shown_at_ = now;
            state_ = State::kShown;
        }
        return state_ == State::kShown && now - shown_at_ < seconds;
    }

    // Still queued, or shown and not yet timed out: the overlay needs another frame either way.
    bool Pending(double now, double seconds) const {
        return state_ == State::kQueued || (state_ == State::kShown && now - shown_at_ < seconds);
    }

    bool queued() const { return queued_; }
    const std::string& text() const { return text_; }

private:
    enum class State { kHidden, kQueued, kShown } state_ = State::kHidden;
    bool queued_ = false;
    std::string text_;
    double shown_at_ = 0.0;
};

} // namespace OslNotice
