/* The launcher, called by the loader before the guest and the game window exist. */
#ifndef OSL_LAUNCHER_H
#define OSL_LAUNCHER_H

#include <stddef.h>

#include "osl_resolution.h"
#include "osl_skip_intro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when the graphics system can run the game. Otherwise 0, with what is wrong and what to do about it in the buffers. */
typedef int (*OslGraphicsCheck)(char *cause, size_t cause_size, char *advice, size_t advice_size);

typedef struct {
    const char *app0;          /* prepared game folder (/app0) */
    const char *image_dir;     /* folder with boot.bin and image.json */
    const char *user_dir;      /* user data; osl.ini lives here unless settings_file is set */
    const char *settings_file; /* NULL: <user_dir>/osl.ini */
    const char *logs_dir;      /* NULL when unknown */
    int skip_once;             /* --osl-skip-launcher: do not show it unless the game files are not ready */
    int force_show;            /* --launcher: show it even when "Show this launcher" is off, like holding Shift */
    OslGraphicsCheck check_graphics; /* NULL: Play does not check; see osl_check_vulkan */
} OslLauncherConfig;

/* NULL asks for the default, which is relative to the executable (launcher/paths.h). */
typedef struct {
    const char *boot;
    const char *app0;
    const char *user_dir;
    const char *logs_dir;
} OslPathRequest;

typedef struct {
    char boot[4096];
    char app0[4096];
    char image_dir[4096];
    char user_dir[4096];
    char logs_dir[4096];
} OslPaths;

/* 0 when the executable's folder is unknown or a path does not fit. */
int osl_resolve_paths(const OslPathRequest *request, OslPaths *paths);

/* Index into osl_resolutions (osl_resolution.h) of the render_resolution in the settings file, among the resolutions
 * the build image_dir was built from: 0 (native) when the file or the value is missing or the build has no verified
 * render resolution sites. */
int osl_render_resolution(const char *settings_file, const char *image_dir);

/* Index into osl_aspect_ratios of the aspect_ratio in the settings file, among the shapes the build image_dir was
 * built from: 0 (16:9 Original) when the file or the value is missing or the build has no verified render resolution
 * sites. */
int osl_aspect_ratio(const char *settings_file, const char *image_dir);

/* The system language the game is told for the text_language in the settings file, among the languages of the build
 * image_dir was built from: the build's default (English when the build is unknown) when the file or the value is
 * missing or not one of them. */
int osl_text_language(const char* settings_file, const char* image_dir);

/* Prints into the run log the Gameplay choices the loader applies, starting with the menus-and-subtitles language. */
void osl_log_gameplay_settings(const char* settings_file, const char* image_dir);

/* 1 when the settings file turns Skip intro on (skip_intro; 0 by default). */
int osl_skip_intro(const char *settings_file);

/* 1 when the settings file keeps the logs of normal sessions (detailed_logging; 1 by default). */
int osl_detailed_logging(const char *settings_file);

/* The folder holding this run's log: the log_file's parent when one is given, else logs_dir. 0 when
 * neither yields a path. */
int osl_logs_directory(const char *logs_dir, const char *log_file, char *out, size_t size);

/* Fills `names` with the distinct names of the Vulkan devices the renderer can use and returns how many (none without a
 * usable Vulkan on Linux); enumerates only, no device is created. */
int osl_gpu_names(char (*names)[256], int max);

/* An OslGraphicsCheck: 1 when Vulkan can run the renderer. Otherwise logs why and returns 0. It asks again on every call
 * until it succeeds, so a driver installed meanwhile is found. */
int osl_check_vulkan(char *cause, size_t cause_size, char *advice, size_t advice_size);

/* Builds boot.bin and image.json in `out` from the prepared game folder `app0` (UTF-8 paths). 0 on success; otherwise 1
 * and the reason in `error`. */
int osl_build_boot_image(const char *app0, const char *out, char *error, size_t error_size);

typedef struct {
    const char *label;
    const OslResolutionSite *resolution_sites;
    size_t resolution_site_count;
    const OslSkipIntroSites *skip_intro; /* NULL: not available for the build */
} OslGameBuild;

/* The supported game build a boot image folder was built from (its image.json); 0 when it is not one. */
int osl_game_build(const char *image_dir, OslGameBuild *build);

/* The one readiness check (launcher/game_files.h): 1 when the prepared game and its boot image are a supported,
 * complete installation; otherwise 0 and the reason in `detail`. */
int osl_check_game(const char *app0, const char *image_dir, char *detail, size_t detail_size);

/* Developer/diagnostic CLI: installs `package` into the folders of `app0` and `image_dir` (the same installer the
 * launcher runs). 0 on success; otherwise 1 and a message in `detail`. */
int osl_install_game(const char *package, const char *app0, const char *image_dir, char *detail, size_t detail_size);

typedef enum { OSL_LAUNCHER_PLAY = 0, OSL_LAUNCHER_QUIT = 1, OSL_LAUNCHER_BLOCKED = 2 } OslLauncherResult;

/* Shows the launcher when its rules say so (game files not ready, "Show this launcher" on, or Shift held) and returns
 * once the player pressed Play or quit. Returns OSL_LAUNCHER_PLAY at once when it is not shown.
 * While check_graphics fails, Play is refused: the launcher says why and stays open. A launcher that is not shown
 * explains in a window of its own and returns OSL_LAUNCHER_BLOCKED. */
OslLauncherResult osl_launcher_run(const OslLauncherConfig *config);

#ifdef __cplusplus
}
#endif

#endif
