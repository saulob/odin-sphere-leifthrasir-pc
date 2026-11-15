#include "game_source.h"

#include <algorithm>
#include <fstream>
#include <istream>
#include <streambuf>
#include <system_error>

#include "utf8.h"

namespace osl {
namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

// A relative path that stays inside the source.
bool IsContained(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string::npos || path.find(':') != std::string::npos) {
        return false;
    }
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part(path.data() + start, end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return true;
}

class FolderSource final : public GameSource {
public:
    explicit FolderSource(fs::path root) : root_(std::move(root)) {}

    std::string Kind() const override { return "game folder"; }

    std::vector<SourceEntry> Files() const override {
        std::vector<SourceEntry> files;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && files.size() < kMaxSourceFiles; it.increment(ec)) {
            std::error_code item_ec;
            if (!it->is_regular_file(item_ec) || item_ec) {
                continue;
            }
            std::string relative = Utf8(it->path().lexically_relative(root_));
            std::replace(relative.begin(), relative.end(), '\\', '/');
            const uint64_t size = it->file_size(item_ec);
            files.push_back({std::move(relative), item_ec ? 0 : size});
        }
        std::sort(files.begin(), files.end(), [](const SourceEntry& a, const SourceEntry& b) { return a.path < b.path; });
        return files;
    }

protected:
    bool ReadWhole(const std::string& path, uint64_t max_bytes, Bytes& out) const override {
        std::error_code ec;
        const fs::path file = root_ / PathFromUtf8(path);
        const uint64_t size = fs::file_size(file, ec);
        return !ec && size <= max_bytes && ReadPart(path, 0, static_cast<size_t>(size), out);
    }

    bool ReadPart(const std::string& path, uint64_t offset, size_t length, Bytes& out) const override {
        std::error_code ec;
        const fs::path file = root_ / PathFromUtf8(path);
        if (!fs::is_regular_file(file, ec)) {
            return false;
        }
        const uint64_t size = fs::file_size(file, ec);
        if (ec || offset > size || length > size - offset) {
            return false;
        }
        std::ifstream in(file, std::ios::binary);
        out.resize(length);
        if (!in || !in.seekg(static_cast<std::streamoff>(offset)) ||
            !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(length))) {
            return false;
        }
        return true;
    }

private:
    fs::path root_;
};

class PackageSource final : public GameSource {
public:
    explicit PackageSource(Package& package) : package_(package), files_(package.Files()) {}

    std::string Kind() const override { return "package"; }

    std::vector<SourceEntry> Files() const override {
        std::vector<SourceEntry> files;
        for (const PackageFile& file : files_) {
            if (files.size() >= kMaxSourceFiles) {
                break;
            }
            files.push_back({file.path, file.size});
        }
        std::sort(files.begin(), files.end(), [](const SourceEntry& a, const SourceEntry& b) { return a.path < b.path; });
        return files;
    }

protected:
    bool ReadWhole(const std::string& path, uint64_t max_bytes, Bytes& out) const override {
        return package_.ReadFile(path, max_bytes, out).ok();
    }

    bool ReadPart(const std::string& path, uint64_t offset, size_t length, Bytes& out) const override {
        return package_.ReadFileRange(path, offset, length, out).ok();
    }

private:
    Package& package_;
    std::vector<PackageFile> files_;
};

// A seekable stream over one file of a source, filled one range read at a time.
class RangeBuffer final : public std::streambuf {
public:
    RangeBuffer(const GameSource& source, std::string path, uint64_t size, size_t chunk)
        : source_(source), path_(std::move(path)), size_(static_cast<off_type>(size)), chunk_size_(chunk) {}

protected:
    int_type underflow() override {
        if (gptr() && gptr() < egptr()) {
            return traits_type::to_int_type(*gptr());
        }
        const off_type start = base_ + (gptr() ? gptr() - eback() : 0);
        if (start >= size_) {
            return traits_type::eof();
        }
        const size_t length = static_cast<size_t>(std::min<off_type>(static_cast<off_type>(chunk_size_), size_ - start));
        if (!source_.ReadRange(path_, static_cast<uint64_t>(start), length, chunk_) || chunk_.size() != length) {
            return traits_type::eof();
        }
        base_ = start;
        char* data = reinterpret_cast<char*>(chunk_.data());
        setg(data, data, data + length);
        return traits_type::to_int_type(*gptr());
    }

    pos_type seekoff(off_type offset, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        const off_type current = base_ + (gptr() ? gptr() - eback() : 0);
        const off_type target = dir == std::ios_base::beg ? offset : dir == std::ios_base::cur ? current + offset : size_ + offset;
        if (target < 0 || target > size_) {
            return pos_type(off_type(-1));
        }
        base_ = target;
        setg(nullptr, nullptr, nullptr);
        return pos_type(target);
    }

    pos_type seekpos(pos_type position, std::ios_base::openmode mode) override {
        return seekoff(off_type(position), std::ios_base::beg, mode);
    }

private:
    const GameSource& source_;
    std::string path_;
    off_type size_;
    size_t chunk_size_;
    off_type base_ = 0;
    Bytes chunk_;
};

} // namespace

bool GameSource::Read(const std::string& path, uint64_t max_bytes, Bytes& out) const {
    out.clear();
    if (!IsContained(path) || !ReadWhole(path, max_bytes, out) || out.size() > max_bytes) {
        out.clear();
        return false;
    }
    ++reads_;
    bytes_read_ += out.size();
    return true;
}

bool GameSource::ReadRange(const std::string& path, uint64_t offset, size_t length, Bytes& out) const {
    out.clear();
    if (!IsContained(path) || length > kMaxRangeRead || !ReadPart(path, offset, length, out) || out.size() != length) {
        out.clear();
        return false;
    }
    ++reads_;
    bytes_read_ += out.size();
    return true;
}

std::unique_ptr<GameSource> OpenFolderSource(const fs::path& folder) {
    return std::make_unique<FolderSource>(folder);
}

std::unique_ptr<GameSource> OpenPackageSource(Package& package) {
    return std::make_unique<PackageSource>(package);
}

bool OpenSourceCpk(const GameSource& source, const SourceEntry& file, CpkArchive& archive, std::string* error, size_t chunk) {
    RangeBuffer buffer(source, file.path, file.size, std::clamp<size_t>(chunk, 4096, kMaxRangeRead));
    std::istream stream(&buffer);
    return archive.OpenStream(stream, error);
}

bool ReadSourceCpkEntry(const GameSource& source, const std::string& archive_path, const CpkEntry& entry, uint64_t max_bytes,
                        Bytes& out, std::string* error) {
    out.clear();
    if (entry.size > max_bytes || entry.extract_size > max_bytes) {
        if (error) *error = "entry larger than the diagnostic read limit";
        return false;
    }
    out.reserve(entry.size);
    Bytes chunk;
    for (uint64_t done = 0; done < entry.size;) {
        const size_t length = static_cast<size_t>(std::min<uint64_t>(entry.size - done, kMaxRangeRead));
        if (!source.ReadRange(archive_path, entry.offset + done, length, chunk)) {
            out.clear();
            if (error) *error = "entry data unreadable";
            return false;
        }
        out.insert(out.end(), chunk.begin(), chunk.end());
        done += length;
    }
    if (entry.compressed() || (out.size() >= 8 && std::equal(out.begin(), out.begin() + 8, "CRILAYLA"))) {
        Bytes plain = CrilaylaDecompress(out, error);
        out = std::move(plain);
        return !out.empty() || entry.extract_size == 0;
    }
    return true;
}

} // namespace osl
