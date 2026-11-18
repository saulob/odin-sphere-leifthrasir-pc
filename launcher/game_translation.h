// The port's Menu & Subtitles languages: English on builds without it and Portuguese (Brazil) everywhere, served over
// a native slot when the game reads its archives (runtime_file_overlay); the archives on disk are never written.
//
// localization/en/sheets/*.json hold the English of every text resource; localization/pt-BR/sheets/*.json one
// Portuguese entry per distinct English text, bound to it by SHA-256; localization/en/sources/<slot>.json, for a slot
// in another language, the hash of each of its records proven to be an English record (`from` names it when it is not
// the record of the same index). A record whose installed text is not the one the corpus was made from keeps the slot's
// text. localization/pt-BR/font/glyphs.json says, per executable, which letters its fonts lack and how they are drawn
// (latin_font.h); a record with a letter the build cannot draw keeps the slot's text rather than lose its accents.
#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cpk_patch.h"
#include "embedded_file.h"
#include "game_profile.h"
#include "game_text.h"
#include "latin_font.h"

namespace osl {

// localization/en/sheets, localization/en/sources, localization/pt-BR/sheets and localization/pt-BR/font, embedded
// at build time; named en/sheets/msgsheet.json, en/sources/JP.json, pt-BR/sheets/story.json, pt-BR/font/glyphs.json.
std::span<const EmbeddedFile> LocalizationFiles();

struct GlyphFamily {
    struct Atlas {
        std::string archive, entry;
        uint64_t size = 0;
        uint32_t crc32 = 0;
        AtlasGeometry geometry;
    };
    std::vector<std::pair<uint64_t, std::array<uint8_t, 16>>> descriptors; // image offset and header
    std::vector<Atlas> atlases;
    std::vector<GlyphRecipe> glyphs;
    std::vector<uint32_t> templates; // per glyph: the code whose record it copies
    std::set<uint32_t> native;       // letters the family already draws
};

struct GlyphBuild {
    std::string eboot_sha256, label;
    std::vector<GlyphFamily> families;
    uint64_t sjis_table = 0; // image offset of the Shift-JIS decoder's page pointers; 0 when the build has none
    uint8_t sjis_lead = 0;
    std::map<uint32_t, uint8_t> sjis_letters; // letter -> trail byte of an unassigned code
};

struct TextCorpus {
    std::unordered_map<std::string, std::vector<std::string>> english;           // resource -> units
    std::unordered_map<std::string, std::string> portuguese;                     // English SHA-256 -> text
    std::map<std::pair<std::string, size_t>, std::pair<std::string, std::string>> portuguese_only; // record -> hash, text
    // slot file (JP, CN) -> resource -> index -> accepted 16-hex prefixes of the slot record's stored bytes
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<size_t, std::vector<std::string>>>> maps;
    // slot file -> resource -> slot index -> English index, where they differ
    std::unordered_map<std::string, std::unordered_map<std::string, std::unordered_map<size_t, size_t>>> sources;
    std::vector<GlyphBuild> glyph_builds;
    // pt-BR/visual/untranslated.json: localized visual entries the Portuguese (Brazil) art does not cover yet
    std::vector<std::string> untranslated_visuals;
    std::vector<std::string> errors;
};

// The corpus in `files` (LocalizationFiles() by default); errors name every file that does not parse.
TextCorpus LoadTextCorpus(std::span<const EmbeddedFile> files);
const TextCorpus& EmbeddedTextCorpus();

struct GlyphPatch {
    uint64_t descriptor = 0;
    std::array<uint8_t, 16> header{};
    uint32_t code = 0, template_code = 0;
    uint16_t cell = 0;
};

struct SjisPatch {
    uint64_t table = 0;
    uint8_t lead = 0, trail = 0;
    uint16_t code = 0;
};

// What the build's fonts can draw for the language, and the changes that make it so.
struct TextGlyphs {
    bool known = false;                  // the executable is one glyphs.json describes
    std::set<uint32_t> letters;          // accented letters every text font draws
    std::map<uint32_t, std::pair<uint8_t, uint8_t>> sjis; // letter -> its Shift-JIS surrogate
    std::vector<GlyphPatch> patches;
    std::vector<SjisPatch> sjis_patches;
    std::vector<std::pair<std::string, std::pair<std::string, std::vector<uint8_t>>>> atlases; // archive, entry, bytes
    size_t drawn = 0;                    // letters drawn into atlases
    std::vector<std::string> notes;      // why a family or atlas was left as it is
};

struct ServedArchive {
    std::string archive;     // file name in the game folder
    ArchiveOverlay overlay;
    size_t entries = 0;      // entries replaced
};

// A localized visual entry whose first read the runtime reports for a Portuguese (Brazil) run (runtime_file_watch).
enum class VisualWatchKind { kServed, kPrologue, kMissed, kUntranslated };
struct VisualWatch {
    std::string archive, entry;
    uint64_t offset = 0, size = 0;  // as the game reads the archive (after the overlay)
    VisualWatchKind kind = VisualWatchKind::kServed;
};

struct TextTranslation {
    std::vector<ServedArchive> archives;
    std::vector<VisualWatch> visual_watches;
    size_t corpus = 0;   // translatable records the slot was checked for
    size_t served = 0;   // records shown in the language
    size_t kept = 0;     // records left in the slot's own text
    size_t pages = 0;    // ArchiveViewer blocks served
    size_t visuals = 0;  // texture and layout entries replaced by localization/pt-BR/visual
    size_t visual_targets = 0; // entries of the slot that some visual patch names
    size_t prologue_pages = 0; // chapter prologue layouts (Classic, HD) served with their atlas
    std::vector<std::string> visual_skipped; // visual targets the slot holds in another form, and why
    TextGlyphs glyphs;
    std::map<std::string, size_t> reasons; // why records were kept
    std::vector<std::string> problems;     // archive-level failures
};

// The fonts' letters for `language` on the build whose eboot.bin has `eboot_sha256`: for Portuguese (Brazil) the
// missing letters are drawn into the atlases of `app0`'s slot archives when their fingerprints match.
TextGlyphs PrepareGlyphs(const std::filesystem::path& app0, const GameBuild& build, const TextLanguage& language,
                         const std::string& eboot_sha256, const TextCorpus& corpus);

// localization/pt-BR/visual/*.dcpx: Portuguese art as block deltas against the slot's own entries.
std::vector<BlockPatch> PortugueseVisualPatches();

// localization/pt-BR/visual/*.oslv: entries rebuilt from the player's exact entry by copy and add operations; the result
// may change size (a layout that grows). scripts/localization/oslv.py writes them.
struct VisualDeltaTarget {
    std::string name;
    std::string file;  // the embedded .oslv it came from
    uint32_t size = 0, crc32 = 0, result_size = 0, result_crc32 = 0;
    struct Op {
        bool copy = true;
        uint32_t offset = 0, length = 0;  // copy: source range; add: range in `added`
    };
    // Sources of one OSLV3 target share the operations and the added bytes.
    std::shared_ptr<const std::vector<Op>> ops;
    std::shared_ptr<const std::vector<uint8_t>> added;
};
std::optional<std::vector<VisualDeltaTarget>> ParseVisualDelta(std::span<const uint8_t> bytes, std::string* error = nullptr);
// The rebuilt entry, or nothing when `source` is not the target's exact source or the result's CRC32 differs.
std::optional<std::vector<uint8_t>> ApplyVisualDelta(const VisualDeltaTarget& target, std::span<const uint8_t> source);
std::vector<VisualDeltaTarget> PortugueseVisualDeltas();
// The chapter prologue pages (Installation layouts and atlas) of the European Spanish slot, laid on per archive whole.
inline constexpr std::string_view kPrologueVisuals = "pt-BR/visual/prologues-ptbr.oslv";

// The slot's archives in `app0` with every record the language can take replaced. Nothing to serve for a native
// language. Never writes to `app0`.
TextTranslation TranslateGameText(const std::filesystem::path& app0, const GameBuild& build, const TextLanguage& language,
                                  const TextCorpus& corpus = EmbeddedTextCorpus());

// The units to serve for one resource of the slot, and the count served; `reasons` gets why the others are kept.
// `english` is the corpus English of the resource (nullptr when the slot is the English one); `map` the slot's source
// map of the resource, or nullptr to prove alignment from the records themselves; `from` its English indices.
// `index_aligned` takes the English source aligned by record index, without the slot's own record shapes.
std::vector<std::string> ServedUnits(const std::string& resource, const TextContainer& slot, const TextLanguage& language,
                                     const TextCorpus& corpus, const std::vector<std::string>* english,
                                     const std::unordered_map<size_t, std::vector<std::string>>* map,
                                     const std::unordered_map<size_t, size_t>* from, bool index_aligned,
                                     const TextGlyphs& glyphs, size_t& served, size_t& candidates,
                                     std::map<std::string, size_t>& reasons);

// The first letter of `text` the glyphs cannot draw (U+00E3 as "ã U+00E3"), or empty.
std::string MissingLetter(std::string_view text, const TextGlyphs& glyphs);
// The text as Shift-JIS with the build's surrogates for letters; nothing when a character has no code.
std::optional<std::string> ShiftJisText(std::string_view text, const TextGlyphs& glyphs);

} // namespace osl
