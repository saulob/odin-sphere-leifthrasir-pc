#include "runtime_cheats.h"

#include <atomic>

#include "osl_cheat_session.h"

namespace {

osl::cheats::SessionCheats session;
std::atomic<unsigned> available{0};
std::atomic<volatile uint8_t*> hook_flags{nullptr};
osl::cheats::SessionExperience experience;
std::atomic<bool> experience_available{false};
std::atomic<volatile uint8_t*> experience_flag{nullptr};

bool Valid(int cheat) {
    return cheat >= 0 && cheat < OSL_CHEAT_COUNT;
}

bool Enabled(OslCheat cheat) {
    return (available.load(std::memory_order_relaxed) & osl_cheat_bit(cheat)) != 0 && session.Effective(cheat);
}

void Publish() {
    volatile uint8_t* flags = hook_flags.load(std::memory_order_acquire);
    for (int c = 0; flags && c < OSL_CHEAT_COUNT; ++c) {
        __atomic_store_n(&flags[c], Enabled(static_cast<OslCheat>(c)) ? 1 : 0, __ATOMIC_RELEASE);
    }
}

int Multiplier() {
    return experience_available.load(std::memory_order_relaxed) ? experience.Effective() : OSL_EXPERIENCE_OFF;
}

void PublishExperience() {
    volatile uint8_t* flag = experience_flag.load(std::memory_order_acquire);
    if (!flag) {
        return;
    }
    const int value = Multiplier();
    __atomic_store_n(flag, static_cast<uint8_t>(value == OSL_EXPERIENCE_OFF ? 0 : value), __ATOMIC_RELEASE);
}

} // namespace

extern "C" void runtime_cheats_start(unsigned saved_mask) {
    session.Start(saved_mask);
    Publish();
}

extern "C" void runtime_cheats_set_available(unsigned available_mask, volatile uint8_t* flags) {
    available.store(available_mask & OSL_CHEAT_ALL, std::memory_order_relaxed);
    hook_flags.store(flags, std::memory_order_release);
    Publish();
}

extern "C" unsigned runtime_cheats_available_mask(void) {
    return available.load(std::memory_order_relaxed);
}

extern "C" int runtime_cheat_available(int cheat) {
    return Valid(cheat) && (runtime_cheats_available_mask() & osl_cheat_bit(static_cast<OslCheat>(cheat))) != 0;
}

extern "C" int runtime_cheat_enabled(int cheat) {
    return Valid(cheat) && Enabled(static_cast<OslCheat>(cheat)) ? 1 : 0;
}

extern "C" int runtime_cheat_toggle(int cheat) {
    if (!runtime_cheat_available(cheat)) {
        return -1;
    }
    session.Toggle(static_cast<OslCheat>(cheat));
    Publish();
    return runtime_cheat_enabled(cheat);
}

extern "C" void runtime_experience_start(int saved) {
    experience.Start(saved);
    PublishExperience();
}

extern "C" void runtime_experience_set_available(int available_now, volatile uint8_t* flag) {
    experience_available.store(available_now != 0, std::memory_order_relaxed);
    experience_flag.store(flag, std::memory_order_release);
    PublishExperience();
}

extern "C" int runtime_experience_available(void) {
    return experience_available.load(std::memory_order_relaxed) ? 1 : 0;
}

extern "C" int runtime_experience_multiplier(void) {
    return Multiplier();
}

extern "C" int runtime_experience_cycle(void) {
    if (!runtime_experience_available()) {
        return -1;
    }
    experience.Cycle();
    PublishExperience();
    return Multiplier();
}
