// Block patches for files inside the game's CRI CPK archives (Odin2_*.CPK), applied in place and reversibly: the
// patched copy of an entry is appended, uncompressed, to the end of the archive and the entry's row in the table of
// contents is pointed at it. Nothing of the original data is overwritten, and an undo record next to the archive
// (<name>.CPK.undo) holds what revert needs: the original table of contents and header bytes and the original length.
// The Controller prompts setting is built on this. Taken from Dragon's Crown Pro PC.
//
// A DCPX patch (scripts/prompts/make_dcpx.py) names the entries it changes with the CRC32 of the entry before and
// after, so an archive that does not match is left alone, and the 16-byte runs that differ. Several targets may share
// a name, one per copy of the file the builds and languages ship; an entry takes the one its CRC32 matches. DCPX2 runs
// also hold the original block, so a file of an unknown CRC32 is still taken when every block a target replaces is
// exactly that target's original.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace osl {

struct BlockRun {
    uint32_t offset = 0;
    std::array<uint8_t, 16> original{}; // DCPX2 only
    std::array<uint8_t, 16> data{};     // the replacement
};

struct BlockPatchTarget {
    std::string name; // path inside the archive, as the table of contents spells it: stage/Interface_US.ftx
    uint32_t size = 0; // of the uncompressed entry
    uint32_t crc_before = 0;
    uint32_t crc_after = 0;
    bool source_blocks = false; // the runs hold their original blocks (DCPX2)
    std::vector<BlockRun> runs;
};

struct BlockPatch {
    std::vector<BlockPatchTarget> targets;
};

// Nothing when the bytes are not a DCPX patch; `error` says why.
std::optional<BlockPatch> ParseBlockPatch(std::span<const uint8_t> bytes, std::string* error = nullptr);

// One entry of an archive's table of contents.
struct CpkEntry {
    std::string path;      // DirName/FileName
    uint32_t size = 0;     // bytes stored
    uint32_t extract_size = 0;
    uint64_t offset = 0;   // in the archive
    uint32_t id = 0;
    size_t row = 0;        // row in the TOC table
    bool compressed() const { return size < extract_size; }
};

// Reads the header and table of contents of a CPK archive; entries are read on demand.
class CpkArchive {
public:
    // False when the file is not a CPK the reader understands; `error` says why.
    bool Open(const std::filesystem::path& file, std::string* error = nullptr);
    const std::vector<CpkEntry>& entries() const { return entries_; }
    const CpkEntry* Find(std::string_view path) const; // case-insensitive, nothing when absent
    // The entry's data, decompressed when it is stored CRILAYLA-compressed; empty with `error` set on failure.
    std::vector<uint8_t> Read(const CpkEntry& entry, std::string* error = nullptr) const;

    const std::filesystem::path& file() const { return file_; }
    uint64_t toc_offset() const { return toc_offset_; }
    uint64_t toc_base() const { return toc_base_; }
    uint32_t align() const { return align_; }
    // The header's EnableTocCrc value (0 when the column is absent).
    uint32_t enable_toc_crc() const { return enable_toc_crc_; }

private:
    friend struct PatchWriter;
    std::filesystem::path file_;
    std::vector<CpkEntry> entries_;
    uint64_t toc_offset_ = 0, toc_base_ = 0, content_offset_ = 0;
    uint32_t align_ = 0x800;
    uint32_t enable_toc_crc_ = 0;
    // The raw packets (decrypted) and where their per-row numeric fields sit, for rewriting.
    struct Field {
        size_t pos;
        int type;
    };
    struct Table {
        bool encrypted = false;
        uint64_t file_pos = 0; // of the packet in the archive (after the 16-byte section header)
        std::vector<uint8_t> packet;
        std::vector<std::vector<std::pair<std::string, Field>>> fields; // per row
        const Field* field(size_t row, std::string_view name) const {
            if (row >= fields.size()) return nullptr;
            for (const auto& [n, f] : fields[row]) {
                if (n == name) return &f;
            }
            return nullptr;
        }
    };
    Table header_, toc_;
};

// Decompresses one CRILAYLA block as stored in a CPK entry; empty with `error` set when it is malformed.
std::vector<uint8_t> CrilaylaDecompress(std::span<const uint8_t> data, std::string* error = nullptr);

enum class ArchivePatchState { kOriginal, kPatched };

// A patch layer is named by its undo record's suffix. Layers revert last in, first out: the title menu patch goes on
// top of the controller prompts patch and comes off first.
inline constexpr std::string_view kUndoSuffix = ".undo";
inline constexpr std::string_view kTitleUndoSuffix = ".title.undo";

std::filesystem::path UndoPath(const std::filesystem::path& cpk, std::string_view suffix = kUndoSuffix);
// Patched when the undo record next to the archive exists.
ArchivePatchState ArchiveState(const std::filesystem::path& cpk, std::string_view suffix = kUndoSuffix);
// The entries the archive's undo record says were rewritten; empty when it is not patched or the record is unreadable.
std::vector<std::string> PatchedEntries(const std::filesystem::path& cpk, std::string_view suffix = kUndoSuffix);

struct PatchHooks {
    std::function<void(const char* text)> phase;                     // "Reading stage/Interface_US.ftx..."
    std::function<bool(uint64_t done, uint64_t total)> progress;     // false cancels
};

// An entry the patch names whose data is neither the original nor the patched copy of any of its targets.
struct UnmatchedEntry {
    std::string archive;
    std::string entry;
    uint64_t size = 0;
    uint32_t crc = 0;
    std::string analysis; // why no target takes it
};

struct PatchResult {
    bool ok = false;
    bool cancelled = false;
    std::string detail;   // for the log
    size_t patched = 0;   // entries rewritten (0 with ok when the archive holds none of the patch's entries)
    size_t skipped = 0;   // entries named by the patch that are not a copy it was made for
    std::vector<UnmatchedEntry> unmatched;
    uint64_t appended = 0;
};

enum class EntryState { kUnknown, kOriginal, kPatched };

struct EntryMatch {
    EntryState state = EntryState::kUnknown;
    const BlockPatchTarget* target = nullptr; // the copy the entry is, or was made from
    bool exact = false;                       // by CRC32; otherwise by source blocks
    bool ambiguous = false;                   // source-block matches that would write different bytes
    std::string analysis;                     // how a source-block match was made, or why there is none
};

// Whether the entry's data is an original or patched copy of one of the patch's targets. A CRC32 match is preferred.
// Otherwise the data must have the size of a DCPX2 target and equal its original (or its replacement) blocks at every
// run; copies that would give different results are ambiguous and match nothing.
EntryMatch MatchEntry(const BlockPatch& patch, std::string_view name, std::span<const uint8_t> data, uint32_t crc);

// The entries of the archive the patch names that are not a copy it was made for; nothing is written.
std::vector<UnmatchedEntry> FindUnmatchedEntries(const std::filesystem::path& cpk, const BlockPatch& patch);

// Whether the archive holds this patch and nothing else: every entry the patch names is a patched copy, and the undo
// record names exactly those entries. False when it holds none of them.
bool HoldsBlockPatch(const std::filesystem::path& cpk, const BlockPatch& patch, std::string_view suffix = kUndoSuffix);

// Applies every target of the patch the archive holds. An archive that is already patched is left alone (ok, 0
// patched); an entry that is not the file the patch was made for is skipped and named in `detail`.
PatchResult ApplyBlockPatch(const std::filesystem::path& cpk, const BlockPatch& patch, const PatchHooks& hooks = {},
                            std::string_view suffix = kUndoSuffix);
// Puts the archive back as it was, from the undo record, and removes the record.
PatchResult RevertBlockPatch(const std::filesystem::path& cpk, std::string_view suffix = kUndoSuffix);

uint32_t Crc32(std::span<const uint8_t> bytes);

} // namespace osl
