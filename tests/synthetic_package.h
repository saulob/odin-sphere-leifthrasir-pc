// Test-only: builds a fake PS4 package in memory that osl::Package opens like a real decrypted one. The public RSA
// moduli are the same fake-package keys the reader decrypts with; the package carries no game data.
#pragma once

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "crypto.h"

namespace osl_test::fakepkg {

using Bytes = std::vector<uint8_t>;

inline constexpr uint8_t kFakeModulus[256] = {
    0xC6, 0xCF, 0x71, 0xE7, 0xE5, 0x9A, 0xF0, 0xD1, 0x2A, 0x2C, 0x45, 0x8B, 0xF9, 0x2A, 0x0E, 0xC1, 0x43, 0x05, 0x8B, 0xC3,
    0x71, 0x17, 0x80, 0x1D, 0xCD, 0x49, 0x7D, 0xDE, 0x35, 0x9D, 0x25, 0x9B, 0xA0, 0xD7, 0xA0, 0xF2, 0x7D, 0x6C, 0x08, 0x7E,
    0xAA, 0x55, 0x02, 0x68, 0x2B, 0x23, 0xC6, 0x44, 0xB8, 0x44, 0x18, 0xEB, 0x56, 0xCF, 0x16, 0xA2, 0x48, 0x03, 0xC9, 0xE7,
    0x4F, 0x87, 0xEB, 0x3D, 0x30, 0xC3, 0x15, 0x88, 0xBF, 0x20, 0xE7, 0x9D, 0xFF, 0x77, 0x0C, 0xDE, 0x1D, 0x24, 0x1E, 0x63,
    0xA9, 0x4F, 0x8A, 0xBF, 0x5B, 0xBE, 0x60, 0x19, 0x68, 0x33, 0x3B, 0xFC, 0xED, 0x9F, 0x47, 0x4E, 0x5F, 0xF8, 0xEA, 0xCB,
    0x3D, 0x00, 0xBD, 0x67, 0x01, 0xF9, 0x2C, 0x6D, 0xC6, 0xAC, 0x13, 0x64, 0xE7, 0x67, 0x14, 0xF3, 0xDC, 0x52, 0x69, 0x6A,
    0xB9, 0x83, 0x2C, 0x42, 0x30, 0x13, 0x1B, 0xB2, 0xD8, 0xA5, 0x02, 0x0D, 0x79, 0xED, 0x96, 0xB1, 0x0D, 0xF8, 0xCC, 0x0C,
    0xDF, 0x81, 0x95, 0x4F, 0x03, 0x58, 0x09, 0x57, 0x0E, 0x80, 0x69, 0x2E, 0xFE, 0xFF, 0x52, 0x77, 0xEA, 0x75, 0x28, 0xA8,
    0xFB, 0xC9, 0xBE, 0xBF, 0x9F, 0xBB, 0xB7, 0x79, 0x8E, 0x18, 0x05, 0xE1, 0x80, 0xBD, 0x50, 0x34, 0x94, 0x81, 0xD3, 0x53,
    0xC2, 0x69, 0xA2, 0xD2, 0x4C, 0xCF, 0x6C, 0xF4, 0x57, 0x2C, 0x10, 0x4A, 0x3F, 0xFB, 0x22, 0xFD, 0x8B, 0x97, 0xE2, 0xC9,
    0x5B, 0xA6, 0x2B, 0xCD, 0xD6, 0x1B, 0x6B, 0xDB, 0x68, 0x7F, 0x4B, 0xC2, 0xA0, 0x50, 0x34, 0xC0, 0x05, 0xE5, 0x8D, 0xEF,
    0x24, 0x67, 0xFF, 0x93, 0x40, 0xCF, 0x2D, 0x62, 0xA2, 0xA0, 0x50, 0xB1, 0xF1, 0x3A, 0xA8, 0x3D, 0xFD, 0x80, 0xD1, 0xF9,
    0xB8, 0x05, 0x22, 0xAF, 0xC8, 0x35, 0x45, 0x90, 0x58, 0x8E, 0xE3, 0x3A, 0x7C, 0xBD, 0x3E, 0x27,
};

inline constexpr uint8_t kDk3Modulus[256] = {
    0xD2, 0x12, 0xFC, 0x33, 0x5F, 0x6D, 0xDB, 0x83, 0x16, 0x09, 0x62, 0x8B, 0x03, 0x56, 0x27, 0x37, 0x82, 0xD4, 0x77, 0x85,
    0x35, 0x29, 0x39, 0x2D, 0x52, 0x6B, 0x8C, 0x4C, 0x8C, 0xFB, 0x06, 0xC1, 0x84, 0x5B, 0xE7, 0xD4, 0xF7, 0xBC, 0xD2, 0x4E,
    0x62, 0x45, 0xCD, 0x2A, 0xBB, 0xD7, 0x77, 0x76, 0x45, 0x36, 0x55, 0x27, 0x3F, 0xB3, 0xF5, 0xF9, 0x8E, 0xDA, 0x4B, 0xEF,
    0xAA, 0x59, 0xAE, 0xB3, 0x9B, 0xEA, 0x54, 0x98, 0xD2, 0x06, 0x32, 0x6A, 0x58, 0x31, 0x2A, 0xE0, 0xD4, 0x4F, 0x90, 0xB5,
    0x0A, 0x7D, 0xEC, 0xF4, 0x3A, 0x9C, 0x52, 0x67, 0x2D, 0x99, 0x31, 0x8E, 0x0C, 0x43, 0xE6, 0x82, 0xFE, 0x07, 0x46, 0xE1,
    0x2E, 0x50, 0xD4, 0x1F, 0x2D, 0x2F, 0x7E, 0xD9, 0x08, 0xBA, 0x06, 0xB3, 0xBF, 0x2E, 0x20, 0x3F, 0x4E, 0x3F, 0xFE, 0x44,
    0xFF, 0xAA, 0x50, 0x43, 0x57, 0x91, 0x69, 0x94, 0x49, 0x15, 0x82, 0x82, 0xE4, 0x0F, 0x4C, 0x8D, 0x9D, 0x2C, 0xC9, 0x5B,
    0x1D, 0x64, 0xBF, 0x88, 0x8B, 0xD4, 0xC5, 0x94, 0xE7, 0x65, 0x47, 0x84, 0x1E, 0xE5, 0x79, 0x10, 0xFB, 0x98, 0x93, 0x47,
    0xB9, 0x7D, 0x85, 0x12, 0xA6, 0x40, 0x98, 0x2C, 0xF7, 0x92, 0xBC, 0x95, 0x19, 0x32, 0xED, 0xE8, 0x90, 0x56, 0x0D, 0x65,
    0xC1, 0xAA, 0x78, 0xC6, 0x2E, 0x54, 0xFD, 0x5F, 0x54, 0xA1, 0xF6, 0x7E, 0xE5, 0xE0, 0x5F, 0x61, 0xC1, 0x20, 0xB4, 0xB9,
    0xB4, 0x33, 0x08, 0x70, 0xE4, 0xDF, 0x89, 0x56, 0xED, 0x01, 0x29, 0x46, 0x77, 0x5F, 0x8C, 0xB8, 0xA9, 0xF5, 0x1E, 0x2E,
    0xB3, 0xB9, 0xBF, 0xE0, 0x09, 0xB7, 0x8D, 0x28, 0xD4, 0xA6, 0xC3, 0xB8, 0x1E, 0x1F, 0x07, 0xEB, 0xB4, 0x12, 0x0B, 0x95,
    0xB8, 0x85, 0x30, 0xFD, 0xDC, 0x39, 0x13, 0xD0, 0x7C, 0xDC, 0x8F, 0xED, 0xF9, 0xC9, 0xA3, 0xC1,
};

// Deterministic data that zlib cannot shrink, so its package blocks are stored whole.
inline std::string Noise(size_t size, uint32_t seed) {
    std::string out(size, '\0');
    uint32_t state = seed * 2654435761u + 1;
    for (char& c : out) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        c = static_cast<char>(state >> 11);
    }
    return out;
}

namespace detail {

constexpr size_t kLimbs = 65;
using Big = std::array<uint32_t, kLimbs>;

inline bool AtLeast(const Big& a, const Big& b) {
    for (size_t i = kLimbs; i-- > 0;) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return true;
}

inline void Subtract(Big& a, const Big& b) {
    int64_t borrow = 0;
    for (size_t i = 0; i < kLimbs; ++i) {
        int64_t v = int64_t(a[i]) - b[i] - borrow;
        borrow = v < 0;
        a[i] = uint32_t(v + (borrow ? (int64_t(1) << 32) : 0));
    }
}

inline void Add(Big& a, const Big& b) {
    uint64_t carry = 0;
    for (size_t i = 0; i < kLimbs; ++i) {
        const uint64_t v = uint64_t(a[i]) + b[i] + carry;
        a[i] = uint32_t(v);
        carry = v >> 32;
    }
}

inline void Double(Big& a) {
    uint32_t carry = 0;
    for (size_t i = 0; i < kLimbs; ++i) {
        const uint32_t next = a[i] >> 31;
        a[i] = (a[i] << 1) | carry;
        carry = next;
    }
}

inline Big ModMul(const Big& a, const Big& b, const Big& n) {
    Big result{};
    for (int bit = 2047; bit >= 0; --bit) {
        Double(result);
        if (AtLeast(result, n)) {
            Subtract(result, n);
        }
        if ((b[size_t(bit) / 32] >> (bit % 32)) & 1) {
            Add(result, a);
            if (AtLeast(result, n)) {
                Subtract(result, n);
            }
        }
    }
    return result;
}

inline Big FromBigEndian(const uint8_t* bytes, size_t size) {
    Big out{};
    for (size_t i = 0; i < size; ++i) {
        out[(size - 1 - i) / 4] |= uint32_t(bytes[i]) << (8 * ((size - 1 - i) % 4));
    }
    return out;
}

// RSAES-PKCS1-v1_5 with the public exponent 65537.
inline std::array<uint8_t, 256> RsaEncrypt(const uint8_t (&modulus)[256], const std::array<uint8_t, 32>& message) {
    uint8_t block[256];
    block[0] = 0;
    block[1] = 2;
    const size_t padding = sizeof(block) - 3 - message.size();
    for (size_t i = 0; i < padding; ++i) {
        block[2 + i] = uint8_t(0x21 + i % 200);
    }
    block[2 + padding] = 0;
    std::memcpy(block + 3 + padding, message.data(), message.size());
    const Big n = FromBigEndian(modulus, sizeof(modulus)), m = FromBigEndian(block, sizeof(block));
    Big power = m;
    for (int i = 0; i < 16; ++i) {
        power = ModMul(power, power, n);
    }
    const Big c = ModMul(power, m, n);
    std::array<uint8_t, 256> out{};
    for (size_t i = 0; i < out.size(); ++i) {
        out[out.size() - 1 - i] = uint8_t(c[i / 4] >> (8 * (i % 4)));
    }
    return out;
}

inline void PutBe(Bytes& out, size_t at, uint64_t value, size_t bytes) {
    for (size_t i = 0; i < bytes; ++i) {
        out[at + i] = uint8_t(value >> (8 * (bytes - 1 - i)));
    }
}

template <typename T> void Put(Bytes& out, size_t at, T value) {
    std::memcpy(out.data() + at, &value, sizeof(T));
}

constexpr uint64_t kBlock = 0x10000;
constexpr size_t kInode = 0xA8;
constexpr size_t kSector = 0x1000;

struct Node {
    bool directory = false;
    std::string name;
    std::string path;
    const Bytes* data = nullptr;
    std::vector<size_t> children;
    uint32_t blocks = 0, loc = 0;
};

struct Pfs {
    Bytes inner;
    uint64_t blocks = 0;
};

// The inner file system: header block, inode block(s), then one block per directory and the files' blocks.
inline Pfs BuildInnerPfs(const std::map<std::string, Bytes>& files) {
    std::vector<Node> nodes(2);
    nodes[0].directory = true;
    nodes[1].directory = true;
    nodes[0].children = {1};
    nodes[1].name = "uroot";
    std::map<std::string, size_t> directories{{"", 1}};
    for (const auto& [path, data] : files) {
        size_t parent = 1, from = 0;
        for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', from)) {
            const std::string dir = path.substr(0, slash);
            auto [it, fresh] = directories.try_emplace(dir, nodes.size());
            if (fresh) {
                Node node;
                node.directory = true;
                node.name = path.substr(from, slash - from);
                node.path = dir;
                nodes.push_back(node);
                nodes[parent].children.push_back(it->second);
            }
            parent = it->second;
            from = slash + 1;
        }
        Node file;
        file.name = path.substr(from);
        file.path = path;
        file.data = &data;
        nodes.push_back(file);
        nodes[parent].children.push_back(nodes.size() - 1);
    }
    if (nodes.size() * kInode > kBlock) {
        throw std::runtime_error("synthetic package: too many files");
    }
    uint32_t next = 2;
    for (Node& node : nodes) {
        node.blocks = node.directory ? 1 : uint32_t((node.data->size() + kBlock - 1) / kBlock);
        node.loc = next;
        next += node.blocks;
    }

    Pfs pfs;
    pfs.blocks = next;
    pfs.inner.assign(size_t(next) * kBlock, 0);
    Put<int64_t>(pfs.inner, 0, 1);
    Put<int64_t>(pfs.inner, 8, 20130315);
    Put<int32_t>(pfs.inner, 32, int32_t(kBlock));
    Put<int64_t>(pfs.inner, 40, int64_t(next));
    Put<int64_t>(pfs.inner, 48, int64_t(nodes.size()));
    Put<int64_t>(pfs.inner, 64, 1);
    Put<int64_t>(pfs.inner, 72, 0);
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        const size_t at = kBlock + i * kInode;
        Put<uint16_t>(pfs.inner, at, node.directory ? 0x41ed : 0x81a4);
        Put<uint16_t>(pfs.inner, at + 2, 1);
        Put<int64_t>(pfs.inner, at + 8, int64_t(node.directory ? kBlock : node.data->size()));
        Put<int64_t>(pfs.inner, at + 16, int64_t(node.directory ? kBlock : node.data->size()));
        Put<uint32_t>(pfs.inner, at + 0x60, node.blocks);
        Put<uint32_t>(pfs.inner, at + 0x64, node.loc);
        const size_t body = size_t(node.loc) * kBlock;
        if (!node.directory) {
            if (!node.data->empty()) {
                std::memcpy(pfs.inner.data() + body, node.data->data(), node.data->size());
            }
            continue;
        }
        size_t cursor = body;
        for (const size_t child : node.children) {
            const std::string& name = nodes[child].name;
            const size_t entry = (16 + name.size() + 1 + 7) & ~size_t(7);
            Put<int32_t>(pfs.inner, cursor, int32_t(child));
            Put<int32_t>(pfs.inner, cursor + 4, nodes[child].directory ? 3 : 2);
            Put<int32_t>(pfs.inner, cursor + 8, int32_t(name.size()));
            Put<int32_t>(pfs.inner, cursor + 12, int32_t(entry));
            std::memcpy(pfs.inner.data() + cursor + 16, name.data(), name.size());
            cursor += entry;
        }
    }
    return pfs;
}

inline Bytes Deflate(const uint8_t* data, size_t size) {
    uLongf length = compressBound(uLong(size));
    Bytes out(length);
    if (compress2(out.data(), &length, data, uLong(size), 1) != Z_OK) {
        throw std::runtime_error("synthetic package: zlib failed");
    }
    out.resize(length);
    return out;
}

// The compressed PFS image: header, block map, then each block deflated when that makes it shorter.
inline Bytes BuildPfsc(const Pfs& pfs) {
    const uint64_t map_at = 0x400, count = pfs.blocks;
    uint64_t at = (map_at + (count + 1) * 8 + 0xFFF) & ~uint64_t(0xFFF);
    Bytes out(size_t(at), 0);
    Put<int32_t>(out, 0, 0x43534650);
    Put<int32_t>(out, 12, int32_t(kBlock));
    Put<int64_t>(out, 16, int64_t(kBlock));
    Put<int64_t>(out, 24, int64_t(map_at));
    Put<uint64_t>(out, 32, at);
    Put<int64_t>(out, 40, int64_t(count * kBlock));
    for (uint64_t b = 0; b < count; ++b) {
        Put<uint64_t>(out, size_t(map_at + b * 8), at);
        const uint8_t* block = pfs.inner.data() + b * kBlock;
        const Bytes packed = Deflate(block, size_t(kBlock));
        const uint8_t* stored = packed.size() < kBlock ? packed.data() : block;
        const size_t length = packed.size() < kBlock ? packed.size() : size_t(kBlock);
        out.insert(out.end(), stored, stored + length);
        at += length;
    }
    Put<uint64_t>(out, size_t(map_at + count * 8), at);
    return out;
}

inline void XtsEncrypt(Bytes& image, size_t from, const uint8_t (&digest)[32]) {
    osl::crypto::AesKey tweak_key, data_key;
    if (!tweak_key.Init(osl::crypto::AesMode::kEcb, digest) || !data_key.Init(osl::crypto::AesMode::kEcb, digest + 16)) {
        throw std::runtime_error("synthetic package: no AES");
    }
    Bytes mask(kSector);
    for (size_t at = from; at < image.size(); at += kSector) {
        uint8_t tweak[16] = {};
        const uint64_t sector = at / kSector;
        std::memcpy(tweak, &sector, sizeof(sector));
        tweak_key.Encrypt(tweak, sizeof(tweak));
        uint64_t low, high;
        std::memcpy(&low, tweak, 8);
        std::memcpy(&high, tweak + 8, 8);
        for (size_t block = 0; block < kSector; block += 16) {
            std::memcpy(&mask[block], &low, 8);
            std::memcpy(&mask[block + 8], &high, 8);
            const uint64_t carry = high >> 63;
            high = (high << 1) | (low >> 63);
            low = (low << 1) ^ (carry ? 0x87 : 0);
        }
        uint8_t* data = &image[at];
        for (size_t i = 0; i < kSector; ++i) {
            data[i] ^= mask[i];
        }
        data_key.Encrypt(data, kSector);
        for (size_t i = 0; i < kSector; ++i) {
            data[i] ^= mask[i];
        }
    }
}

} // namespace detail

struct Spec {
    std::string content_id;
    Bytes sfo;
    std::map<std::string, Bytes> files; // the inner file system, '/' separated
    uint32_t image_key_index = 3;       // 3 is a fake (decryptable) package; any other models a console-encrypted one
};

inline Bytes Build(const Spec& spec) {
    using namespace detail;
    const std::array<uint8_t, 32> dk3 = {0x31, 0x41, 0x59, 0x26, 0x53, 0x58, 0x97, 0x93, 0x23, 0x84, 0x62,
                                         0x64, 0x33, 0x83, 0x27, 0x95, 0x02, 0x88, 0x41, 0x97, 0x16, 0x93,
                                         0x99, 0x37, 0x51, 0x05, 0x82, 0x09, 0x74, 0x94, 0x45, 0x92};
    const std::array<uint8_t, 32> ekpfs = {0x27, 0x18, 0x28, 0x18, 0x28, 0x45, 0x90, 0x45, 0x23, 0x53, 0x60,
                                           0x28, 0x74, 0x71, 0x35, 0x26, 0x62, 0x49, 0x77, 0x57, 0x24, 0x70,
                                           0x93, 0x69, 0x99, 0x59, 0x57, 0x49, 0x66, 0x96, 0x76, 0x27};

    const Pfs pfs = BuildInnerPfs(spec.files);
    Bytes pfsc = BuildPfsc(pfs);
    Bytes outer(0x20000, 0);
    const uint8_t seed[16] = {'o', 's', 'l', '-', 't', 'e', 's', 't', '-', 's', 'e', 'e', 'd', 0, 0, 1};
    std::memcpy(&outer[0x370], seed, sizeof(seed));
    outer.insert(outer.end(), pfsc.begin(), pfsc.end());
    outer.resize((outer.size() + kSector - 1) / kSector * kSector, 0);

    uint8_t input[20], digest[32];
    const uint32_t index = 1;
    std::memcpy(input, &index, sizeof(index));
    std::memcpy(input + 4, seed, sizeof(seed));
    if (!osl::crypto::HmacSha256(ekpfs.data(), ekpfs.size(), input, sizeof(input), digest)) {
        throw std::runtime_error("synthetic package: no HMAC");
    }
    XtsEncrypt(outer, 0x20000, digest);

    constexpr uint32_t kEntryKeys = 0x10, kImageKey = 0x20, kParamSfo = 0x1000;
    const size_t entry_count = 3, table_at = 0x1000, keys_size = 32 + 7 * 32 + 4 * 256;
    const size_t keys_at = table_at + entry_count * 32, image_key_at = keys_at + keys_size,
                 sfo_at = image_key_at + 256, pfs_at = (sfo_at + spec.sfo.size() + 0xFFF) & ~size_t(0xFFF);
    Bytes file(pfs_at + outer.size(), 0);

    const auto entry = [&](size_t i, uint32_t id, uint32_t flags2, size_t offset, size_t size) {
        PutBe(file, table_at + i * 32, id, 4);
        PutBe(file, table_at + i * 32 + 12, flags2, 4);
        PutBe(file, table_at + i * 32 + 16, offset, 4);
        PutBe(file, table_at + i * 32 + 20, size, 4);
    };
    entry(0, kEntryKeys, 0, keys_at, keys_size);
    entry(1, kImageKey, spec.image_key_index << 12, image_key_at, 256);
    entry(2, kParamSfo, 0, sfo_at, spec.sfo.size());

    const std::array<uint8_t, 256> key3 = RsaEncrypt(kDk3Modulus, dk3);
    std::memcpy(&file[keys_at + 32 + 7 * 32 + 3 * 256], key3.data(), key3.size());

    std::array<uint8_t, 256> image_key = RsaEncrypt(kFakeModulus, ekpfs);
    uint8_t ivkey_input[64], ivkey[32];
    std::memcpy(ivkey_input, &file[table_at + 32], 32);
    std::memcpy(ivkey_input + 32, dk3.data(), 32);
    osl::crypto::Sha256(ivkey_input, sizeof(ivkey_input), ivkey);
    osl::crypto::AesKey cbc;
    uint8_t iv[16];
    std::memcpy(iv, ivkey, sizeof(iv));
    if (!cbc.Init(osl::crypto::AesMode::kCbc, ivkey + 16) || !cbc.Encrypt(image_key.data(), image_key.size(), iv)) {
        throw std::runtime_error("synthetic package: no AES");
    }
    std::memcpy(&file[image_key_at], image_key.data(), image_key.size());
    std::memcpy(&file[sfo_at], spec.sfo.data(), spec.sfo.size());
    std::memcpy(&file[pfs_at], outer.data(), outer.size());

    PutBe(file, 0, 0x7F434E54, 4);
    PutBe(file, 0x10, entry_count, 4);
    PutBe(file, 0x18, table_at, 4);
    PutBe(file, 0x30, table_at, 8);
    PutBe(file, 0x38, file.size() - table_at, 8);
    std::memcpy(&file[0x40], spec.content_id.data(), std::min<size_t>(spec.content_id.size(), 0x24));
    PutBe(file, 0x410, pfs_at, 8);
    PutBe(file, 0x418, outer.size(), 8);
    PutBe(file, 0x430, file.size(), 8);
    PutBe(file, 0x43C, uint32_t(outer.size()), 4);
    return file;
}

inline void Write(const std::filesystem::path& path, const Bytes& package) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream(path, std::ios::binary | std::ios::trunc)
        .write(reinterpret_cast<const char*>(package.data()), std::streamsize(package.size()));
}

} // namespace osl_test::fakepkg
