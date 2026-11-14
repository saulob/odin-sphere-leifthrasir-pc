// Waiting for a controller button on the Controls page: which button or trigger the user presses next.
#pragma once

#include <optional>
#include <span>
#include <vector>

#include "osl_pad.h"

struct SDL_Gamepad;

namespace osl {

struct PadCaptureResult {
    enum class Kind { kWaiting, kAssigned, kCancelled, kNoController };
    Kind kind = Kind::kWaiting;
    int source = OSL_PAD_SOURCE_NONE;
};

class PadCapture {
public:
    // False when no controller is connected: nothing starts and the binding stays as it is.
    bool Begin(OslPadAction action, size_t controllers);
    void Cancel() { action_.reset(); }
    bool active() const { return action_.has_value(); }
    OslPadAction action() const { return *action_; }

    // One frame of the controllers' state. A button held when the capture began (the one that opened the row) is
    // ignored until everything has been released.
    PadCaptureResult Update(std::span<const OslPadInput> controllers, bool escape);

private:
    std::optional<OslPadAction> action_;
    bool armed_ = false;
};

// The controllers SDL reports, opened for the time a binding waits.
class ControllerReader {
public:
    ~ControllerReader() { Close(); }
    static size_t Connected();
    std::vector<OslPadInput> Read();
    void Close();

private:
    std::vector<SDL_Gamepad*> open_;
};

} // namespace osl
