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
