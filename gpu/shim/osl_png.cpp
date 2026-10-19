// SPDX-License-Identifier: GPL-2.0-or-later
#include "osl_png.h"

#include <cstdio>

#include <png.h>

namespace OslPng {

namespace {
constexpr unsigned kMaxSide = 4096; // save icons are 228x128; anything far larger is not one
}

bool DecodeRgba(const unsigned char* data, size_t size, Image& out) {
    out = {};
    if (!data || !size || size > 64u << 20) {
        return false;
    }
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, size)) {
        return false;
    }
    if (image.width == 0 || image.height == 0 || image.width > kMaxSide || image.height > kMaxSide) {
        png_image_free(&image);
        return false;
    }
    image.format = PNG_FORMAT_RGBA;
    out.width = int(image.width);
    out.height = int(image.height);
    out.rgba.resize(PNG_IMAGE_SIZE(image));
    // png_image_finish_read releases the decoder whether or not it succeeds.
    if (!png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr)) {
        out = {};
        return false;
    }
    return true;
}

bool LoadRgba(const char* path, Image& out) {
    out = {};
    FILE* file = path ? std::fopen(path, "rb") : nullptr;
    if (!file) {
        return false;
    }
    std::vector<unsigned char> bytes;
    unsigned char chunk[65536];
    size_t n;
    while ((n = std::fread(chunk, 1, sizeof(chunk), file)) > 0 && bytes.size() <= (64u << 20)) {
        bytes.insert(bytes.end(), chunk, chunk + n);
    }
    std::fclose(file);
    return DecodeRgba(bytes.data(), bytes.size(), out);
}

} // namespace OslPng
