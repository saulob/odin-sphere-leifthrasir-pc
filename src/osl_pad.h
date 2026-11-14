/* Controller bindings: which physical gamepad button or trigger presses each PS4 button the game reads. Shared by
 * the launcher, which edits them, and the runtime pad, which applies them to every connected gamepad. A source is
 * saved as SDL's gamepad name (a, leftshoulder, dpup, lefttrigger...). */
#ifndef OSL_PAD_H
#define OSL_PAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The order is the PS4 button order of OSL_PAD_MAP and of the launcher's rows. */
typedef enum {
    OSL_PAD_CROSS, OSL_PAD_CIRCLE, OSL_PAD_SQUARE, OSL_PAD_TRIANGLE,
    OSL_PAD_L1, OSL_PAD_R1, OSL_PAD_L2, OSL_PAD_R2, OSL_PAD_L3, OSL_PAD_R3,
    OSL_PAD_OPTIONS, OSL_PAD_TOUCHPAD,
    OSL_PAD_DPAD_UP, OSL_PAD_DPAD_DOWN, OSL_PAD_DPAD_LEFT, OSL_PAD_DPAD_RIGHT,
    OSL_PAD_COUNT
} OslPadAction;

/* A source below 32 is an SDL_GamepadButton; the triggers are axes in SDL and have their own values. */
#define OSL_PAD_SOURCE_NONE (-1)
#define OSL_PAD_SOURCE_LEFT_TRIGGER 0x100
#define OSL_PAD_SOURCE_RIGHT_TRIGGER 0x101
/* A trigger counts as pressed above this value (0..255). */
#define OSL_PAD_TRIGGER_PRESS 30

typedef struct {
    const char *ini_key;  /* the line in osl.ini */
    const char *label;    /* the launcher's row */
    const char *name;     /* the PS4 button in OSL_PAD_MAP */
    int default_source;
} OslPadInfo;

extern const OslPadInfo osl_pad_actions[OSL_PAD_COUNT];

typedef struct { int source[OSL_PAD_COUNT]; } OslPadMap;

void osl_pad_defaults(OslPadMap *map);

/* The source a saved name stands for (case-insensitive), or OSL_PAD_SOURCE_NONE. The Guide button belongs to the
 * system and is not bindable. */
int osl_pad_source_from_name(const char *name);
int osl_pad_source_bindable(int source);
/* The name saved for a source; empty when it is not bindable. */
const char *osl_pad_source_name(int source, char *buffer, size_t size);
/* The launcher's text for a source, e.g. "A", "LB / L1", "View / Back". */
const char *osl_pad_source_label(int source);

/* Applies one saved line; false when the key is not a controller binding or the value names no bindable source. */
int osl_pad_apply(OslPadMap *map, const char *ini_key, const char *value);
/* Defaults, then every valid binding in the osl.ini at `path`; a missing file or line keeps the default. */
void osl_pad_load(const char *path, OslPadMap *map);
/* Applies an OSL_PAD_MAP value ("cross=b,circle=a") on top of the map; returns the entries used. Bad entries are
 * logged and skipped. */
int osl_pad_apply_spec(OslPadMap *map, const char *spec);
/* How many actions are bound to something other than their default. */
int osl_pad_changed(const OslPadMap *map);

/* One sample of a gamepad: a bit per pressed SDL_GamepadButton and the trigger values (0..255). */
typedef struct { uint32_t buttons; uint8_t left_trigger, right_trigger; } OslPadInput;
/* The PS4 side: a bit per pressed action, and the L2/R2 values the game reads. */
typedef struct { uint32_t pressed; uint8_t l2, r2; } OslPadOutput;
void osl_pad_translate(const OslPadMap *map, const OslPadInput *in, OslPadOutput *out);
/* The bindable source held in a sample, or OSL_PAD_SOURCE_NONE; a trigger counts when pulled about halfway. */
int osl_pad_first_pressed(const OslPadInput *in);

#ifdef __cplusplus
}
#endif

#endif
