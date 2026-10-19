/* Keyboard bindings: the actions, their defaults and the saved key names, shared by the launcher, the runtime pad
 * and the video core. Keys are SDL scancodes, saved as the shadPS4 names (space, lshift, f10, pgup...) or SDL's own
 * lowercase names for the rest. */
#ifndef OSL_KEYS_H
#define OSL_KEYS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OSL_KEY_LEFT_STICK_UP, OSL_KEY_LEFT_STICK_DOWN, OSL_KEY_LEFT_STICK_LEFT, OSL_KEY_LEFT_STICK_RIGHT,
    OSL_KEY_RIGHT_STICK_UP, OSL_KEY_RIGHT_STICK_DOWN, OSL_KEY_RIGHT_STICK_LEFT, OSL_KEY_RIGHT_STICK_RIGHT,
    OSL_KEY_CROSS, OSL_KEY_CIRCLE, OSL_KEY_SQUARE, OSL_KEY_TRIANGLE,
    OSL_KEY_L1, OSL_KEY_R1, OSL_KEY_L2, OSL_KEY_R2, OSL_KEY_L3, OSL_KEY_R3,
    OSL_KEY_DPAD_UP, OSL_KEY_DPAD_DOWN, OSL_KEY_DPAD_LEFT, OSL_KEY_DPAD_RIGHT,
    OSL_KEY_OPTIONS, OSL_KEY_TOUCHPAD_LEFT, OSL_KEY_TOUCHPAD_RIGHT,
    OSL_KEY_HOTKEY_MENU, OSL_KEY_HOTKEY_WINDOW_MODE,
    OSL_KEY_COUNT
} OslKeyAction;

#define OSL_KEY_FIRST_HOTKEY OSL_KEY_HOTKEY_MENU

typedef struct {
    const char *ini_key; /* the line in osl.ini */
    const char *label;   /* the launcher's row */
    int default_scancode;
} OslKeyInfo;

extern const OslKeyInfo osl_key_actions[OSL_KEY_COUNT];

typedef struct { int scancode[OSL_KEY_COUNT]; } OslKeyMap;

void osl_keys_defaults(OslKeyMap *map);

/* The scancode a saved name stands for (case-insensitive), or 0 when it names no bindable key. */
int osl_key_from_name(const char *name);
/* False for no key and for Escape, which cancels a capture. */
int osl_key_bindable(int scancode);
/* The name saved for a scancode, written to `buffer`; empty when the key is not bindable. */
const char *osl_key_name(int scancode, char *buffer, size_t size);

/* Applies one saved line to the map; false when the key is not an action or the value names no bindable key. */
int osl_keys_apply(OslKeyMap *map, const char *ini_key, const char *value);
/* Defaults, then every valid binding in the osl.ini at `path`; a missing file or line keeps the default. */
void osl_keys_load(const char *path, OslKeyMap *map);

#ifdef __cplusplus
}
#endif

#endif
