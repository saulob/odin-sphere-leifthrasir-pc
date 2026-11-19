#include "game_translation.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <tuple>

#include <zlib.h>

#include "game_files.h"
#include "json_lite.h"
#include "osl_launcher.h"
#include "prologue_translation.h"
#include "settings.h"
#include "utf8.h"

namespace osl {

extern const EmbeddedFile kLocalizationFiles[];

namespace {

std::string_view View(const EmbeddedFile& f) { return {reinterpret_cast<const char*>(f.data), f.size}; }

bool EndsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string Upper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    return out;
}

uint64_t Hex(const std::string& s) { return s.empty() ? 0 : std::stoull(s, nullptr, 16); }

// The code points of UTF-8 text; U+FFFD for a malformed byte.
std::vector<uint32_t> CodePoints(std::string_view s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const uint8_t c = uint8_t(s[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (!n || i + n > s.size()) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        uint32_t cp = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
        for (size_t k = 1; k < n; ++k) cp = cp << 6 | (uint8_t(s[i + k]) & 0x3F);
        out.push_back(cp);
        i += n;
    }
    return out;
}

bool NeedsGlyph(uint32_t cp) { return (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7) || cp == 0xAA || cp == 0xBA; }

void LoadEnglishSheets(TextCorpus& corpus, const json::Value& root) {
    const json::Value* resources = root.Find("resources");
    if (!resources || !resources->IsArray()) return;
    for (const json::Value& item : resources->array) {
        const json::Value* units = item.Find("units");
        if (!units || !units->IsArray()) continue;
        std::vector<std::string>& out = corpus.english[item.StringOr("resource")];
        for (const json::Value& u : units->array) out.push_back(u.string);
    }
}

void LoadPortuguese(TextCorpus& corpus, const json::Value& root) {
    const json::Value* entries = root.Find("entries");
    if (!entries || !entries->IsArray()) return;
    for (const json::Value& e : entries->array) {
        const std::string hash = e.StringOr("source_sha256"), text = e.StringOr("pt_br");
        const json::Value* only = e.Find("only");
        if (only && only->IsArray() && !only->array.empty()) {
            for (const json::Value& at : only->array) {
                const size_t hash_mark = at.string.rfind('#');
                if (hash_mark == std::string::npos) continue;
                corpus.portuguese_only[{at.string.substr(0, hash_mark), std::stoul(at.string.substr(hash_mark + 1))}] = {hash, text};
            }
        } else {
            corpus.portuguese[hash] = text;
        }
    }
}

void LoadSourceMap(TextCorpus& corpus, const std::string& slot, const json::Value& root) {
    const json::Value* resources = root.Find("resources");
    if (!resources || !resources->IsObject()) return;
    auto& map = corpus.maps[slot];
    for (const auto& [resource, entry] : resources->object) {
        const json::Value* accept = entry.Find("accept");
        if (!accept || !accept->IsObject()) continue;
        auto& records = map[resource];
        for (const auto& [index, hashes] : accept->object) {
            std::vector<std::string>& out = records[std::stoul(index)];
            for (const json::Value& h : hashes.array) out.push_back(h.string);
        }
        if (const json::Value* from = entry.Find("from"); from && from->IsObject()) {
            auto& sources = corpus.sources[slot][resource];
            for (const auto& [index, english] : from->object) sources[std::stoul(index)] = size_t(english.number);
        }
    }
}

void LoadGlyphs(TextCorpus& corpus, const json::Value& root) {
    const json::Value* builds = root.Find("builds");
    if (!builds || !builds->IsArray()) return;
    for (const json::Value& b : builds->array) {
        GlyphBuild build;
        build.eboot_sha256 = b.StringOr("eboot_sha256");
        build.label = b.StringOr("label");
        const json::Value* families = b.Find("families");
        for (const json::Value& f : families ? families->array : std::vector<json::Value>{}) {
            GlyphFamily family;
            for (const json::Value& d : f.Find("descriptors")->array) {
                std::array<uint8_t, 16> header{};
                const std::string hex = d.StringOr("header");
                for (size_t i = 0; i < 16 && 2 * i + 1 < hex.size(); ++i) header[i] = uint8_t(std::stoul(hex.substr(2 * i, 2), nullptr, 16));
                family.descriptors.emplace_back(Hex(d.StringOr("va")), header);
            }
            for (const json::Value& a : f.Find("atlases")->array) {
                family.atlases.push_back({a.StringOr("archive"), a.StringOr("entry"), uint64_t(a.NumberOr("size")),
                                          uint32_t(Hex(a.StringOr("crc32"))),
                                          {int(a.NumberOr("textures")), int(a.NumberOr("cell")), int(a.NumberOr("cols")), int(a.NumberOr("rows"))}});
            }
            for (const json::Value& g : f.Find("glyphs")->array) {
                GlyphRecipe r;
                r.code = uint32_t(g.NumberOr("code"));
                r.cell = int(g.NumberOr("cell"));
                r.base_cell = int(g.NumberOr("base_cell"));
                r.mark = g.StringOr("mark");
                r.donor_cell = g.Find("donor_cell") ? int(g.NumberOr("donor_cell")) : -1;
                r.donor_base_cell = g.Find("donor_base_cell") ? int(g.NumberOr("donor_base_cell")) : -1;
                r.punctuation_cell = g.Find("punctuation_cell") ? int(g.NumberOr("punctuation_cell")) : -1;
                const uint32_t base = uint32_t(g.NumberOr("template"));
                r.base = base;
                r.capital = base >= 'A' && base <= 'Z';
                r.dotless = base == 'i';
                family.glyphs.push_back(r);
                family.templates.push_back(base);
            }
            for (const json::Value& n : f.Find("native")->array) family.native.insert(uint32_t(n.number));
            build.families.push_back(std::move(family));
        }
        if (const json::Value* sjis = b.Find("sjis"); sjis && sjis->IsObject()) {
            build.sjis_table = Hex(sjis->StringOr("table_va"));
            build.sjis_lead = uint8_t(sjis->NumberOr("lead"));
            for (const auto& [code, trail] : sjis->Find("letters")->object) build.sjis_letters[std::stoul(code)] = uint8_t(trail.number);
        }
        corpus.glyph_builds.push_back(std::move(build));
    }
}

bool InSlot(std::string_view file, std::string_view slot) {
    return slot.empty() || EndsWith(Upper(file), "_" + Upper(slot) + ".CPK");
}

// Archives of the slot, lowest priority first: an update's PATCH archives are read after the ones they patch.
std::vector<std::string> SlotArchives(const GameBuild& build, std::string_view slot) {
    std::vector<std::string> names;
    for (const char* file : build.data_files) {
        if (InSlot(file, slot)) names.emplace_back(file);
    }
    std::stable_sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return (Upper(a).find("PATCH") != std::string::npos) < (Upper(b).find("PATCH") != std::string::npos);
    });
    return names;
}

std::string MapFileOf(std::string_view slot) { return slot.empty() ? "JP" : std::string(slot); }

bool WindowAgrees(const std::vector<std::string>& english, const std::vector<std::string>& slot, size_t i) {
    const size_t from = i >= 2 ? i - 2 : 0, to = std::min(english.size(), i + 3);
    for (size_t k = from; k < to; ++k) {
        if (Shape(english[k]) != Shape(slot[k])) return false;
    }
    return true;
}

std::string Utf8Of(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | cp >> 6);
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xE0 | cp >> 12);
        out += char(0x80 | (cp >> 6 & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
    return out;
}

// Shift-JIS codes of the punctuation the corpus uses beside ASCII.
constexpr std::pair<uint32_t, uint16_t> kShiftJis[] = {
    {0x3000, 0x8140}, {0x2014, 0x815C}, {0x2015, 0x815C}, {0x2018, 0x8165}, {0x2019, 0x8166}, {0x201C, 0x8167},
    {0x201D, 0x8168}, {0x2026, 0x8163}, {0x00D7, 0x817E}, {0x2605, 0x819A}, {0x25C6, 0x819F}, {0x25A1, 0x81A0},
    {0x2192, 0x81A8}, {0x2190, 0x81A9}, {0x2191, 0x81AA}, {0x2193, 0x81AB},
};

} // namespace

std::span<const EmbeddedFile> LocalizationFiles() {
    size_t n = 0;
    while (kLocalizationFiles[n].name) ++n;
    return {kLocalizationFiles, n};
}

TextCorpus LoadTextCorpus(std::span<const EmbeddedFile> files) {
    TextCorpus corpus;
    for (const EmbeddedFile& f : files) {
        const std::string name = f.name;
        if (!EndsWith(name, ".json")) continue;
        const std::optional<json::Value> root = json::Parse(View(f));
        if (!root || !root->IsObject()) {
            corpus.errors.push_back(name + ": not JSON");
            continue;
        }
        if (name.rfind("en/sheets/", 0) == 0) {
            LoadEnglishSheets(corpus, *root);
        } else if (name.rfind("pt-BR/sheets/", 0) == 0) {
            LoadPortuguese(corpus, *root);
        } else if (name == "pt-BR/font/glyphs.json") {
            LoadGlyphs(corpus, *root);
        } else if (name == "pt-BR/visual/untranslated.json") {
            if (const json::Value* entries = root->Find("entries")) {
                for (const json::Value& e : entries->array) corpus.untranslated_visuals.push_back(e.string);
            }
        } else if (name.rfind("en/sources/", 0) == 0 && name != "en/sources/US.json") {
            LoadSourceMap(corpus, name.substr(11, name.size() - 11 - 5), *root);
        }
    }
    return corpus;
}

const TextCorpus& EmbeddedTextCorpus() {
    static const TextCorpus corpus = LoadTextCorpus(LocalizationFiles());
    return corpus;
}

std::string MissingLetter(std::string_view text, const TextGlyphs& glyphs) {
    for (const uint32_t cp : CodePoints(text)) {
        if (NeedsGlyph(cp) && !glyphs.letters.count(cp)) {
            char hex[16];
            std::snprintf(hex, sizeof(hex), " U+%04X", unsigned(cp));
            return Utf8Of(cp) + hex;
        }
    }
    return {};
}

std::optional<std::string> ShiftJisText(std::string_view text, const TextGlyphs& glyphs) {
    std::string out;
    for (const uint32_t cp : CodePoints(text)) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (const auto it = glyphs.sjis.find(cp); it != glyphs.sjis.end()) {
            out += char(it->second.first);
            out += char(it->second.second);
        } else {
            const auto code = std::find_if(std::begin(kShiftJis), std::end(kShiftJis), [&](const auto& p) { return p.first == cp; });
            if (code == std::end(kShiftJis)) return std::nullopt;
            out += char(code->second >> 8);
            out += char(code->second & 0xFF);
        }
    }
    return out;
}

TextGlyphs PrepareGlyphs(const std::filesystem::path& app0, const GameBuild& build, const TextLanguage& language,
                         const std::string& eboot_sha256, const TextCorpus& corpus) {
    TextGlyphs out;
    const auto known = std::find_if(corpus.glyph_builds.begin(), corpus.glyph_builds.end(),
                                    [&](const GlyphBuild& b) { return b.eboot_sha256 == eboot_sha256; });
    if (known == corpus.glyph_builds.end()) {
        out.notes.push_back("no glyph data for this eboot.bin (SHA-256 " + eboot_sha256.substr(0, 16) + "): accented letters unavailable");
        return out;
    }
    out.known = true;
    const bool draw = language.source == TextSource::kPortTranslation;
    std::map<std::string, std::unique_ptr<CpkArchive>> archives;
    const auto in_build = [&](const std::string& archive) {
        return std::any_of(build.data_files.begin(), build.data_files.end(),
                           [&](const char* name) { return archive == name; });
    };
    bool first = true;
    for (const GlyphFamily& family : known->families) {
        std::set<uint32_t> letters = family.native;
        bool drawn = draw;
        std::vector<std::pair<std::string, std::pair<std::string, std::vector<uint8_t>>>> atlases;
        for (const GlyphFamily::Atlas& atlas : draw ? family.atlases : std::vector<GlyphFamily::Atlas>{}) {
            const std::string where = atlas.archive + ": " + atlas.entry;
            if (!in_build(atlas.archive)) {
                out.notes.push_back(where + " is not in the build");
                drawn = false;
                break;
            }
            std::unique_ptr<CpkArchive>& open = archives[atlas.archive];
            std::string error;
            if (!open) {
                open = std::make_unique<CpkArchive>();
                if (!open->Open(app0 / atlas.archive, &error)) {
                    out.notes.push_back(where + ": " + error);
                    drawn = false;
                    break;
                }
            }
            const CpkEntry* entry = open->Find(atlas.entry);
            std::vector<uint8_t> data = entry ? open->Read(*entry, &error) : std::vector<uint8_t>{};
            char seen[64];
            std::snprintf(seen, sizeof(seen), "%zu bytes, CRC32 %08x", data.size(), Crc32(data));
            if (data.size() != atlas.size || Crc32(data) != atlas.crc32) {
                out.notes.push_back(where + " is not the font the letters were made for (" + seen + ")");
                drawn = false;
                break;
            }
            if (!ComposeGlyphs(data, atlas.geometry, family.glyphs, &error)) {
                out.notes.push_back(where + ": " + error);
                drawn = false;
                break;
            }
            atlases.push_back({atlas.archive, {entry->path, std::move(data)}});
        }
        if (drawn) {
            for (auto& a : atlases) out.atlases.push_back(std::move(a));
            for (size_t g = 0; g < family.glyphs.size(); ++g) {
                letters.insert(family.glyphs[g].code);
                for (const auto& [descriptor, header] : family.descriptors) {
                    out.patches.push_back({descriptor, header, family.glyphs[g].code, family.templates[g], uint16_t(family.glyphs[g].cell)});
                }
            }
            out.drawn += family.glyphs.size();
        }
        if (first) {
            out.letters = letters;
            first = false;
        } else {
            std::set<uint32_t> both;
            std::set_intersection(out.letters.begin(), out.letters.end(), letters.begin(), letters.end(), std::inserter(both, both.end()));
            out.letters = both;
        }
    }
    if (draw && known->sjis_table) {
        for (const auto& [code, trail] : known->sjis_letters) {
            if (!out.letters.count(code)) continue;
            out.sjis[code] = {known->sjis_lead, trail};
            out.sjis_patches.push_back({known->sjis_table, known->sjis_lead, trail, uint16_t(code)});
        }
    }
    return out;
}

std::vector<std::string> ServedUnits(const std::string& resource, const TextContainer& slot, const TextLanguage& language,
                                     const TextCorpus& corpus, const std::vector<std::string>* english,
                                     const std::unordered_map<size_t, std::vector<std::string>>* map,
                                     const std::unordered_map<size_t, size_t>* from, bool index_aligned,
                                     const TextGlyphs& glyphs, size_t& served, size_t& candidates,
                                     std::map<std::string, size_t>& reasons) {
    std::vector<std::string> out = slot.units;
    const bool native_english = english == nullptr;
    const std::vector<std::string>& source = native_english ? slot.units : *english;
    const bool page = slot.kind == TextKind::kArchivePage;
    if (!native_english && (index_aligned || !page) && source.size() != slot.units.size()) {
        for (const std::string& u : source) {
            if (Translatable(u)) {
                ++candidates;
                ++reasons["record count differs from the English"];
            }
        }
        return out;
    }
    for (size_t i = 0; i < slot.units.size(); ++i) {
        size_t j = i;
        if (from) {
            if (const auto it = from->find(i); it != from->end()) j = it->second;
        }
        if (j >= source.size()) continue;
        const std::string& en = source[j];
        if (!Translatable(en)) continue;
        ++candidates;
        const std::string& native = slot.units[i];
        if (!native_english) {
            if (index_aligned) {
                // The physical base is the localized slot; the English source is the same record of the paired English archive.
            } else if (map) {
                const auto found = map->find(i);
                const std::string stored = TextSha256(native).substr(0, 16);
                if (found == map->end() || std::find(found->second.begin(), found->second.end(), stored) == found->second.end()) {
                    ++reasons["not in the slot's source map"];
                    continue;
                }
            } else if (page || slot.shift_jis) {
                ++reasons["slot without a source map"];
                continue;
            } else if (Arguments(en) != Arguments(native) || Digits(en) != Digits(native) ||
                       !WindowAgrees(source, slot.units, i)) {
                ++reasons["record not proven to be the English one"];
                continue;
            }
        }
        std::string text;
        if (language.source == TextSource::kPortEnglish) {
            text = en;
        } else {
            const std::string hash = TextSha256(en);
            const auto only = corpus.portuguese_only.find({resource, j});
            if (only != corpus.portuguese_only.end() && only->second.first == hash) {
                text = only->second.second;
            } else if (const auto it = corpus.portuguese.find(hash); it != corpus.portuguese.end()) {
                text = it->second;
            } else {
                ++reasons[native_english && corpus.english.count(resource) &&
                                  (corpus.english.at(resource).size() <= j || corpus.english.at(resource)[j] != en)
                              ? "installed English differs from the corpus"
                              : "not translated"];
                continue;
            }
            if (!CheckTranslation(en, text).empty()) {
                ++reasons["translation refused by validation"];
                continue;
            }
        }
        if (!native_english && !index_aligned && Arguments(text) != Arguments(native)) {
            ++reasons["arguments differ from the slot's record"];
            continue;
        }
        if (const std::string missing = MissingLetter(text, glyphs); !missing.empty()) {
            ++reasons["no glyph for " + missing];
            continue;
        }
        if (slot.shift_jis) {
            std::optional<std::string> sjis = ShiftJisText(text, glyphs);
            if (!sjis) {
                ++reasons["no Shift-JIS code for a character"];
                continue;
            }
            text = std::move(*sjis);
        }
        out[i] = std::move(text);
        ++served;
    }
    return out;
}

std::optional<std::vector<VisualDeltaTarget>> ParseVisualDelta(std::span<const uint8_t> b, std::string* error) {
    size_t pos = 0;
    auto fail = [&](const char* why) -> std::optional<std::vector<VisualDeltaTarget>> {
        if (error) *error = why;
        return std::nullopt;
    };
    auto u32 = [&](uint32_t& v) {
        if (pos + 4 > b.size()) return false;
        v = uint32_t(b[pos]) | uint32_t(b[pos + 1]) << 8 | uint32_t(b[pos + 2]) << 16 | uint32_t(b[pos + 3]) << 24;
        pos += 4;
        return true;
    };
    auto is = [&](char version) {
        static constexpr uint8_t kMagic[8] = {'O', 'S', 'L', 'V', '1', 0, 0, 0};
        return b.size() >= 12 && std::equal(kMagic, kMagic + 4, b.begin()) && b[4] == uint8_t(version) &&
               std::equal(kMagic + 5, kMagic + 8, b.begin() + 5);
    };
    const int version = is('3') ? 3 : is('2') ? 2 : is('1') ? 1 : 0;
    if (!version) return fail("not an OSLV file");
    pos = 8;
    uint32_t count = 0;
    u32(count);
    std::vector<VisualDeltaTarget> out;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 2 > b.size()) return fail("truncated");
        const size_t n = b[pos] | b[pos + 1] << 8;
        pos += 2;
        if (pos + n > b.size()) return fail("truncated name");
        const std::string name(reinterpret_cast<const char*>(b.data() + pos), n);
        pos += n;
        std::vector<VisualDeltaTarget> sources(1);
        uint32_t sources_count = 1;
        if (version == 3 && !u32(sources_count)) return fail("truncated");
        if (!sources_count || sources_count > 64) return fail("source count out of range");
        sources.resize(sources_count);
        for (VisualDeltaTarget& t : sources) {
            t.name = name;
            if (!u32(t.size) || !u32(t.crc32) || !u32(t.result_size) || !u32(t.result_crc32)) return fail("truncated");
        }
        uint32_t ops_count = 0, added_size = 0, packed_size = 0;
        if (!u32(ops_count)) return fail("truncated");
        if (version >= 2 && (!u32(added_size) || !u32(packed_size))) return fail("truncated");
        auto ops = std::make_shared<std::vector<VisualDeltaTarget::Op>>();
        auto added = std::make_shared<std::vector<uint8_t>>();
        uint64_t built = 0, added_total = 0;
        for (uint32_t j = 0; j < ops_count; ++j) {
            if (pos >= b.size()) return fail("truncated operation");
            const uint8_t kind = b[pos++];
            VisualDeltaTarget::Op op;
            if (kind == 0) {
                if (!u32(op.offset) || !u32(op.length)) return fail("truncated copy");
                for (const VisualDeltaTarget& t : sources) {
                    if (uint64_t(op.offset) + op.length > t.size) return fail("copy outside the source");
                }
            } else if (kind == 1) {
                if (!u32(op.length)) return fail("truncated add");
                op.copy = false;
                op.offset = uint32_t(added_total);
                added_total += op.length;
                if (version == 1) {
                    if (pos + op.length > b.size()) return fail("truncated add");
                    added->insert(added->end(), b.begin() + pos, b.begin() + pos + op.length);
                    pos += op.length;
                }
            } else {
                return fail("unknown operation");
            }
            built += op.length;
            ops->push_back(op);
        }
        if (version >= 2) {
            if (added_total != added_size || pos + packed_size > b.size()) return fail("added bytes do not fit");
            // zlib inflates at most ~1032:1; a larger claim is refused before allocating for it.
            if (added_size > uint64_t(packed_size) * 1032 + 64) return fail("added bytes exceed their stream");
            added->resize(added_size);
            uLongf size = added_size;
            if (uncompress(added->data(), &size, b.data() + pos, packed_size) != Z_OK || size != added_size) {
                return fail("added bytes do not inflate");
            }
            pos += packed_size;
        }
        for (VisualDeltaTarget& t : sources) {
            if (built != t.result_size) return fail("operations do not build the result size");
            t.ops = ops;
            t.added = added;
            out.push_back(std::move(t));
        }
    }
    if (pos != b.size()) return fail("trailing bytes");
    return out;
}

std::optional<std::vector<uint8_t>> ApplyVisualDelta(const VisualDeltaTarget& t, std::span<const uint8_t> source) {
    if (!t.ops || !t.added || source.size() != t.size || Crc32(source) != t.crc32) return std::nullopt;
    std::vector<uint8_t> out;
    out.reserve(t.result_size);
    for (const VisualDeltaTarget::Op& op : *t.ops) {
        const uint8_t* from = op.copy ? source.data() + op.offset : t.added->data() + op.offset;
        out.insert(out.end(), from, from + op.length);
    }
    if (out.size() != t.result_size || Crc32(out) != t.result_crc32) return std::nullopt;
    return out;
}

std::vector<VisualDeltaTarget> PortugueseVisualDeltas() {
    std::vector<VisualDeltaTarget> out;
    for (const EmbeddedFile& f : LocalizationFiles()) {
        const std::string_view name = f.name;
        if (name.rfind("pt-BR/visual/", 0) != 0 || !EndsWith(name, ".oslv")) continue;
        if (auto targets = ParseVisualDelta({f.data, f.size})) {
            for (VisualDeltaTarget& t : *targets) {
                t.file = name;
                out.push_back(std::move(t));
            }
        }
    }
    return out;
}

std::vector<BlockPatch> PortugueseVisualPatches() {
    std::vector<BlockPatch> out;
    for (const EmbeddedFile& f : LocalizationFiles()) {
        const std::string_view name = f.name;
        if (name.rfind("pt-BR/visual/", 0) != 0 || !EndsWith(name, ".dcpx")) continue;
        if (std::optional<BlockPatch> patch = ParseBlockPatch({f.data, f.size})) out.push_back(std::move(*patch));
    }
    return out;
}

namespace {

// The entry with a visual patch laid on, when the entry is exactly a source the patch was made from.
std::optional<std::vector<uint8_t>> VisualFor(const std::vector<BlockPatch>& patches, const CpkEntry& entry,
                                              const std::vector<uint8_t>& data, std::string& why) {
    const uint32_t crc = Crc32(data);
    bool named = false;
    for (const BlockPatch& patch : patches) {
        if (std::none_of(patch.targets.begin(), patch.targets.end(), [&](const BlockPatchTarget& t) { return t.name == entry.path; })) continue;
        named = true;
        const EntryMatch match = MatchEntry(patch, entry.path, data, crc);
        if (match.state != EntryState::kOriginal || !match.target || match.ambiguous) continue;
        std::vector<uint8_t> out = data;
        for (const BlockRun& run : match.target->runs) {
            if (size_t(run.offset) + run.data.size() > out.size()) return std::nullopt;
            std::copy(run.data.begin(), run.data.end(), out.begin() + run.offset);
        }
        if (Crc32(out) == match.target->crc_after) return out;
    }
    if (named) {
        char seen[96];
        std::snprintf(seen, sizeof(seen), "%s: %zu bytes, CRC32 %08x is no source of the Portuguese art", entry.path.c_str(), data.size(), crc);
        why = seen;
    }
    return std::nullopt;
}

// The Portuguese art of each visual patch, by archive. A patch's entries are laid on together or not at all: art made
// for a layer of the title menu must not be drawn over another version of it.
std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>> VisualReplacements(
    const std::filesystem::path& app0, const std::vector<std::string>& archives, const std::vector<BlockPatch>& visuals,
    TextTranslation& result, std::vector<std::pair<std::string, std::string>>& missed) {
    std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>> out;
    for (const BlockPatch& patch : visuals) {
        std::vector<std::tuple<std::string, std::string, std::vector<uint8_t>>> found;
        std::vector<std::tuple<std::string, std::string, std::string>> refused;
        for (const std::string& name : archives) {
            CpkArchive archive;
            if (!archive.Open(app0 / name)) continue;
            for (const CpkEntry& entry : archive.entries()) {
                if (std::none_of(patch.targets.begin(), patch.targets.end(), [&](const BlockPatchTarget& t) { return t.name == entry.path; })) continue;
                std::string why;
                const std::vector<uint8_t> data = archive.Read(entry);
                if (std::optional<std::vector<uint8_t>> art = VisualFor({patch}, entry, data, why)) {
                    found.emplace_back(name, entry.path, std::move(*art));
                } else {
                    refused.emplace_back(name, entry.path, why);
                }
            }
        }
        if (refused.empty()) {
            for (auto& [archive, path, data] : found) out[archive].emplace_back(path, std::move(data));
            result.visuals += found.size();
        } else {
            for (const auto& [archive, path, why] : refused) {
                result.visual_skipped.push_back(archive + ": " + why);
                missed.emplace_back(archive, path);
            }
            for (const auto& [archive, path, data] : found) missed.emplace_back(archive, path);
            if (!found.empty()) {
                result.visual_skipped.push_back(std::to_string(found.size()) + " matching entries of the same art left as they are");
            }
        }
    }
    return out;
}

// The archive with the same role in another slot (Odin2_OR_ES.CPK <-> Odin2_OR_UK.CPK), or empty.
std::string PairedArchive(std::string_view name, std::string_view from, std::string_view to) {
    const std::string suffix = "_" + std::string(from) + ".CPK";
    if (name.size() < suffix.size() || name.substr(name.size() - suffix.size()) != suffix) return {};
    return std::string(name.substr(0, name.size() - suffix.size())) + "_" + std::string(to) + ".CPK";
}

// The Portuguese (Brazil) visual patches of a Europe run are built from the English slot, while the game now reads the
// Spanish localized layer: a replacement stays on the Spanish archive when that archive holds the entry, and on the
// English archive (the base the game also mounts) when the entry exists only there.
std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>> RetargetVisuals(
    const std::filesystem::path& app0,
    std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>> visuals) {
    std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>> out;
    std::map<std::string, std::unique_ptr<CpkArchive>> archives;
    for (auto& [name, entries] : visuals) {
        const std::string target = PairedArchive(name, "UK", "ES");
        CpkArchive* layer = nullptr;
        if (!target.empty()) {
            auto it = archives.find(target);
            if (it == archives.end()) {
                auto opened = std::make_unique<CpkArchive>();
                if (!opened->Open(app0 / target)) opened.reset();
                it = archives.emplace(target, std::move(opened)).first;
            }
            layer = it->second.get();
        }
        for (auto& entry : entries) out[layer && layer->Find(entry.first) ? target : name].push_back(std::move(entry));
    }
    return out;
}

// Entries of refused art, named in the archive the game reads them from (as RetargetVisuals places served art).
std::vector<std::pair<std::string, std::string>> RetargetMissed(const std::filesystem::path& app0,
                                                                std::vector<std::pair<std::string, std::string>> missed) {
    std::map<std::string, std::unique_ptr<CpkArchive>> archives;
    for (auto& [name, path] : missed) {
        const std::string target = PairedArchive(name, "UK", "ES");
        if (target.empty()) continue;
        auto it = archives.find(target);
        if (it == archives.end()) {
            auto opened = std::make_unique<CpkArchive>();
            if (!opened->Open(app0 / target)) opened.reset();
            it = archives.emplace(target, std::move(opened)).first;
        }
        if (it->second && it->second->Find(path)) name = target;
    }
    return missed;
}

bool IsUntranslatedVisual(const TextCorpus& corpus, const std::string& path) {
    const std::string wanted = Upper(path);
    return std::any_of(corpus.untranslated_visuals.begin(), corpus.untranslated_visuals.end(),
                       [&](const std::string& e) { return Upper(e) == wanted; });
}

void AddServedWatches(TextTranslation& result, const std::string& archive, const ArchiveOverlay& overlay,
                      const std::map<std::string, VisualWatchKind>& kinds) {
    for (const ArchiveOverlay::Placed& placed : overlay.placed) {
        const auto kind = kinds.find(placed.path);
        if (kind != kinds.end()) result.visual_watches.push_back({archive, placed.path, placed.offset, placed.size, kind->second});
    }
}

// A Spanish-slot run mounts the English base archives too: their copies of the entries the visual deltas rebuild get the
// same Portuguese result, so no path of the game draws the English art.
template <class Paired>
void AddEnglishBaseDeltas(const std::vector<std::string>& slot_names, const std::vector<VisualDeltaTarget>& deltas,
                          const Paired& paired,
                          std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>>& art,
                          TextTranslation& result) {
    for (const std::string& name : slot_names) {
        const std::string base = PairedArchive(name, "ES", "UK");
        CpkArchive* english = base.empty() ? nullptr : paired(name);
        if (!english) continue;
        auto& served = art[base];
        for (const CpkEntry& entry : english->entries()) {
            if (std::any_of(served.begin(), served.end(), [&](const auto& s) { return s.first == entry.path; })) continue;
            std::string error;
            std::vector<uint8_t> data;
            for (const VisualDeltaTarget& t : deltas) {
                if (t.name != entry.path || t.file == kPrologueVisuals) continue;
                if (data.empty()) data = english->Read(entry, &error);
                std::optional<std::vector<uint8_t>> rebuilt = ApplyVisualDelta(t, data);
                if (!rebuilt) continue;
                if (*rebuilt != data) {
                    served.emplace_back(entry.path, std::move(*rebuilt));
                    ++result.visuals;
                }
                break;
            }
        }
    }
}

} // namespace

TextTranslation TranslateGameText(const std::filesystem::path& app0, const GameBuild& build, const TextLanguage& language,
                                  const TextCorpus& corpus) {
    TextTranslation result;
    osl_prologue_set_active(0);
    if (!IsPortLanguage(language)) return result;
    const bool es_base = language.source == TextSource::kPortTranslation && language.slot == "ES";
    if (es_base) osl_prologue_set_active(1);
    const bool telemetry = language.key == kPortugueseBrazilKey;
    std::vector<std::pair<std::string, std::string>> missed;
    const std::vector<std::string> slot_names = SlotArchives(build, language.slot);
    auto art = language.source == TextSource::kPortTranslation
                   ? VisualReplacements(app0, SlotArchives(build, es_base ? "UK" : language.slot),
                                        PortugueseVisualPatches(), result, missed)
                   : std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>>{};
    if (es_base) {
        art = RetargetVisuals(app0, std::move(art));
        missed = RetargetMissed(app0, std::move(missed));
    }
    const bool native_english = language.slot == "US" || language.slot == "UK";
    std::vector<VisualDeltaTarget> deltas =
        language.source == TextSource::kPortTranslation ? PortugueseVisualDeltas() : std::vector<VisualDeltaTarget>{};
    if (!es_base) std::erase_if(deltas, [](const VisualDeltaTarget& t) { return t.file == kPrologueVisuals; });
    result.glyphs = PrepareGlyphs(app0, build, language, Sha256Hex(app0 / "eboot.bin"), corpus);
    ChunkPool pool;
    const auto map_file = corpus.maps.find(MapFileOf(language.slot));
    const auto sources_file = corpus.sources.find(MapFileOf(language.slot));

    std::map<std::string, std::unique_ptr<CpkArchive>> paired_archives;
    const auto paired = [&](std::string_view name) -> CpkArchive* {
        const std::string target = PairedArchive(name, "ES", "UK");
        if (target.empty()) return nullptr;
        auto it = paired_archives.find(target);
        if (it == paired_archives.end()) {
            auto opened = std::make_unique<CpkArchive>();
            if (!opened->Open(app0 / target)) opened.reset();
            it = paired_archives.emplace(target, std::move(opened)).first;
        }
        return it->second.get();
    };
    std::map<std::string, std::vector<std::string>> english_units;

    for (const std::string& name : slot_names) {
        const std::filesystem::path file = app0 / name;
        std::error_code ec;
        const uint64_t length = std::filesystem::file_size(file, ec);
        if (ec) continue;
        CpkArchive archive;
        std::string error;
        if (!archive.Open(file, &error)) {
            result.problems.push_back(name + ": " + error);
            continue;
        }
        std::vector<std::pair<std::string, ChunkedBytes>> replaced;
        std::map<std::string, VisualWatchKind> visual_kind;
        for (auto& [atlas_archive, entry] : result.glyphs.atlases) {
            if (atlas_archive != name) continue;
            replaced.emplace_back(entry.first, pool.Add(std::vector<uint8_t>(entry.second)));
            visual_kind[entry.first] = VisualWatchKind::kServed;
        }
        for (auto& entry : art[name]) {
            visual_kind[entry.first] = VisualWatchKind::kServed;
            replaced.emplace_back(entry.first, pool.Add(std::move(entry.second)));
        }
        art.erase(name);
        CpkArchive* english = es_base ? paired(name) : nullptr;
        if (language.source == TextSource::kPortTranslation) {
            // A prologue layout must never be served without its atlas (or the reverse): both or neither.
            std::vector<std::pair<std::string, std::vector<uint8_t>>> prologue;
            std::vector<std::string> prologue_named;
            std::string prologue_refused;
            for (const CpkEntry& entry : archive.entries()) {
                std::vector<const VisualDeltaTarget*> named;
                for (const VisualDeltaTarget& t : deltas) {
                    if (t.name == entry.path) named.push_back(&t);
                }
                if (named.empty()) continue;
                ++result.visual_targets;
                const std::vector<uint8_t> data = archive.Read(entry, &error);
                std::optional<std::vector<uint8_t>> rebuilt;
                for (const VisualDeltaTarget* t : named) {
                    if ((rebuilt = ApplyVisualDelta(*t, data))) break;
                }
                if (named.front()->file == kPrologueVisuals) {
                    prologue_named.push_back(entry.path);
                    if (rebuilt) {
                        prologue.emplace_back(entry.path, std::move(*rebuilt));
                    } else if (prologue_refused.empty()) {
                        char seen[160];
                        std::snprintf(seen, sizeof(seen), "%s: %s: %zu bytes, CRC32 %08x", name.c_str(), entry.path.c_str(),
                                      data.size(), Crc32(data));
                        prologue_refused = seen;
                    }
                    continue;
                }
                if (!rebuilt && english) {
                    if (const CpkEntry* was = english->Find(entry.path)) {
                        const std::vector<uint8_t> source = english->Read(*was, &error);
                        for (const VisualDeltaTarget* t : named) {
                            if ((rebuilt = ApplyVisualDelta(*t, source))) break;
                        }
                    }
                }
                if (rebuilt) {
                    if (*rebuilt != data) {
                        replaced.emplace_back(entry.path, pool.Add(std::move(*rebuilt)));
                        visual_kind[entry.path] = VisualWatchKind::kServed;
                        ++result.visuals;
                    }
                    continue;
                }
                missed.emplace_back(name, entry.path);
                char seen[160];
                std::snprintf(seen, sizeof(seen), "%s: %s: %zu bytes, CRC32 %08x is no source of the Portuguese layout",
                              name.c_str(), entry.path.c_str(), data.size(), Crc32(data));
                result.visual_skipped.push_back(seen);
            }
            if (!prologue_refused.empty()) {
                result.visual_skipped.push_back("chapter prologues left in Spanish: " + prologue_refused +
                                                " is no source of the Portuguese pages");
            } else if (prologue.size() == 2) {
                for (auto& [path, bytes] : prologue) {
                    visual_kind[path] = VisualWatchKind::kPrologue;
                    replaced.emplace_back(path, pool.Add(std::move(bytes)));
                }
                result.visuals += 2;
                ++result.prologue_pages;
            } else if (!prologue.empty()) {
                result.visual_skipped.push_back(name + ": chapter prologues left in Spanish: the page and its atlas are not both there");
            }
            if (!prologue_named.empty() && !visual_kind.count(prologue_named.front())) {
                for (const std::string& path : prologue_named) missed.emplace_back(name, path);
            }
        }
        if (telemetry) {
            for (const CpkEntry& entry : archive.entries()) {
                if (visual_kind.count(entry.path) || !IsUntranslatedVisual(corpus, entry.path)) continue;
                result.visual_watches.push_back({name, entry.path, entry.offset, entry.size, VisualWatchKind::kUntranslated});
            }
        }
        for (const CpkEntry& entry : archive.entries()) {
            const std::optional<std::string> resource = TextResourceOf(entry.path);
            if (!resource || !corpus.english.count(*resource)) continue;
            const std::vector<uint8_t> data = archive.Read(entry, &error);
            std::optional<TextContainer> container = data.empty() ? std::nullopt : ParseTextContainer(*resource, data);
            if (!container) {
                result.problems.push_back(name + ": " + entry.path + " not parsed");
                continue;
            }
            const std::vector<std::string>* source = nullptr;
            bool index_aligned = false;
            if (es_base) {
                const CpkEntry* was = english ? english->Find(entry.path) : nullptr;
                if (!was) {
                    result.problems.push_back(name + ": " + entry.path + " has no English counterpart");
                    continue;
                }
                const std::vector<uint8_t> source_data = english->Read(*was, &error);
                const std::optional<TextContainer> source_container = ParseTextContainer(*resource, source_data);
                if (!source_container) {
                    result.problems.push_back(name + ": " + entry.path + " English counterpart not parsed");
                    continue;
                }
                english_units[*resource] = source_container->units;
                source = &english_units[*resource];
                index_aligned = true;
            } else if (!native_english) {
                source = &corpus.english.find(*resource)->second;
            }
            const std::unordered_map<size_t, std::vector<std::string>>* map = nullptr;
            const std::unordered_map<size_t, size_t>* from = nullptr;
            if (!native_english && !es_base && map_file != corpus.maps.end()) {
                const auto it = map_file->second.find(*resource);
                static const std::unordered_map<size_t, std::vector<std::string>> kEmpty;
                map = it != map_file->second.end() ? &it->second : &kEmpty;
                if (sources_file != corpus.sources.end()) {
                    const auto s = sources_file->second.find(*resource);
                    if (s != sources_file->second.end()) from = &s->second;
                }
            }
            size_t served = 0, candidates = 0;
            std::vector<std::string> units =
                ServedUnits(*resource, *container, language, corpus, source, map, from, index_aligned, result.glyphs,
                            served, candidates, result.reasons);
            result.corpus += candidates;
            result.served += served;
            result.kept += candidates - served;
            if (container->kind == TextKind::kArchivePage) result.pages += served;
            if (!served) continue;
            container->units = std::move(units);
            replaced.emplace_back(entry.path, pool.Add(BuildTextContainer(*container)));
        }
        if (replaced.empty()) continue;
        std::optional<ArchiveOverlay> overlay = OverlayEntries(archive, length, replaced, &error);
        if (!overlay) {
            result.problems.push_back(name + ": " + error);
            continue;
        }
        AddServedWatches(result, name, *overlay, visual_kind);
        result.archives.push_back({name, std::move(*overlay), replaced.size()});
    }

    if (es_base) AddEnglishBaseDeltas(slot_names, deltas, paired, art, result);

    // Glyph atlases and art whose entry lives only in the English base archives the game mounts with the localized layer.
    std::map<std::string, std::vector<std::pair<std::string, std::vector<uint8_t>>>> extra = std::move(art);
    for (auto& [atlas_archive, entry] : result.glyphs.atlases) {
        if (std::find(slot_names.begin(), slot_names.end(), atlas_archive) == slot_names.end())
            extra[atlas_archive].emplace_back(entry.first, entry.second);
    }
    for (auto& [name, entries] : extra) {
        if (entries.empty()) continue;
        const std::filesystem::path file = app0 / name;
        std::error_code ec;
        const uint64_t length = std::filesystem::file_size(file, ec);
        if (ec) continue;
        CpkArchive archive;
        std::string error;
        if (!archive.Open(file, &error)) {
            result.problems.push_back(name + ": " + error);
            continue;
        }
        std::vector<std::pair<std::string, ChunkedBytes>> replaced;
        std::map<std::string, VisualWatchKind> visual_kind;
        for (auto& entry : entries) {
            visual_kind[entry.first] = VisualWatchKind::kServed;
            replaced.emplace_back(entry.first, pool.Add(std::move(entry.second)));
        }
        std::optional<ArchiveOverlay> overlay = OverlayEntries(archive, length, replaced, &error);
        if (!overlay) {
            result.problems.push_back(name + ": " + error);
            continue;
        }
        AddServedWatches(result, name, *overlay, visual_kind);
        result.archives.push_back({name, std::move(*overlay), replaced.size()});
    }
    if (!telemetry) {
        result.visual_watches.clear();
        return result;
    }
    std::map<std::string, std::unique_ptr<CpkArchive>> opened;
    for (const auto& [name, path] : missed) {
        auto it = opened.find(name);
        if (it == opened.end()) {
            auto archive = std::make_unique<CpkArchive>();
            if (!archive->Open(app0 / name)) archive.reset();
            it = opened.emplace(name, std::move(archive)).first;
        }
        const CpkEntry* entry = it->second ? it->second->Find(path) : nullptr;
        if (entry) result.visual_watches.push_back({name, entry->path, entry->offset, entry->size, VisualWatchKind::kMissed});
    }
    return result;
}

} // namespace osl

namespace {

osl::TextTranslation g_translation;
std::vector<OslGlyphPatch> g_glyph_patches;
std::vector<OslSjisPatch> g_sjis_patches;
const char* g_prompt_word = nullptr;

// "ES" for Odin2_RE_ES.CPK; empty when the archive's name carries no language (the Japanese and Asian archives).
std::string SourceSlotOf(std::string_view archive) {
    if (archive.size() < 7 || archive[archive.size() - 7] != '_' || archive.substr(archive.size() - 4) != ".CPK") return {};
    return std::string(archive.substr(archive.size() - 6, 2));
}

} // namespace

extern "C" const OslArchiveOverlay* osl_text_translation(const char* settings_file, const char* image_dir, const char* app0,
                                                         size_t* count) {
    using namespace osl;
    *count = 0;
    const GameBuild* build = BuildOfImageDir(image_dir);
    if (!build || !app0) return nullptr;
    const LauncherSettings settings(PathFromUtf8(settings_file ? settings_file : ""));
    const TextLanguage& language = ResolveTextLanguage(build, settings.Get(kTextLanguageKey));
    if (!IsPortLanguage(language)) return nullptr;
    const auto start = std::chrono::steady_clock::now();
    const TextCorpus& corpus = EmbeddedTextCorpus();
    g_translation = TranslateGameText(PathFromUtf8(app0), *build, language, corpus);
    const TextTranslation& t = g_translation;
    static std::deque<std::string> names;
    static std::vector<std::vector<OslFilePatch>> patches;
    static std::vector<OslArchiveOverlay> overlays;
    for (const ServedArchive& served : t.archives) {
        names.push_back(served.archive);
        std::vector<OslFilePatch>& list = patches.emplace_back();
        for (const auto& [offset, bytes] : served.overlay.patches) list.push_back({offset, bytes->size(), bytes->data()});
    }
    for (size_t i = 0; i < t.archives.size(); ++i) {
        overlays.push_back({names[i].c_str(), t.archives[i].overlay.length, patches[i].data(), patches[i].size()});
    }
    for (const GlyphPatch& p : t.glyphs.patches) {
        OslGlyphPatch g{p.descriptor, {}, p.code, p.template_code, p.cell};
        std::copy(p.header.begin(), p.header.end(), g.header);
        g_glyph_patches.push_back(g);
    }
    for (const SjisPatch& p : t.glyphs.sjis_patches) g_sjis_patches.push_back({p.table, p.lead, p.trail, p.code});
    g_prompt_word = language.key == kPortugueseBrazilKey ? "Pressione" : nullptr;
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    const std::string slot = language.slot.empty() ? std::string("Japanese") : std::string(language.slot);
    std::printf("Menu & Subtitles: %.*s served over the %s slot: %zu of %zu records, %zu kept in the slot's text, "
                "%zu ArchiveViewer blocks, %zu visual entries, %zu archive(s), %lld ms\n",
                static_cast<int>(language.name.size()), language.name.data(), slot.c_str(), t.served, t.corpus, t.kept,
                t.pages, t.visuals, t.archives.size(), ms);
    if (language.source == TextSource::kPortTranslation) {
        std::printf("Menu & Subtitles: %.*s visuals: %zu applied, %zu skipped (%zu layout entries named)\n",
                    static_cast<int>(language.name.size()), language.name.data(), t.visuals, t.visual_skipped.size(),
                    t.visual_targets);
    }
    if (t.prologue_pages) {
        std::printf("Menu & Subtitles: %.*s chapter prologues: %zu books, %zu lines\n", static_cast<int>(language.name.size()),
                    language.name.data(), osl_prologue_book_count(), osl_prologue_record_count());
    }
    for (const std::string& v : t.visual_skipped) std::printf("Menu & Subtitles:   visual skipped: %s\n", v.c_str());
    std::string atlases;
    for (const auto& [archive, entry] : t.glyphs.atlases) atlases += (atlases.empty() ? "" : ", ") + entry.first;
    std::printf("Menu & Subtitles: glyphs: %s, %zu letters drawn%s%s, %zu glyph records and %zu Shift-JIS codes to add\n",
                t.glyphs.known ? "known executable" : "unknown executable", t.glyphs.drawn, atlases.empty() ? "" : " into ",
                atlases.c_str(), g_glyph_patches.size(), g_sjis_patches.size());
    for (const std::string& note : t.glyphs.notes) std::printf("Menu & Subtitles:   glyphs: %s\n", note.c_str());
    for (const auto& [why, n] : t.reasons) std::printf("Menu & Subtitles:   %zu kept: %s\n", n, why.c_str());
    for (const std::string& p : t.problems) std::printf("Menu & Subtitles:   %s\n", p.c_str());
    for (const std::string& e : corpus.errors) std::printf("Menu & Subtitles:   corpus %s\n", e.c_str());
    *count = overlays.size();
    return overlays.data();
}

extern "C" const OslVisualWatch* osl_text_visual_watches(size_t* count) {
    static std::deque<std::string> strings;
    static std::vector<OslVisualWatch> watches;
    if (watches.empty()) {
        for (const osl::VisualWatch& w : g_translation.visual_watches) {
            const std::string& archive = strings.emplace_back(w.archive);
            const std::string& entry = strings.emplace_back(w.entry);
            const std::string& source = strings.emplace_back(SourceSlotOf(w.archive));
            watches.push_back({archive.c_str(), entry.c_str(), source.c_str(), w.offset, w.size, static_cast<int>(w.kind)});
        }
    }
    *count = watches.size();
    return watches.data();
}

extern "C" const OslGlyphPatch* osl_text_glyph_patches(size_t* count, const OslSjisPatch** sjis, size_t* sjis_count) {
    *count = g_glyph_patches.size();
    *sjis = g_sjis_patches.data();
    *sjis_count = g_sjis_patches.size();
    return g_glyph_patches.data();
}

extern "C" const char* osl_text_prompt_word(void) {
    return g_prompt_word;
}
