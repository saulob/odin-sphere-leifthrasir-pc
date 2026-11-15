/* The cheat state shared by the in-game menu (video core) and the game hooks. The hooks read one byte per cheat; the
 * menu changes the state of this session only and never writes the settings file. */
#ifndef OSL_RUNTIME_CHEATS_H
#define OSL_RUNTIME_CHEATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called once, after the launcher: `saved_mask` is osl_cheats_enabled(), the start of this game session. Clears every
 * override the menu made. */
void runtime_cheats_start(unsigned saved_mask);

/* The cheats whose hooks are in (osl_install_cheats' result) and the hooks' flag bytes (OSL_CHEAT_COUNT of them), which
 * this module keeps equal to each cheat's effective state. */
void runtime_cheats_set_available(unsigned available_mask, volatile uint8_t *hook_flags);

unsigned runtime_cheats_available_mask(void);
int runtime_cheat_available(int cheat);

/* 1 when the cheat is available and on: the saved value, or the menu's override of it this session. */
int runtime_cheat_enabled(int cheat);

/* The in-game menu's toggle: only this session changes. Returns the new state, or -1 for a cheat that is unavailable. */
int runtime_cheat_toggle(int cheat);

/* The Experience Multiplier: `saved` is osl_experience_multiplier_saved(). Clears the menu's override. */
void runtime_experience_start(int saved);

/* Whether its hook is in (osl_install_experience's result) and the hook's byte, which this module keeps at the
 * effective multiplier, 0 for Off. */
void runtime_experience_set_available(int available, volatile uint8_t *hook_flag);

int runtime_experience_available(void);

/* The effective multiplier, 1 (Off) while unavailable. */
int runtime_experience_multiplier(void);

/* The in-game menu's step to the next choice, this session only. Returns the new multiplier, or -1 when unavailable. */
int runtime_experience_cycle(void);

#ifdef __cplusplus
}
#endif

#endif
