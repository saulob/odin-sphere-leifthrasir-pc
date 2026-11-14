#include "pad_capture.h"

#include <algorithm>

#include <SDL3/SDL.h>

namespace osl {
namespace {

uint8_t TriggerValue(SDL_Gamepad* gamepad, SDL_GamepadAxis axis) {
    return static_cast<uint8_t>(std::clamp(SDL_GetGamepadAxis(gamepad, axis) >> 7, 0, 255));
}

} // namespace

bool PadCapture::Begin(OslPadAction action, size_t controllers) {
    if (controllers == 0) {
        return false;
    }
    action_ = action;
    armed_ = false;
    return true;
}

PadCaptureResult PadCapture::Update(std::span<const OslPadInput> controllers, bool escape) {
    using Kind = PadCaptureResult::Kind;
    if (!action_) {
        return {Kind::kCancelled, OSL_PAD_SOURCE_NONE};
    }
    if (escape) {
        Cancel();
        return {Kind::kCancelled, OSL_PAD_SOURCE_NONE};
    }
    if (controllers.empty()) {
        Cancel();
        return {Kind::kNoController, OSL_PAD_SOURCE_NONE};
    }
    int pressed = OSL_PAD_SOURCE_NONE;
    for (const OslPadInput& controller : controllers) {
        pressed = osl_pad_first_pressed(&controller);
        if (pressed != OSL_PAD_SOURCE_NONE) {
            break;
        }
    }
    if (pressed == OSL_PAD_SOURCE_NONE) {
        armed_ = true;
        return {};
    }
    if (!armed_) {
        return {};
    }
    Cancel();
    return {Kind::kAssigned, pressed};
}

size_t ControllerReader::Connected() {
    int count = 0;
    SDL_free(SDL_GetGamepads(&count));
    return static_cast<size_t>(count);
}

std::vector<OslPadInput> ControllerReader::Read() {
    for (auto it = open_.begin(); it != open_.end();) {
        if (SDL_GamepadConnected(*it)) {
            ++it;
        } else {
            SDL_CloseGamepad(*it);
            it = open_.erase(it);
        }
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; ids && i < count; ++i) {
        SDL_Gamepad* gamepad = SDL_OpenGamepad(ids[i]);
        if (!gamepad) {
            continue;
        }
        if (std::find(open_.begin(), open_.end(), gamepad) == open_.end()) {
            open_.push_back(gamepad);
        } else {
            SDL_CloseGamepad(gamepad);
        }
    }
    SDL_free(ids);
    std::vector<OslPadInput> samples;
    for (SDL_Gamepad* gamepad : open_) {
        OslPadInput sample{};
        for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button) {
            if (SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(button))) {
                sample.buttons |= 1u << button;
            }
        }
        sample.left_trigger = TriggerValue(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        sample.right_trigger = TriggerValue(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        samples.push_back(sample);
    }
    return samples;
}

void ControllerReader::Close() {
    for (SDL_Gamepad* gamepad : open_) {
        SDL_CloseGamepad(gamepad);
    }
    open_.clear();
}

} // namespace osl
