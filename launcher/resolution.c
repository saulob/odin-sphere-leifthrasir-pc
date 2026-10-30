#include "osl_resolution.h"

#include <stdio.h>
#include <string.h>

const OslResolution osl_resolutions[OSL_RESOLUTION_COUNT] = {
    {"native", "Native (1080p)", 1920, 1080}, {"480p", "480p", 854, 480},
    {"720p", "720p", 1280, 720},              {"1440p", "1440p", 2560, 1440},
    {"1800p", "1800p", 3200, 1800},           {"4K", "4K", 3840, 2160},
};

int osl_resolution_index(const char *key) {
    for (int i = 0; key && i < OSL_RESOLUTION_COUNT; ++i)
        if (!strcmp(key, osl_resolutions[i].key)) return i;
    return 0;
}

const OslAspectRatio osl_aspect_ratios[OSL_ASPECT_COUNT] = {
    {"16:9", "16:9 Original", 16, 9}, {"16:10", "16:10", 16, 10},       {"21:9", "21:9 Ultrawide", 21, 9},
    {"32:9", "32:9 Super Ultrawide", 32, 9}, {"4:3", "4:3", 4, 3},      {"5:4", "5:4", 5, 4},
};

int osl_aspect_index(const char *key) {
    for (int i = 0; key && i < OSL_ASPECT_COUNT; ++i)
        if (!strcmp(key, osl_aspect_ratios[i].key)) return i;
    return 0;
}

static int aligned_width(int height, const OslAspectRatio *aspect) {
    const long width = ((long)height * aspect->width * 2 + aspect->height) / (2 * aspect->height);
    return (int)((width + 4) / 8 * 8);
}

OslResolution osl_render_target(const OslResolution *resolution, const OslAspectRatio *aspect) {
    OslResolution target = *resolution;
    if (aspect == &osl_aspect_ratios[0]) return target;
    const long budget = 3840L * 2160;
    int height = resolution->height;
    while (height > 8 && (long)aligned_width(height, aspect) * height > budget) height -= 8;
    target.width = aligned_width(height, aspect);
    target.height = height;
    return target;
}

static int fail(char *error, size_t size, const char *what, uint32_t offset) {
    snprintf(error, size, "render resolution patch: %s at image offset 0x%x", what, (unsigned)offset);
    return 0;
}

static uint32_t float_bits(int value) {
    const float number = (float)value;
    uint32_t bits;
    memcpy(&bits, &number, 4);
    return bits;
}

uint32_t osl_scaled_budget(uint32_t native_bytes, const OslResolution *target) {
    const uint64_t native_pixels = 1920u * 1080u;
    const uint64_t step = 2u << 20;
    const uint64_t pixels = (uint64_t)target->width * (uint64_t)target->height;
    uint64_t bytes = ((uint64_t)native_bytes * pixels + native_pixels - 1) / native_pixels;
    bytes = (bytes + step - 1) / step * step;
    return (uint32_t)(bytes > native_bytes ? bytes : native_bytes);
}

#define ANY (-1)

/* vmovss xmm1,[1.5f]; lea rax,[pair]; mov edi,0; vmovaps xmm2,xmm1; vmovss xmm5,[rax]; vmovss xmm1,[rax+4];
 * vmulss xmm3,xmm3,xmm2; vmulss xmm6,xmm6,xmm2; vmulss xmm7,xmm7,xmm2 */
enum { SNAPSHOT_WINDOW = 45, SNAPSHOT_FACTOR = 4, SNAPSHOT_PAIR = 11, SNAPSHOT_LOAD = 32 };
static const short snapshot_window[SNAPSHOT_WINDOW] = {
    0xc5, 0xfa, 0x10, 0x0d, ANY,  ANY,  ANY,  ANY,  0x48, 0x8d, 0x05, ANY,  ANY,  ANY,  ANY,
    0xbf, 0x00, 0x00, 0x00, 0x00, 0xc5, 0xf8, 0x28, 0xd1, 0xc5, 0xfa, 0x10, 0x28, 0xc5, 0xfa, 0x10,
    0x48, 0x04, 0xc5, 0xe2, 0x59, 0xda, 0xc5, 0xca, 0x59, 0xf2, 0xc5, 0xc2, 0x59, 0xfa,
};

static int32_t read_displacement(const unsigned char *at) {
    int32_t value;
    memcpy(&value, at, 4);
    return value;
}

/* mov edx,x; vmovd xmm1,edx; mov edx,y; vmovd xmm2,edx; xor edi,edi; vmulss xmm3,xmm3,xmm1; vmulss xmm6,xmm6,xmm1;
 * vmulss xmm7,xmm7,xmm2; vmovq xmm5,[pair]; vpsrlq xmm1,xmm5,32
 * The same registers hold the same values afterwards; edx and the flags are dead there. */
int osl_apply_snapshot_scale(unsigned char *image, uint64_t image_size, uint32_t offset, const OslResolution *resolution,
                             char *error, size_t error_size) {
    if ((uint64_t)offset + SNAPSHOT_WINDOW > image_size) return fail(error, error_size, "site outside the image", offset);
    unsigned char *at = image + offset;
    for (int i = 0; i < SNAPSHOT_WINDOW; ++i)
        if (snapshot_window[i] != ANY && at[i] != (unsigned char)snapshot_window[i])
            return fail(error, error_size, "unexpected instruction", offset);
    const int64_t factor = (int64_t)offset + SNAPSHOT_FACTOR + 4 + read_displacement(at + SNAPSHOT_FACTOR);
    const int64_t pair = (int64_t)offset + SNAPSHOT_PAIR + 4 + read_displacement(at + SNAPSHOT_PAIR);
    uint32_t native;
    if (factor < 0 || (uint64_t)factor + 4 > image_size) return fail(error, error_size, "constant outside the image", offset);
    memcpy(&native, image + factor, 4);
    if (native != 0x3fc00000) return fail(error, error_size, "unexpected constant", offset);

    const float scales[2] = {(float)resolution->width / 1280.0f, (float)resolution->height / 720.0f};
    uint32_t x, y;
    memcpy(&x, &scales[0], 4);
    memcpy(&y, &scales[1], 4);
    const int32_t pair_displacement = (int32_t)(pair - ((int64_t)offset + SNAPSHOT_LOAD + 8));
    unsigned char code[SNAPSHOT_WINDOW] = {0xba, 0, 0, 0, 0, 0xc5, 0xf9, 0x6e, 0xca, 0xba, 0, 0, 0, 0, 0xc5, 0xf9, 0x6e, 0xd2,
                                           0x31, 0xff, 0xc5, 0xe2, 0x59, 0xd9, 0xc5, 0xca, 0x59, 0xf1, 0xc5, 0xc2, 0x59, 0xfa,
                                           0xc5, 0xfa, 0x7e, 0x2d, 0, 0, 0, 0, 0xc5, 0xf1, 0x73, 0xd5, 0x20};
    memcpy(code + 1, &x, 4);
    memcpy(code + 10, &y, 4);
    memcpy(code + SNAPSHOT_LOAD + 4, &pair_displacement, 4);
    memcpy(at, code, sizeof(code));
    return 1;
}

int osl_apply_resolution(unsigned char *image, uint64_t image_size, const OslResolutionSite *sites, size_t count,
                         const OslResolution *resolution, char *error, size_t error_size) {
    for (size_t i = 0; i < count; ++i) {
        const OslResolutionSite *site = &sites[i];
        const uint64_t immediate_size = site->packed_floats ? 8 : 4;
        const uint64_t end = (uint64_t)site->offset + site->opcode_length + immediate_size;
        if (end > image_size) return fail(error, error_size, "site outside the image", site->offset);
        const unsigned char *at = image + site->offset;
        uint32_t current;
        memcpy(&current, at + site->opcode_length, 4);
        if (memcmp(at, site->opcode, site->opcode_length) || current != site->value)
            return fail(error, error_size, "unexpected instruction", site->offset);
        if (site->packed_floats) {
            memcpy(&current, at + site->opcode_length + 4, 4);
            if (current != site->value_high)
                return fail(error, error_size, "unexpected instruction", site->offset);
        }
    }
    for (size_t i = 0; i < count; ++i) {
        const OslResolutionSite *site = &sites[i];
        if (site->packed_floats) {
            const uint32_t values[2] = {float_bits(resolution->width), float_bits(resolution->height)};
            memcpy(image + site->offset + site->opcode_length, values, sizeof(values));
        } else if (site->pixel_budget) {
            const uint32_t value = osl_scaled_budget(site->value, resolution);
            memcpy(image + site->offset + site->opcode_length, &value, 4);
        } else {
            const uint32_t value = (uint32_t)(site->height ? resolution->height : resolution->width);
            memcpy(image + site->offset + site->opcode_length, &value, 4);
        }
    }
    return 1;
}
