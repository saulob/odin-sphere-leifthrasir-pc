// The input rules the in-game menus share: when the game gets its input back after a menu closes, a key that opens and
// closes a menu, and the controller's steps through a list. Logic only.
#pragma once

#include <cstdint>

#include <SDL3/SDL_keycode.h>

namespace osl::menu {

// A button that is never reported released (a controller unplugged as the menu closes) cannot block the game forever.
inline constexpr double kReleaseTimeoutSeconds = 1.0;

inline bool InputReleased(bool anything_held, double waited_seconds, double timeout_seconds) {
    return !anything_held || waited_seconds >= timeout_seconds;
}

// Closing the menu starts a wait, and the game gets its input back only after what was pressed in the menu is
// released, so the closing press never also reaches the game.
class CloseGate {
public:
    void Request() { requested_ = true; }
    bool requested() const { return requested_; }

    // Once per frame. True when the menu may go and the game may read its input again.
    bool Step(bool anything_held, double now) {
        if (!requested_) {
            return false;
        }
        if (since_ < 0.0) {
            since_ = now;
        }
        return InputReleased(anything_held, now - since_, kReleaseTimeoutSeconds);
    }

private:
    bool requested_ = false;
    double since_ = -1.0;
};

enum class ToggleAction { None, Open, Close };

// A menu's key: a held key repeats and only the first press counts. `busy` is a game dialog or the loading screen,
// which the menu never opens over.
constexpr ToggleAction TogglePress(bool repeat, bool open, bool busy) {
    if (repeat) {
        return ToggleAction::None;
    }
    if (open) {
        return ToggleAction::Close;
    }
    return busy ? ToggleAction::None : ToggleAction::Open;
}

enum class KeyIntent { None, Up, Down, Activate, Close };

// What a key does in an open menu. A held arrow repeats; accepting and closing count once per press.
inline KeyIntent KeyIntentOf(SDL_Keycode key, bool repeat) {
    if (key == SDLK_UP || key == SDLK_W) {
        return KeyIntent::Up;
    }
    if (key == SDLK_DOWN || key == SDLK_S) {
        return KeyIntent::Down;
    }
    if (repeat) {
        return KeyIntent::None;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
        return KeyIntent::Activate;
    }
    return key == SDLK_ESCAPE ? KeyIntent::Close : KeyIntent::None;
}

// The PS4 pad bits the game's own button state uses.
inline constexpr uint32_t kPadUp = 0x10, kPadDown = 0x40, kPadCircle = 0x2000, kPadCross = 0x4000;
inline constexpr uint32_t kPadMenuMask = kPadUp | kPadDown | kPadCircle | kPadCross;

struct NavStep {
    int move = 0; // -1 up, 1 down
    bool activate = false;
    bool close = false;
};

// Steps from the controller's buttons, polled every frame. Buttons held when the menu opened are ignored until they are
// released; holding up or down repeats.
class PadNav {
public:
    NavStep Poll(uint32_t buttons, double now) {
        if (!started_) {
            started_ = true;
            ignored_ = buttons & kPadMenuMask;
        }
        ignored_ &= buttons;
        const uint32_t current = buttons & kPadMenuMask & ~ignored_;
        const uint32_t pressed = current & ~previous_;
        previous_ = current;
        NavStep step;
        step.move = (pressed & kPadUp) ? -1 : (pressed & kPadDown) ? 1 : 0;
        if (step.move) {
            repeat_at_ = now + 0.4;
        } else if ((current & (kPadUp | kPadDown)) && now >= repeat_at_) {
            step.move = (current & kPadUp) ? -1 : 1;
            repeat_at_ = now + 0.12;
        }
        step.activate = (pressed & kPadCross) != 0;
        step.close = (pressed & kPadCircle) != 0;
        return step;
    }

    // The menu buttons pressed since it opened and not yet released.
    uint32_t Held() const { return previous_; }

private:
    bool started_ = false;
    uint32_t ignored_ = 0, previous_ = 0;
    double repeat_at_ = 0.0;
};

} // namespace osl::menu
