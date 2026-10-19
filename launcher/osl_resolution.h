/* The render resolutions the launcher offers and the game code that selects them. */
#ifndef OSL_RESOLUTION_H
#define OSL_RESOLUTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;  /* render_resolution in osl.ini */
    const char *name; /* launcher label */
    int width, height;
} OslResolution;

#define OSL_RESOLUTION_COUNT 6
extern const OslResolution osl_resolutions[OSL_RESOLUTION_COUNT];

/* Index of the key, 0 (native) when it is not listed. */
int osl_resolution_index(const char *key);

typedef struct {
    const char *key;
    const char *name;
    int width, height;
} OslAspectRatio;

#define OSL_ASPECT_COUNT 6
extern const OslAspectRatio osl_aspect_ratios[OSL_ASPECT_COUNT];

/* Index of the key, 0 (16:9 Original) when it is not listed. */
int osl_aspect_index(const char *key);

/* The width is aligned to the 8-pixel tile; a target above the 4K pixel count keeps its ratio and loses height. */
OslResolution osl_render_target(const OslResolution *resolution, const OslAspectRatio *aspect);

/* One instruction in the game that holds a render width, height, or the packed float screen size. */
typedef struct {
    uint32_t offset; /* image offset of the opcode */
    uint8_t opcode_length;
    uint8_t opcode[2];
    uint32_t value;       /* the original immediate, or the low word of a packed 64-bit immediate */
    int height;
    uint32_t value_high;  /* the high word of a packed 64-bit immediate */
    int packed_floats;    /* 1 when value/value_high are the float bits of the width and height */
} OslResolutionSite;

/* Writes the resolution's width and height into every site after checking each one still holds the original
 * instruction. Returns 0 and a message in `error` when the image does not match; nothing is written then. */
int osl_apply_resolution(unsigned char *image, uint64_t image_size, const OslResolutionSite *sites, size_t count,
                         const OslResolution *resolution, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
