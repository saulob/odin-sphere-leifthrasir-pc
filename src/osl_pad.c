#include "osl_pad.h"
#include "osl_keys.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

const OslPadInfo osl_pad_actions[OSL_PAD_COUNT] = {
    {"pad_cross", "Cross (A)", "cross", SDL_GAMEPAD_BUTTON_SOUTH},
    {"pad_circle", "Circle (B)", "circle", SDL_GAMEPAD_BUTTON_EAST},
    {"pad_square", "Square (X)", "square", SDL_GAMEPAD_BUTTON_WEST},
    {"pad_triangle", "Triangle (Y)", "triangle", SDL_GAMEPAD_BUTTON_NORTH},
    {"pad_l1", "L1 (LB)", "l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"pad_r1", "R1 (RB)", "r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"pad_l2", "L2 (LT)", "l2", OSL_PAD_SOURCE_LEFT_TRIGGER},
    {"pad_r2", "R2 (RT)", "r2", OSL_PAD_SOURCE_RIGHT_TRIGGER},
    {"pad_l3", "L3 (left stick click)", "l3", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"pad_r3", "R3 (right stick click)", "r3", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"pad_options", "Options (Menu)", "options", SDL_GAMEPAD_BUTTON_START},
    {"pad_touchpad", "Touchpad (View)", "touchpad", SDL_GAMEPAD_BUTTON_BACK},
    {"pad_dpad_up", "D-pad up", "up", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"pad_dpad_down", "D-pad down", "down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"pad_dpad_left", "D-pad left", "left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"pad_dpad_right", "D-pad right", "right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

static const struct { int source; const char *label; } labels[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, "A"}, {SDL_GAMEPAD_BUTTON_EAST, "B"}, {SDL_GAMEPAD_BUTTON_WEST, "X"},
    {SDL_GAMEPAD_BUTTON_NORTH, "Y"}, {SDL_GAMEPAD_BUTTON_BACK, "View / Back"}, {SDL_GAMEPAD_BUTTON_START, "Menu / Start"},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, "Left stick click"}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, "Right stick click"},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "LB / L1"}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "RB / R1"},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, "D-pad up"}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "D-pad down"},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "D-pad left"}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "D-pad right"},
    {SDL_GAMEPAD_BUTTON_TOUCHPAD, "Touchpad click"}, {SDL_GAMEPAD_BUTTON_MISC1, "Extra button 1"},
    {SDL_GAMEPAD_BUTTON_MISC2, "Extra button 2"}, {SDL_GAMEPAD_BUTTON_MISC3, "Extra button 3"},
    {SDL_GAMEPAD_BUTTON_MISC4, "Extra button 4"}, {SDL_GAMEPAD_BUTTON_MISC5, "Extra button 5"},
    {SDL_GAMEPAD_BUTTON_MISC6, "Extra button 6"}, {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, "Right paddle 1"},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, "Left paddle 1"}, {SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, "Right paddle 2"},
    {SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, "Left paddle 2"},
    {OSL_PAD_SOURCE_LEFT_TRIGGER, "LT / L2"}, {OSL_PAD_SOURCE_RIGHT_TRIGGER, "RT / R2"},
};

void osl_pad_defaults(OslPadMap *map) {
    for (int i=0;i<OSL_PAD_COUNT;++i) map->source[i]=osl_pad_actions[i].default_source;
}

int osl_pad_source_bindable(int source) {
    if (source==OSL_PAD_SOURCE_LEFT_TRIGGER || source==OSL_PAD_SOURCE_RIGHT_TRIGGER) return 1;
    return source>SDL_GAMEPAD_BUTTON_INVALID && source<SDL_GAMEPAD_BUTTON_COUNT && source!=SDL_GAMEPAD_BUTTON_GUIDE;
}

int osl_pad_source_from_name(const char *name) {
    if (!name || !*name || strlen(name)>31) return OSL_PAD_SOURCE_NONE;
    char lower[32];
    size_t n=0;
    for (;name[n];++n) lower[n]=(char)(name[n]>='A' && name[n]<='Z' ? name[n]-'A'+'a' : name[n]);
    lower[n]=0;
    if (!strcmp(lower,SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER))) return OSL_PAD_SOURCE_LEFT_TRIGGER;
    if (!strcmp(lower,SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER))) return OSL_PAD_SOURCE_RIGHT_TRIGGER;
    const int button=(int)SDL_GetGamepadButtonFromString(lower);
    return osl_pad_source_bindable(button) ? button : OSL_PAD_SOURCE_NONE;
}

const char *osl_pad_source_name(int source, char *buffer, size_t size) {
    if (size) buffer[0]=0;
    if (!size || !osl_pad_source_bindable(source)) return buffer;
    const char *name=source==OSL_PAD_SOURCE_LEFT_TRIGGER ? SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
                      : source==OSL_PAD_SOURCE_RIGHT_TRIGGER ? SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                      : SDL_GetGamepadStringForButton((SDL_GamepadButton)source);
    if (name) SDL_strlcpy(buffer,name,size);
    return buffer;
}

const char *osl_pad_source_label(int source) {
    for (size_t i=0;i<sizeof(labels)/sizeof(*labels);++i) if (labels[i].source==source) return labels[i].label;
    return "";
}

int osl_pad_apply(OslPadMap *map, const char *ini_key, const char *value) {
    for (int i=0;i<OSL_PAD_COUNT;++i) {
        if (strcmp(ini_key,osl_pad_actions[i].ini_key)) continue;
        const int source=osl_pad_source_from_name(value);
        if (source==OSL_PAD_SOURCE_NONE) return 0;
        map->source[i]=source;
        return 1;
    }
    return 0;
}

static void apply_pad_entry(const char *key, const char *value, void *map) {
    osl_pad_apply((OslPadMap *)map,key,value);
}

void osl_pad_load(const char *path, OslPadMap *map) {
    osl_pad_defaults(map);
    osl_ini_read(path,apply_pad_entry,map);
}

int osl_pad_apply_spec(OslPadMap *map, const char *spec) {
    if (!spec || !*spec) return 0;
    char copy[1024];
    snprintf(copy,sizeof(copy),"%s",spec);
    int used=0;
    char *entry=copy;
    while (entry) {
        char *next=strchr(entry,',');
        if (next) *next++=0;
        char *eq=strchr(entry,'=');
        if (!eq) {
            if (*osl_text_trim(entry)) printf("Runtime: pad map: ignored '%s' (expected button=name)\n",osl_text_trim(entry));
            entry=next;
            continue;
        }
        *eq=0;
        const char *button=osl_text_trim(entry), *name=osl_text_trim(eq+1);
        entry=next;
        int action=-1;
        for (int i=0;i<OSL_PAD_COUNT;++i) if (!SDL_strcasecmp(button,osl_pad_actions[i].name)) action=i;
        const int source=osl_pad_source_from_name(name);
        if (action<0 || source==OSL_PAD_SOURCE_NONE) {
            printf("Runtime: pad map: ignored '%s=%s'\n",button,name);
            continue;
        }
        map->source[action]=source;
        ++used;
    }
    return used;
}

int osl_pad_changed(const OslPadMap *map) {
    int changed=0;
    for (int i=0;i<OSL_PAD_COUNT;++i) changed+=map->source[i]!=osl_pad_actions[i].default_source;
    return changed;
}

static uint8_t source_value(int source, const OslPadInput *in) {
    if (source==OSL_PAD_SOURCE_LEFT_TRIGGER) return in->left_trigger;
    if (source==OSL_PAD_SOURCE_RIGHT_TRIGGER) return in->right_trigger;
    return source>=0 && source<32 && ((in->buttons>>source) & 1u) ? 255 : 0;
}

int osl_pad_first_pressed(const OslPadInput *in) {
    for (int button=0;button<SDL_GAMEPAD_BUTTON_COUNT;++button)
        if (((in->buttons>>button) & 1u) && osl_pad_source_bindable(button)) return button;
    if (in->left_trigger>=128) return OSL_PAD_SOURCE_LEFT_TRIGGER;
    if (in->right_trigger>=128) return OSL_PAD_SOURCE_RIGHT_TRIGGER;
    return OSL_PAD_SOURCE_NONE;
}

void osl_pad_translate(const OslPadMap *map, const OslPadInput *in, OslPadOutput *out) {
    memset(out,0,sizeof(*out));
    for (int i=0;i<OSL_PAD_COUNT;++i) {
        const int source=map->source[i];
        const uint8_t value=source_value(source,in);
        /* The default Touchpad source, View/Back, also stands for the controller's own touchpad click. */
        const int pressed=value>OSL_PAD_TRIGGER_PRESS ||
            (i==OSL_PAD_TOUCHPAD && source==SDL_GAMEPAD_BUTTON_BACK && ((in->buttons>>SDL_GAMEPAD_BUTTON_TOUCHPAD) & 1u));
        if (pressed) out->pressed|=1u<<i;
        if (i==OSL_PAD_L2) out->l2=value;
        if (i==OSL_PAD_R2) out->r2=value;
    }
}
