// What the launcher saved and what the in-game menu changed for this session, per cheat and for the Experience
// Multiplier. Logic only.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "osl_cheats.h"

namespace osl::cheats {

// The menu only records differences from the saved value, so the saved value is never altered and a cheat toggled back
// follows the saved value again.
class SessionCheats {
public:
    void Start(unsigned saved_mask) {
        saved_.store(saved_mask & OSL_CHEAT_ALL, std::memory_order_relaxed);
        for (std::atomic<uint8_t>& state : states_) {
            state.store(kFollowsSaved, std::memory_order_relaxed);
        }
    }

    bool Saved(OslCheat cheat) const { return (saved_.load(std::memory_order_relaxed) & osl_cheat_bit(cheat)) != 0; }

    bool Effective(OslCheat cheat) const {
        const uint8_t state = states_[cheat].load(std::memory_order_relaxed);
        return state == kFollowsSaved ? Saved(cheat) : state == kOn;
    }

    // Returns the new state.
    bool Toggle(OslCheat cheat) {
        const bool enabled = !Effective(cheat);
        states_[cheat].store(enabled == Saved(cheat) ? kFollowsSaved : (enabled ? kOn : kOff), std::memory_order_relaxed);
        return enabled;
    }

private:
    static constexpr uint8_t kFollowsSaved = 0;
    static constexpr uint8_t kOff = 1;
    static constexpr uint8_t kOn = 2;

    std::atomic<unsigned> saved_{0};
    std::array<std::atomic<uint8_t>, OSL_CHEAT_COUNT> states_{};
};

// The Experience Multiplier the same way: the menu cycles through the choices, and reaching the saved one again
// follows the saved value.
class SessionExperience {
public:
    void Start(int saved) {
        saved_.store(osl_experience_listed(saved), std::memory_order_relaxed);
        override_.store(kFollowsSaved, std::memory_order_relaxed);
    }

    int Saved() const { return saved_.load(std::memory_order_relaxed); }

    bool FollowsSaved() const { return override_.load(std::memory_order_relaxed) == kFollowsSaved; }

    int Effective() const {
        const int value = override_.load(std::memory_order_relaxed);
        return value == kFollowsSaved ? Saved() : value;
    }

    // Returns the new value.
    int Cycle() {
        const int next = osl_experience_next(Effective());
        override_.store(next == Saved() ? kFollowsSaved : next, std::memory_order_relaxed);
        return next;
    }

private:
    static constexpr int kFollowsSaved = 0;

    std::atomic<int> saved_{OSL_EXPERIENCE_OFF};
    std::atomic<int> override_{kFollowsSaved};
};

} // namespace osl::cheats
