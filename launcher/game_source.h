// A read-only view of a game the player selected, an unpacked folder or a package, for diagnostics that must not
// extract or change anything. Every read is bounded and counted.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "cpk_patch.h"
#include "pkg_extract.h"

namespace osl {

inline constexpr size_t kMaxSourceFiles = 5000;
inline constexpr size_t kMaxRangeRead = 16u << 20;

struct SourceEntry {
    std::string path; // relative, '/' separated
    uint64_t size = 0;
};

class GameSource {
public:
    virtual ~GameSource() = default;
    virtual std::string Kind() const = 0;
    // At most kMaxSourceFiles files.
    virtual std::vector<SourceEntry> Files() const = 0;

    // The whole file; false when it is absent, unreadable or larger than `max_bytes`.
    bool Read(const std::string& path, uint64_t max_bytes, std::vector<uint8_t>& out) const;
    // `length` bytes from `offset`; false when the range leaves the file or is longer than kMaxRangeRead.
    bool ReadRange(const std::string& path, uint64_t offset, size_t length, std::vector<uint8_t>& out) const;

    uint64_t bytes_read() const { return bytes_read_; }
    size_t reads() const { return reads_; }

protected:
    virtual bool ReadWhole(const std::string& path, uint64_t max_bytes, std::vector<uint8_t>& out) const = 0;
    virtual bool ReadPart(const std::string& path, uint64_t offset, size_t length, std::vector<uint8_t>& out) const = 0;

private:
    mutable uint64_t bytes_read_ = 0;
    mutable size_t reads_ = 0;
};

std::unique_ptr<GameSource> OpenFolderSource(const std::filesystem::path& folder);
// `package` must have loaded its files and outlive the source.
std::unique_ptr<GameSource> OpenPackageSource(Package& package);

// The archive's header and table of contents, read in `chunk`-sized ranges; entries are not read.
bool OpenSourceCpk(const GameSource& source, const SourceEntry& file, CpkArchive& archive, std::string* error = nullptr,
                   size_t chunk = 64u << 10);

// One entry's data, decompressed when stored CRILAYLA-compressed; false when it is larger than `max_bytes`.
bool ReadSourceCpkEntry(const GameSource& source, const std::string& archive_path, const CpkEntry& entry, uint64_t max_bytes,
                        std::vector<uint8_t>& out, std::string* error = nullptr);

} // namespace osl
