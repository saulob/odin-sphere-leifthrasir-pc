/* Interface between the loader/runtime and the video core (shadPS4-derived, gpu/shim/oslgpu.cpp). */
#ifndef OSLGPU_H
#define OSLGPU_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const char *title;          /* window title */
    const char *serial;         /* CUSA id, names the pipeline cache */
    const char *user_dir;       /* user folder: the pipeline cache lives in its cache folder */
    const char *settings_file;  /* osl.ini */
    uint32_t sdk_version;       /* from the eboot's procparam */
    uint32_t psf_attributes;    /* param.sfo ATTRIBUTE */
    int32_t width, height;      /* initial window size */
} OslGpuConfig;
/* 1 when a video core is linked in, 0 for the stub. */
int oslgpu_available(void);
/* Registers kernel event queues (needed with or without graphics). */
void oslgpu_register_kernel(void);
/* Creates window, Vulkan device, presenter and GPU command processor; 0 on success. */
int oslgpu_init(const OslGpuConfig *config);
/* Function for an imported NID ("NID#lib"), or 0 when the video core does not provide it. */
uintptr_t oslgpu_resolve(const char *scoped_nid);
/* Called first by the loader's fault handler: 1 when a GPU page-tracking fault was handled. */
int oslgpu_handle_fault(void *context, void *address);
void oslgpu_dump_guest_writes(void *context);
/* Number of symbols registered by the video core (diagnostics). */
unsigned oslgpu_symbol_count(void);
/* Keyboard text entry and list selection drawn by the video core; begin returns 0 without a window.
 * text poll: 0 typing, 1 confirmed, 2 cancelled. choice poll: -1 open, -2 cancelled, else the index. */
int oslgpu_text_input_begin(const char *initial_utf8, const char *prompt_utf8);
int oslgpu_text_input_poll(char *out_utf8, uint64_t size);
int oslgpu_choice_begin(const char *title, const char *const *items, const char *const *images, int count, int focus);
int oslgpu_choice_poll(void);
/* The choice list reads PS4 pad button bits from this callback every frame while it is open. */
void oslgpu_choice_set_input(uint32_t (*buttons)(void));
/* 1 while a game dialog is open: the game's pad input should be held neutral. */
int oslgpu_ui_captures_input(void);
#ifdef __cplusplus
}
#endif
#endif
