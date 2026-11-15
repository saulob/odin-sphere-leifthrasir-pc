// The loader's boot image (boot.bin, content.bin and the image.json report) built from a prepared game tree: the
// eboot mapped, the game's bundled modules linked after it, every other import left by name for the host runtime.
// Same output as scripts/prepare_image.py.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace osl {

// Bundled modules are linked after the eboot, each at the next boundary of this alignment.
inline constexpr uint64_t kModuleAlignment = 0x10000;

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
std::string MapExecutableBytes(std::span<const uint8_t> bytes, MappedExecutable& out);

// Appends a mapped module to `image` at the next kModuleAlignment boundary, as the boot image links it. Empty on
// success, else the reason; `base` receives where the module starts.
std::string LinkModule(MappedExecutable& image, const MappedExecutable& module, uint64_t* base = nullptr);

struct ProgramHeaderInfo {
    uint32_t type = 0, flags = 0;
    uint64_t offset = 0, vaddr = 0, filesz = 0, memsz = 0, align = 0;
};

struct SelfSegmentInfo {
    uint64_t flags = 0, offset = 0, size = 0, memory = 0;
    bool blocked() const { return (flags & 0x800) != 0; }
    uint64_t header() const { return (flags >> 20) & 4095; }
};

// The headers of a PS4 executable as far as they can be read, for the log.
struct ExecutableInfo {
    bool self = false; // the SELF magic is present
    uint8_t version = 0, mode = 0, endian = 0, attributes = 0;
    uint32_t key_type = 0;
    uint16_t header_size = 0, meta_size = 0, segment_count = 0, self_flags = 0;
    uint64_t declared_size = 0;
    bool elf = false; // a little-endian x86-64 ELF header follows
    uint16_t elf_type = 0, machine = 0;
    uint64_t entry = 0;
    std::vector<SelfSegmentInfo> segments;
    std::vector<ProgramHeaderInfo> headers;
    std::string error; // why the boot image would refuse it; empty when it parses
};

ExecutableInfo InspectExecutable(std::span<const uint8_t> bytes);

} // namespace osl
