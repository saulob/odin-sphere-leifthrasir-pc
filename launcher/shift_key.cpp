#include "shift_key.h"

#include "trg/platform.h"

#ifndef _WIN32
#include <dlfcn.h>

#include <cstdlib>
#endif

namespace osl {

#ifdef _WIN32
bool ShiftHeldAtStart() { return trg::ShiftHeld(); }
#else
namespace {

constexpr unsigned long kShiftLeft = 0xffe1, kShiftRight = 0xffe2;

struct X11 {
    void* library = nullptr;
    void* (*open)(const char*) = nullptr;
    int (*close)(void*) = nullptr;
    int (*query_keymap)(void*, char[32]) = nullptr;
    unsigned char (*keysym_to_keycode)(void*, unsigned long) = nullptr;
};

template <typename T> bool Load(void* library, const char* name, T& function) {
    function = reinterpret_cast<T>(dlsym(library, name));
    return function != nullptr;
}

} // namespace

bool ShiftHeldAtStart() {
    const char* display_name = std::getenv("DISPLAY");
    if (!display_name || !*display_name) {
        return false;
    }
    X11 x11;
    x11.library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (!x11.library) {
        return false;
    }
    bool held = false;
    if (Load(x11.library, "XOpenDisplay", x11.open) && Load(x11.library, "XCloseDisplay", x11.close) &&
        Load(x11.library, "XQueryKeymap", x11.query_keymap) &&
        Load(x11.library, "XKeysymToKeycode", x11.keysym_to_keycode)) {
        if (void* display = x11.open(display_name)) {
            char keys[32] = {};
            x11.query_keymap(display, keys);
            for (const unsigned long symbol : {kShiftLeft, kShiftRight}) {
                const unsigned char code = x11.keysym_to_keycode(display, symbol);
                held = held || (code != 0 && (keys[code / 8] & (1 << (code % 8))) != 0);
            }
            x11.close(display);
        }
    }
    return held;
}
#endif

} // namespace osl
