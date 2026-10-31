// The loader's boot image (boot.bin, content.bin and the image.json report) built from a prepared game tree: the
// eboot mapped, the game's bundled modules linked after it, every other import left by name for the host runtime.
// Same output as scripts/prepare_image.py.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace osl {

// Writes the three files into `out` (created when missing). Empty on success, else the reason.
std::string BuildBootImage(const std::filesystem::path& game, const std::filesystem::path& out);

struct MappedSegment {
    uint64_t vaddr, memsz;
    uint32_t flags; // bit 0: executable
};

struct MappedExecutable {
    std::vector<uint8_t> image; // the loadable segments at their virtual addresses
    std::vector<MappedSegment> segments;
    uint64_t entry = 0;
    uint64_t file_size = 0;
};

// Maps a plaintext PS4 executable read-only into `out`. Empty on success, else the reason; `out` is untouched on failure.
std::string MapExecutable(const std::filesystem::path& file, MappedExecutable& out);

} // namespace osl
