/* CRILAYLA decompression for scripts/localization/visuals.py (the pure-Python reader in scripts/prompts/cpk.py is the
   reference; this is the same algorithm, built on demand as a shared library into cache/localization). */
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

typedef struct {
    const uint8_t* src;
    int64_t pos;
    int bits_left;
    uint32_t buf;
} Bits;

static uint32_t get_bits(Bits* b, int n) {
    uint32_t value = 0;
    while (n) {
        if (b->bits_left == 0) {
            if (b->pos < 0) return value;
            b->buf = b->src[b->pos--];
            b->bits_left = 8;
        }
        int take = b->bits_left < n ? b->bits_left : n;
        value = (value << take) | ((b->buf >> (b->bits_left - take)) & ((1u << take) - 1));
        b->bits_left -= take;
        n -= take;
    }
    return value;
}

/* data: the stored CRILAYLA block; out: out_size + 0x100 bytes. Returns 0 on success. */
EXPORT int crilayla_decompress(const uint8_t* data, int64_t size, uint8_t* out, int64_t out_capacity) {
    if (size < 16 || memcmp(data, "CRILAYLA", 8) != 0) return 1;
    uint32_t out_size, header_off;
    memcpy(&out_size, data + 8, 4);
    memcpy(&header_off, data + 12, 4);
    if ((int64_t)out_size + 0x100 > out_capacity || 0x10 + (int64_t)header_off + 0x100 > size) return 2;
    memcpy(out, data + 0x10 + header_off, 0x100);
    Bits b = {data + 0x10, (int64_t)header_off - 1, 0, 0};
    int64_t out_pos = (int64_t)out_size + 0x100 - 1;
    int64_t written = 0;
    static const int levels[4] = {2, 3, 5, 8};
    while (written < out_size) {
        if (get_bits(&b, 1)) {
            int64_t offset = get_bits(&b, 13) + 3;
            int64_t length = 3;
            int level = 0;
            for (;;) {
                int nbits = level < 4 ? levels[level] : 8;
                uint32_t this_ = get_bits(&b, nbits);
                length += this_;
                if (this_ != (1u << nbits) - 1) break;
                if (level < 4) level++;
            }
            for (int64_t k = 0; k < length; k++) {
                if (out_pos < 0 || out_pos + offset >= out_capacity) return 3;
                out[out_pos] = out[out_pos + offset];
                out_pos--;
            }
            written += length;
        } else {
            if (out_pos < 0) return 3;
            out[out_pos--] = (uint8_t)get_bits(&b, 8);
            written++;
        }
    }
    return 0;
}
