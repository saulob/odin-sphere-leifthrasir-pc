// Menu & Subtitles languages of the port: the language lists of every build, the saved choice across builds, the text
// containers, the control syntax and Portuguese letters, the embedded corpus and, on the prepared games present,
// the translated archives read back through their overlay. Nothing here writes a game file.
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

#include <zlib.h>

#include "cpk_patch.h"
#include "game_profile.h"
#include "game_text.h"
#include "game_translation.h"
#include "prologue_translation.h"
#include "settings.h"
#include "text_glyphs.h"
#include "test_support.h"

// The runtime file layer, linked here to read a translated archive exactly as the loader serves it to the guest.
extern "C" {
typedef struct {
    const char *name;
    void *function;
} RuntimeExport;
typedef struct {
    uint64_t offset, size;
    const unsigned char *data;
} RuntimeFilePatch;
void runtime_file_configure(const char *app0, const char *user);
int runtime_file_overlay(const char *guest, uint64_t length, const RuntimeFilePatch *patches, size_t count);
int64_t runtime_file_open(const char *path, int flags, int mode);
int64_t runtime_file_pread(int fd, void *buffer, uint64_t size, int64_t offset);
int64_t runtime_file_close(int fd);
enum { RUNTIME_WATCH_SERVED, RUNTIME_WATCH_PROLOGUE, RUNTIME_WATCH_MISSED, RUNTIME_WATCH_UNTRANSLATED };
typedef struct {
    uint64_t offset, size;
    const char *entry;
    int kind;
} RuntimeFileWatch;
typedef struct {
    size_t visual_hits, visual_unique_hits, visual_missing, localized_untranslated_hits;
} RuntimeFileWatchCounts;
int runtime_file_watch(const char *guest, const char *archive, const char *source, const RuntimeFileWatch *items, size_t count);
void runtime_file_watch_counts(RuntimeFileWatchCounts *out);
int32_t *runtime_errno(void);
int32_t runtime_guest_errno(int host_errno);
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid);
int runtime_savedata_protects(const char *host_path);
}
int32_t *runtime_errno(void) {
    static int32_t slot;
    return &slot;
}
int32_t runtime_guest_errno(int host_errno) { return host_errno; }
uintptr_t runtime_lookup(const RuntimeExport *, size_t, const char *) { return 0; }
int runtime_savedata_protects(const char *) { return 0; }

using namespace osl;

namespace {

int checks = 0, failures = 0;
#define CHECK(condition)                                                                                         \
    do {                                                                                                         \
        ++checks;                                                                                                \
        if (!(condition)) {                                                                                      \
            ++failures;                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                  \
        }                                                                                                        \
    } while (0)

std::vector<uint8_t> Bytes(std::string_view s) { return {s.begin(), s.end()}; }

const TextLanguage* Find(std::span<const TextLanguage> languages, std::string_view key) {
    for (const TextLanguage& l : languages) {
        if (l.key == key) return &l;
    }
    return nullptr;
}

void TestLanguageLists() {
    for (const GameBuild& build : SupportedBuilds()) {
        const std::span<const TextLanguage> languages = TextLanguagesOf(&build);
        const TextLanguage* english = Find(languages, kEnglishKey);
        const TextLanguage* portuguese = Find(languages, kPortugueseBrazilKey);
        CHECK(english != nullptr);
        CHECK(portuguese != nullptr);
        CHECK(build.text_mode == TextLanguageMode::kSelectable);
        std::set<std::string_view> keys;
        for (const TextLanguage& l : languages) CHECK(keys.insert(l.key).second);
        if (!english || !portuguese) continue;
        CHECK(portuguese->name == "Portuguese (Brazil)");
        CHECK(portuguese->source == TextSource::kPortTranslation);
        if (std::string_view(build.region) == "Europe") {
            CHECK(portuguese->slot == "ES");
            CHECK(portuguese->ps4_id == 3);
        } else {
            CHECK(portuguese->slot == english->slot);
            CHECK(portuguese->ps4_id == english->ps4_id);
        }
        const std::string_view region = build.region;
        if (region == "USA" || region == "Europe") {
            CHECK(english->source == TextSource::kNative);
            CHECK(english->slot == (region == "USA" ? "US" : "UK"));
        } else {
            CHECK(english->source == TextSource::kPortEnglish);
            CHECK(english->slot == (region == "Japan" ? "" : "CN"));
            CHECK(!IsPortLanguage(languages.front()));
        }
        CHECK(IsValidSetting(kTextLanguageKey, kEnglishKey));
        CHECK(IsValidSetting(kTextLanguageKey, kPortugueseBrazilKey));
        // Saved on any build, the choice is the same language on every other build.
        for (const GameBuild& other : SupportedBuilds()) {
            CHECK(ResolveTextLanguage(&other, kPortugueseBrazilKey).key == kPortugueseBrazilKey);
            CHECK(ResolveTextLanguage(&other, kEnglishKey).key == kEnglishKey);
        }
    }
    const GameBuild& usa = SupportedBuilds()[0];
    CHECK(ResolveTextLanguage(&usa, "french").key == kEnglishKey);
    for (const GameBuild& build : SupportedBuilds()) {
        if (std::string_view(build.region) == "Europe") {
            CHECK(ResolveTextLanguage(&build, "french").ps4_id == 2);
            CHECK(TextLanguagesOf(&build).size() == 6);
        }
        if (std::string_view(build.region) == "Japan") {
            CHECK(ResolveTextLanguage(&build, "japanese").key == "japanese");
            CHECK(ResolveTextLanguage(&build, "korean").key == "japanese");
            CHECK(TextLanguagesOf(&build).size() == 3);
        }
        if (std::string_view(build.region) == "HK" || std::string_view(build.region) == "Asia") {
            CHECK(ResolveTextLanguage(&build, "korean").ps4_id == 9);
            CHECK(TextLanguagesOf(&build).size() == 4);
        }
    }
    CHECK(!IsValidSetting(kTextLanguageKey, "klingon"));
}

void TestContainers() {
    // FMSB: header, zeroed pointer table, records padded to 16, FEOC.
    std::vector<uint8_t> fms(0x20, 0);
    std::memcpy(fms.data(), "FMSB", 4);
    fms[8] = 0x20;
    fms[0x14] = 2;
    fms.resize(fms.size() + 16, 0);
    for (const char* r : {"Opções", "%d HP"}) {
        fms.insert(fms.end(), r, r + std::strlen(r));
        fms.push_back(0);
    }
    fms.resize(fms.size() + (16 - fms.size() % 16) % 16, 0);
    const uint32_t trailer = uint32_t(fms.size() - 0x20);
    std::memcpy(fms.data() + 4, &trailer, 4);
    for (char c : std::string("FEOC\0\0\0\0", 8)) fms.push_back(uint8_t(c));
    const std::optional<TextContainer> sheet = ParseTextContainer("msgsheet/Test.fms", fms);
    CHECK(sheet && sheet->units.size() == 2 && sheet->units[0] == "Opções");
    CHECK(sheet && BuildTextContainer(*sheet) == fms);

    std::vector<uint8_t> story(0x40, 0);
    story[0x10] = 2;
    for (const char* r : {"\x82\xa0\x82\xa2", "@w(6)abc"}) {
        story.insert(story.end(), r, r + std::strlen(r));
        story.push_back(0);
    }
    const std::optional<TextContainer> sjis = ParseTextContainer("story/a1_1", story);
    CHECK(sjis && sjis->shift_jis && sjis->units.size() == 2);
    CHECK(sjis && BuildTextContainer(*sjis) == story);
    CHECK(!ParseTextContainer("story/a1_1", Bytes(std::string(0x40, '\0') + "x")));

    const std::string page = "\xEF\xBB\xBF@TITLE( \"Notas\" ) // t\r\n@TEXT(\r\nLinha @b um@/b\r\n)\r\n@FRAME(1)\r\n@TEXT(\r\ndois\r\n  )\r\n";
    const std::optional<TextContainer> p = ParseTextContainer("ArchiveViewer/Note00.txt", Bytes(page));
    CHECK(p && p->units.size() == 3 && p->units[0] == "Notas" && p->units[1] == "\r\nLinha @b um@/b");
    CHECK(p && BuildTextContainer(*p) == Bytes(page));

    CHECK(TextResourceOf("msgsheet/SystemMsg.fms") == "msgsheet/SystemMsg.fms");
    CHECK(TextResourceOf("EventTxt/A1_1.txt") == "story/a1_1");
    CHECK(TextResourceOf("sysmsg.txt") == "story/sysmsg");
    CHECK(TextResourceOf("ArchiveViewer/Dummy.Txt") == "ArchiveViewer/Dummy.txt");
    CHECK(TextResourceOf("StaffRoll/RE_StaffRoll_True.txt") == "StaffRoll/RE_StaffRoll_True.txt");
    CHECK(!TextResourceOf("GUI/HD_Font.ftx"));
}

void TestControlSyntax() {
    const std::string en = "Press @#(106) to @c(6)attack@/c, %d times. ##(JUMP)";
    CHECK(ControlTokens(en) == std::vector<std::string>({"@#(106)", "@c(6)", "@/c", "##(JUMP)"}));
    CHECK(Conversions(en) == std::vector<std::string>({"%d"}));
    CHECK(Conversions("20% or lower").empty());
    CHECK(Conversions("Rate: %g%%, %02d").size() == 3);
    CHECK(CheckTranslation(en, "Pressione @#(106) para @c(6)atacar@/c, %d vezes. ##(JUMP)").empty());
    CHECK(!CheckTranslation(en, "Pressione para @c(6)atacar@/c, %d vezes. ##(JUMP)").empty());
    CHECK(!CheckTranslation(en, "Pressione @#(106) para @c(6)atacar@/c, %s vezes. ##(JUMP)").empty());
    CHECK(!CheckTranslation(en, "Pressione @#(106) para @c(6)atacar, %d vezes. ##(JUMP)").empty());
    CHECK(!CheckTranslation("Max Chain (%d)", "Combo máx. (%d) 10%").empty());
    CHECK(CheckTranslation("Recover 20% HP", "Recupera 20% de HP").empty());
    CHECK(!CheckTranslation("Recover 20% HP", "Recupera %d de HP").empty());
    CHECK(!CheckTranslation("Text", "").empty());
    CHECK(!CheckTranslation("Text", "Ã©").empty());
    CHECK(!CheckTranslation("Text", "Te\xEF\xBF\xBDxto").empty());
    CHECK(!CheckTranslation("Text", "Te\x01xto").empty());
    CHECK(!CheckTranslation("Text", std::string("\xC3", 1)).empty());
    CHECK(Arguments("@#(2) %d") == Arguments("%d e @#(2)"));
    CHECK(Digits("Recover 100 HP") == Digits("ＨＰを１００回復"));
    CHECK(Translatable("@c(6)Phozon@/c") && !Translatable("---") && !Translatable("@#(106)") && !Translatable(""));
}

void TestPortugueseLetters() {
    const std::string letters = "á à â ã é ê í ó ô õ ú ü ç Á À Â Ã É Ê Í Ó Ô Õ Ú Ü Ç";
    for (const char* word : {"não", "informações", "opções", "coração", "ã", "õ", "Ã", "Õ", "ç", "Ç", letters.c_str()}) {
        CHECK(CheckTranslation("Text", word).empty());
    }
    CHECK(std::string("ã") == "\xC3\xA3" && std::string("õ") == "\xC3\xB5" && std::string("Ã") == "\xC3\x83");
    CHECK(std::string("Õ") == "\xC3\x95" && std::string("ç") == "\xC3\xA7" && std::string("Ç") == "\xC3\x87");
    TextGlyphs none;
    CHECK(MissingLetter("não", none) == "ã U+00E3");
    CHECK(MissingLetter("Pressione @#(2) — 3×", none).empty());
    TextGlyphs pt;
    pt.letters = {0xE3, 0xE7, 0xE9};
    CHECK(MissingLetter("não ação café", pt).empty());
    CHECK(MissingLetter("opções", pt) == "õ U+00F5");
    pt.sjis = {{0xE3, {0x81, 0xAD}}, {0xE7, {0x81, 0xAE}}};
    CHECK(ShiftJisText("ação “sim” — ok", pt) == std::string("a\x81\xae\x81\xado \x81\x67sim\x81\x68 \x81\x5c ok"));
    CHECK(!ShiftJisText("opções", pt));
    CHECK(!ShiftJisText("火", pt));
    // The Portuguese text is never shown without its accents.
    for (const char* file : {"launcher/game_translation.cpp", "launcher/game_text.cpp", "launcher/game_text.h"}) {
        std::ifstream in(std::filesystem::path(OSL_SOURCE_DIR) / file);
        const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(!source.empty() && source.find("Fold") == std::string::npos && source.find("AsciiText") == std::string::npos);
    }
}

// Every accented letter of the Portuguese corpus, from the corpus itself.
std::set<uint32_t> CorpusLetters(const TextCorpus& corpus) {
    std::set<uint32_t> out;
    for (const auto& [hash, text] : corpus.portuguese) {
        for (size_t i = 0; i + 1 < text.size(); ++i) {
            const uint8_t c = uint8_t(text[i]);
            if (c == 0xC3 || c == 0xC2) out.insert(uint32_t(c & 0x1F) << 6 | (uint8_t(text[i + 1]) & 0x3F));
        }
    }
    for (auto it = out.begin(); it != out.end();) {
        it = (*it < 0xC0 && *it != 0xAA && *it != 0xBA) || *it == 0xD7 || *it == 0xF7 ? out.erase(it) : std::next(it);
    }
    return out;
}

void TestGlyphData() {
    const TextCorpus& corpus = EmbeddedTextCorpus();
    const std::set<uint32_t> letters = CorpusLetters(corpus);
    CHECK(letters.count(0xE3) && letters.count(0xF5) && letters.count(0xC3) && letters.count(0xD5) && letters.count(0xE7));
    std::map<std::string, const GlyphBuild*> builds;
    for (const GlyphBuild& b : corpus.glyph_builds) builds[b.eboot_sha256] = &b;
    for (const GameBuild& build : SupportedBuilds()) {
        const auto it = builds.find(build.eboot_sha256);
        if (it == builds.end()) {
            std::printf("  %-24s glyphs: STATIC SUPPORT PASS, REAL DATA UNAVAILABLE (accented records keep the slot's text)\n", build.label);
            continue;
        }
        const GlyphBuild& g = *it->second;
        size_t drawn = 0, patches = 0;
        std::string lacking;
        for (const GlyphFamily& family : g.families) {
            CHECK(!family.atlases.empty() && !family.descriptors.empty());
            std::set<uint32_t> have = family.native;
            for (const GlyphRecipe& r : family.glyphs) {
                have.insert(r.code);
                CHECK(r.donor_cell >= 0 || r.punctuation_cell >= 0);
            }
            for (const uint32_t l : letters) {
                if (!have.count(l) && l != 0xAA && l != 0xBA) lacking += " U+" + std::to_string(l);
            }
            drawn += family.glyphs.size();
            patches += family.glyphs.size() * family.descriptors.size();
        }
        CHECK(lacking.empty());
        const bool japanese = std::string_view(build.region) == "Japan";
        CHECK(japanese == (g.sjis_table != 0));
        for (const uint32_t l : letters) CHECK(!japanese || l == 0xAA || l == 0xBA || g.sjis_letters.count(l));
        std::printf("  %-24s glyphs: %zu letters drawn in %zu families, %zu records%s%s\n", build.label, drawn, g.families.size(),
                    patches, japanese ? ", Shift-JIS surrogates " : "", japanese ? std::to_string(g.sjis_letters.size()).c_str() : "");
    }
}

void Put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i)));
}

void TestVisualDelta() {
    const std::vector<uint8_t> source = Bytes("0123456789abcdefFMBS-layout");
    std::vector<uint8_t> result(source.begin(), source.begin() + 16);
    const std::vector<uint8_t> added = Bytes("NEW-KEYS");
    result.insert(result.end(), added.begin(), added.end());
    result.insert(result.end(), source.begin() + 16, source.end());
    std::vector<uint8_t> blob = Bytes(std::string("OSLV1\0\0\0", 8));
    Put32(blob, 1);
    const std::string name = "GUI/REHD_Interface.mbs";
    blob.push_back(uint8_t(name.size()));
    blob.push_back(0);
    blob.insert(blob.end(), name.begin(), name.end());
    Put32(blob, uint32_t(source.size()));
    Put32(blob, Crc32(source));
    Put32(blob, uint32_t(result.size()));
    Put32(blob, Crc32(result));
    Put32(blob, 3);
    blob.push_back(0);
    Put32(blob, 0);
    Put32(blob, 16);
    blob.push_back(1);
    Put32(blob, uint32_t(added.size()));
    blob.insert(blob.end(), added.begin(), added.end());
    blob.push_back(0);
    Put32(blob, 16);
    Put32(blob, uint32_t(source.size() - 16));
    const std::optional<std::vector<VisualDeltaTarget>> parsed = ParseVisualDelta(blob);
    CHECK(parsed && parsed->size() == 1 && (*parsed)[0].name == name);
    if (parsed && !parsed->empty()) {
        const std::optional<std::vector<uint8_t>> rebuilt = ApplyVisualDelta((*parsed)[0], source);
        CHECK(rebuilt && *rebuilt == result);
        std::vector<uint8_t> other = source;
        other.back() ^= 1;
        CHECK(!ApplyVisualDelta((*parsed)[0], other));
        CHECK(!ApplyVisualDelta((*parsed)[0], result));
    }
    std::vector<uint8_t> truncated(blob.begin(), blob.end() - 3);
    CHECK(!ParseVisualDelta(truncated));
    std::vector<uint8_t> unknown = blob;
    unknown[12 + 2 + name.size() + 20] = 7;
    CHECK(!ParseVisualDelta(unknown));

    const std::vector<VisualDeltaTarget> shipped = PortugueseVisualDeltas();
    CHECK(!shipped.empty());
    std::set<std::string> names;
    size_t cards = 0;
    for (const VisualDeltaTarget& t : shipped) {
        names.insert(t.name);
        CHECK(t.ops && t.added);
        if (t.name.rfind("GUI/HD_StageName", 0) == 0) ++cards;
        if (t.name.size() > 4 && t.name.compare(t.name.size() - 4, 4, ".ftx") == 0) CHECK(t.result_size == t.size);
        CHECK(t.result_size > 0);
    }
    CHECK(names.count("GUI/REHD_Interface.mbs") && names.count("Interface.mbs"));
    CHECK(names.count("GUI/HD_StageName12.ftx") && cards >= 58);
    CHECK(names.count("12.ftx") && names.count("12.mbs") && names.count("GUI/REHD_StageName.mbs"));
    CHECK(names.count("GUI/REHD_Mode_Select.mbs") && names.count("GUI/HD_Kitchin.ftx") && names.count("GUI/HD_Archive_00.ftx"));
}

// An OSLV3 file of one target with two sources sharing its operations: copy 4 bytes, add `added`, copy the rest.
std::vector<uint8_t> Oslv3(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const std::vector<uint8_t>& added,
                           std::vector<uint8_t>& result_a) {
    result_a.assign(a.begin(), a.begin() + 4);
    result_a.insert(result_a.end(), added.begin(), added.end());
    result_a.insert(result_a.end(), a.begin() + 4, a.end());
    std::vector<uint8_t> result_b(b.begin(), b.begin() + 4);
    result_b.insert(result_b.end(), added.begin(), added.end());
    result_b.insert(result_b.end(), b.begin() + 4, b.end());
    std::vector<uint8_t> out = Bytes(std::string("OSLV3\0\0\0", 8));
    Put32(out, 1);
    const std::string name = "x.mbs";
    out.push_back(uint8_t(name.size()));
    out.push_back(0);
    out.insert(out.end(), name.begin(), name.end());
    Put32(out, 2);
    for (const auto& [src, res] : {std::pair{&a, &result_a}, std::pair{&b, &result_b}}) {
        Put32(out, uint32_t(src->size()));
        Put32(out, Crc32(*src));
        Put32(out, uint32_t(res->size()));
        Put32(out, Crc32(*res));
    }
    uLongf packed_size = compressBound(uLong(added.size()));
    std::vector<uint8_t> packed(packed_size);
    compress(packed.data(), &packed_size, added.data(), uLong(added.size()));
    packed.resize(packed_size);
    Put32(out, 3);
    Put32(out, uint32_t(added.size()));
    Put32(out, uint32_t(packed.size()));
    out.push_back(0);
    Put32(out, 0);
    Put32(out, 4);
    out.push_back(1);
    Put32(out, uint32_t(added.size()));
    out.push_back(0);
    Put32(out, 4);
    Put32(out, uint32_t(a.size() - 4));
    out.insert(out.end(), packed.begin(), packed.end());
    return out;
}

void TestVisualDeltaMalformed() {
    const std::vector<uint8_t> a = Bytes("FMBS-source-one!"), b = Bytes("FMBS-source-two?"), added = Bytes("grown keys");
    std::vector<uint8_t> result;
    const std::vector<uint8_t> good = Oslv3(a, b, added, result);
    const auto parsed = ParseVisualDelta(good);
    CHECK(parsed && parsed->size() == 2);
    if (parsed && parsed->size() == 2) {
        CHECK((*parsed)[0].ops == (*parsed)[1].ops && (*parsed)[0].added == (*parsed)[1].added);
        const auto rebuilt = ApplyVisualDelta((*parsed)[0], a);
        CHECK(rebuilt && *rebuilt == result);
        CHECK(!ApplyVisualDelta((*parsed)[0], b));
        CHECK(ApplyVisualDelta((*parsed)[1], b).has_value());
    }
    const size_t name_end = 12 + 2 + 5, sources_at = name_end, ops_at = sources_at + 4 + 32;
    auto broken = [&](auto&& change) {
        std::vector<uint8_t> blob = good;
        change(blob);
        std::string why;
        const bool refused = !ParseVisualDelta(blob, &why);
        if (!refused) std::printf("malformed OSLV accepted\n");
        return refused && !why.empty();
    };
    auto put = [](std::vector<uint8_t>& blob, size_t at, uint32_t v) {
        for (int i = 0; i < 4; ++i) blob[at + i] = uint8_t(v >> (8 * i));
    };
    CHECK(broken([&](auto& x) { put(x, sources_at, 0); }));
    CHECK(broken([&](auto& x) { put(x, sources_at, 65); }));
    CHECK(broken([&](auto& x) { put(x, 8, 2); }));
    CHECK(broken([&](auto& x) { x[12] = 0xff; x[13] = 0xff; }));
    CHECK(broken([&](auto& x) { put(x, ops_at, 0xffffffffu); }));
    CHECK(broken([&](auto& x) { put(x, ops_at + 4, uint32_t(added.size() + 1)); }));
    CHECK(broken([&](auto& x) { put(x, ops_at + 8, 0x7fffffffu); }));
    CHECK(broken([&](auto& x) { put(x, ops_at + 12 + 1 + 4, 0xfffffff0u); }));
    CHECK(broken([&](auto& x) { put(x, ops_at + 12 + 1, uint32_t(a.size())); }));
    CHECK(broken([&](auto& x) { put(x, sources_at + 4 + 8, uint32_t(result.size() + 1)); }));
    CHECK(broken([&](auto& x) { x[ops_at + 12 + 9] = 2; }));
    CHECK(broken([&](auto& x) { x.back() ^= 0x55; x[x.size() - 2] ^= 0x55; }));
    CHECK(broken([&](auto& x) { x.push_back(0); }));
    CHECK(broken([&](auto& x) { x.resize(x.size() - 1); }));
    CHECK(broken([&](auto& x) { x[4] = '9'; }));
    std::printf("malformed OSLV: refused\n");
}

void TestChunkPool() {
    ChunkPool pool;
    std::vector<uint8_t> one(ChunkPool::kChunkSize * 2 + 100, 7), two = one;
    two[ChunkPool::kChunkSize + 5] = 9;
    two.back() = 1;
    const std::vector<uint8_t> copy_one = one, copy_two = two;
    const ChunkedBytes a = pool.Add(std::move(one)), b = pool.Add(std::move(two));
    CHECK(a.size == copy_one.size() && b.size == copy_two.size());
    CHECK(a.chunks.size() == 3 && b.chunks.size() == 3);
    CHECK(a.chunks[0] == b.chunks[0]);
    CHECK(a.chunks[1] != b.chunks[1] && a.chunks[2] != b.chunks[2]);
    for (const auto& [chunked, bytes] : {std::pair{&a, &copy_one}, std::pair{&b, &copy_two}}) {
        std::vector<uint8_t> joined;
        for (const SharedBytes& c : chunked->chunks) joined.insert(joined.end(), c->begin(), c->end());
        CHECK(joined == *bytes);
    }
    std::printf("chunk pool: shared first chunk, distinct changed chunks, bytes rebuild\n");
}

void TestGlyphTableInstall() {
    std::vector<uint8_t> image(0x10000, 0);
    const uint64_t desc = 0x100, page0 = 0x2000, table = 0x4000, sjis_page = 0x5000;
    const uint8_t header[16] = {0, 0, 0, 0, 0, 8, 0, 8, 1, 0x42, 0x42, 10, 10, 31, 31, 0};
    std::memcpy(&image[desc], header, 16);
    std::memcpy(&image[desc + 16], &page0, 8);
    image[page0 + 'a' * 8] = 65;
    image[page0 + 'a' * 8 + 2] = 30;
    std::memcpy(&image[table + 0x81 * 8], &sjis_page, 8);
    image[sjis_page + 0x40 * 2] = 0x00;
    image[sjis_page + 0x40 * 2 + 1] = 0x30;
    OslGlyphPatch patch{desc, {}, 0xE3, 'a', 313};
    std::memcpy(patch.header, header, 16);
    OslSjisPatch code{table, 0x81, 0xAD, 0xE3};
    char detail[200];
    std::vector<uint8_t> copy = image;
    CHECK(text_glyphs_install(copy.data(), copy.size(), &patch, 1, &code, 1, detail, sizeof(detail)));
    std::printf("glyph table install: %s\n", detail);
    CHECK(copy[page0 + 0xE3 * 8] == (313 & 0xff) && copy[page0 + 0xE3 * 8 + 1] == (313 >> 8) && copy[page0 + 0xE3 * 8 + 2] == 30);
    CHECK(copy[sjis_page + 0xAD * 2] == 0xE3 && copy[sjis_page + 0xAD * 2 + 1] == 0);
    std::vector<uint8_t> again = copy;
    CHECK(!text_glyphs_install(again.data(), again.size(), &patch, 1, nullptr, 0, detail, sizeof(detail)));
    OslGlyphPatch wrong = patch;
    wrong.header[9] = 0x2e;
    copy = image;
    CHECK(!text_glyphs_install(copy.data(), copy.size(), &wrong, 1, &code, 1, detail, sizeof(detail)) && copy == image);
    OslSjisPatch taken{table, 0x81, 0x40, 0xE3};
    copy = image;
    CHECK(!text_glyphs_install(copy.data(), copy.size(), &patch, 1, &taken, 1, detail, sizeof(detail)) && copy == image);
}

void TestEmbeddedCorpus() {
    const TextCorpus& corpus = EmbeddedTextCorpus();
    CHECK(corpus.errors.empty());
    for (const std::string& e : corpus.errors) std::printf("corpus error: %s\n", e.c_str());
    CHECK(corpus.english.size() == 462);
    CHECK(corpus.english.count("msgsheet/SystemMsg.fms") && corpus.english.count("story/a1_1"));
    CHECK(corpus.maps.count("JP") == 1);
    size_t mapped = 0;
    if (corpus.maps.count("JP")) {
        for (const auto& [resource, records] : corpus.maps.at("JP")) mapped += records.size();
    }
    size_t valid = 0;
    for (const auto& [resource, units] : corpus.english) {
        for (const std::string& u : units) {
            if (!Translatable(u)) continue;
            const auto pt = corpus.portuguese.find(TextSha256(u));
            if (pt != corpus.portuguese.end()) valid += CheckTranslation(u, pt->second).empty();
        }
    }
    std::printf("embedded corpus: %zu English resources, %zu Portuguese entries (%zu records pass), %zu Japanese records mapped\n",
                corpus.english.size(), corpus.portuguese.size(), valid, mapped);
}

bool Inked(const GlyphCell& c) {
    for (size_t i = 3; i < c.rgba.size(); i += 4) {
        if (c.rgba[i] > 24) return true;
    }
    return false;
}

void TestRealGlyphs(const std::filesystem::path& prepared) {
    const TextCorpus& corpus = EmbeddedTextCorpus();
    for (const GameBuild& build : SupportedBuilds()) {
        const std::filesystem::path app0 = prepared / build.folder;
        const auto known = std::find_if(corpus.glyph_builds.begin(), corpus.glyph_builds.end(),
                                        [&](const GlyphBuild& g) { return g.eboot_sha256 == build.eboot_sha256; });
        if (!std::filesystem::is_regular_file(app0 / "eboot.bin") || known == corpus.glyph_builds.end()) {
            std::printf("  %-24s font data: DATA UNAVAILABLE\n", build.label);
            continue;
        }
        const TextGlyphs english = PrepareGlyphs(app0, build, ResolveTextLanguage(&build, kEnglishKey), build.eboot_sha256, corpus);
        CHECK(english.atlases.empty() && english.patches.empty() && english.sjis_patches.empty());
        const TextGlyphs pt = PrepareGlyphs(app0, build, ResolveTextLanguage(&build, kPortugueseBrazilKey), build.eboot_sha256, corpus);
        for (const std::string& n : pt.notes) std::printf("    %s\n", n.c_str());
        CHECK(pt.known && pt.notes.empty());
        size_t atlases = 0, checked = 0;
        for (const GlyphFamily& family : known->families) {
            for (const GlyphFamily::Atlas& atlas : family.atlases) {
                const auto composed = std::find_if(pt.atlases.begin(), pt.atlases.end(), [&](const auto& a) {
                    return a.first == atlas.archive && a.second.first == atlas.entry;
                });
                CHECK(composed != pt.atlases.end());
                if (composed == pt.atlases.end()) continue;
                CpkArchive archive;
                CHECK(archive.Open(app0 / atlas.archive));
                const CpkEntry* entry = archive.Find(atlas.entry);
                const std::vector<uint8_t> before = entry ? archive.Read(*entry) : std::vector<uint8_t>{};
                const std::vector<uint8_t>& after = composed->second.second;
                CHECK(before.size() == after.size() && Crc32(before) == atlas.crc32);
                std::set<size_t> allowed;
                for (const GlyphRecipe& r : family.glyphs) {
                    for (const size_t b : GlyphCellBlocks(before, atlas.geometry, r.cell)) allowed.insert(b / 16);
                    const std::optional<GlyphCell> was = ReadGlyphCell(before, atlas.geometry, r.cell);
                    const std::optional<GlyphCell> now = ReadGlyphCell(after, atlas.geometry, r.cell);
                    const std::optional<GlyphCell> base = ReadGlyphCell(after, atlas.geometry, r.base_cell);
                    CHECK(was && !Inked(*was));
                    CHECK(now && Inked(*now));
                    CHECK(base && now && base->rgba != now->rgba);
                    ++checked;
                }
                bool outside = false;
                for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
                    if (before[i] != after[i] && !allowed.count(i / 16)) outside = true;
                }
                CHECK(!outside);
                ++atlases;
            }
        }
        CHECK(!pt.patches.empty());
        std::printf("  %-24s font data: PASS, %zu atlases, %zu letters drawn into blank cells, %zu glyph records, %zu Shift-JIS codes\n",
                    build.label, atlases, checked, pt.patches.size(), pt.sjis_patches.size());
    }
}

// The bytes a reader of the archive gets through its overlay.
class OverlayBuf : public std::streambuf {
public:
    OverlayBuf(const std::filesystem::path& file, const ArchiveOverlay& overlay) : file_(file, std::ios::binary), overlay_(overlay) {}

    bool Read(uint64_t offset, size_t size, std::vector<uint8_t>& out) {
        out.assign(size, 0);
        file_.clear();
        file_.seekg(int64_t(offset));
        file_.read(reinterpret_cast<char*>(out.data()), std::streamsize(size));
        for (const auto& [at, bytes] : overlay_.patches) {
            const uint64_t lo = std::max(at, offset), hi = std::min(at + bytes->size(), offset + size);
            if (lo < hi) std::memcpy(out.data() + (lo - offset), bytes->data() + (lo - at), hi - lo);
        }
        return offset + size <= overlay_.length;
    }

protected:
    pos_type seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode) override {
        pos_ = dir == std::ios_base::beg ? uint64_t(off) : dir == std::ios_base::cur ? pos_ + off : overlay_.length + off;
        setg(nullptr, nullptr, nullptr);
        return pos_type(off_type(pos_));
    }
    pos_type seekpos(pos_type p, std::ios_base::openmode m) override { return seekoff(off_type(p), std::ios_base::beg, m); }
    int_type underflow() override {
        if (pos_ >= overlay_.length) return traits_type::eof();
        const size_t n = size_t(std::min<uint64_t>(sizeof(chunk_), overlay_.length - pos_));
        std::vector<uint8_t> bytes;
        Read(pos_, n, bytes);
        std::memcpy(chunk_, bytes.data(), n);
        setg(chunk_, chunk_, chunk_ + n);
        pos_ += n;
        return traits_type::to_int_type(chunk_[0]);
    }

private:
    std::ifstream file_;
    const ArchiveOverlay& overlay_;
    uint64_t pos_ = 0;
    char chunk_[65536];
};

struct FileState {
    uintmax_t size = 0;
    std::filesystem::file_time_type time;
    std::vector<char> head;
    bool operator==(const FileState&) const = default;
};

FileState StateOf(const std::filesystem::path& file) {
    FileState s;
    s.size = std::filesystem::file_size(file);
    s.time = std::filesystem::last_write_time(file);
    std::ifstream in(file, std::ios::binary);
    s.head.resize(1 << 20);
    in.read(s.head.data(), std::streamsize(s.head.size()));
    s.head.resize(size_t(in.gcount()));
    return s;
}

// A text container read back through the overlay, or nothing.
std::optional<TextContainer> ServedContainer(const std::filesystem::path& app0, const ServedArchive& served, std::string_view entry_path) {
    OverlayBuf buf(app0 / served.archive, served.overlay);
    std::istream in(&buf);
    CpkArchive archive;
    if (!archive.OpenStream(in)) return std::nullopt;
    const CpkEntry* entry = archive.Find(entry_path);
    if (!entry) return std::nullopt;
    std::vector<uint8_t> data;
    if (!buf.Read(entry->offset, entry->size, data)) return std::nullopt;
    if (entry->compressed()) data = CrilaylaDecompress(data);
    const std::optional<std::string> resource = TextResourceOf(entry_path);
    return resource ? ParseTextContainer(*resource, data) : std::nullopt;
}

// The served unit of a resource read back through the overlay, or nothing.
std::optional<std::string> ServedUnit(const std::filesystem::path& app0, const ServedArchive& served,
                                      std::string_view entry_path, size_t index) {
    OverlayBuf buf(app0 / served.archive, served.overlay);
    std::istream in(&buf);
    CpkArchive archive;
    if (!archive.OpenStream(in)) return std::nullopt;
    const CpkEntry* entry = archive.Find(entry_path);
    if (!entry) return std::nullopt;
    std::vector<uint8_t> data;
    if (!buf.Read(entry->offset, entry->size, data)) return std::nullopt;
    if (entry->compressed()) data = CrilaylaDecompress(data);
    const std::optional<std::string> resource = TextResourceOf(entry_path);
    const std::optional<TextContainer> c = resource ? ParseTextContainer(*resource, data) : std::nullopt;
    if (!c || index >= c->units.size()) return std::nullopt;
    return c->units[index];
}

void TestRealBuilds() {
    const std::filesystem::path prepared = std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared";
    const TextCorpus& corpus = EmbeddedTextCorpus();
    for (const GameBuild& build : SupportedBuilds()) {
        const std::filesystem::path app0 = prepared / build.folder;
        if (!std::filesystem::is_regular_file(app0 / "eboot.bin")) {
            std::printf("  %-24s DATA UNAVAILABLE\n", build.label);
            continue;
        }
        std::map<std::filesystem::path, FileState> before;
        for (const char* f : build.data_files) {
            if (std::filesystem::exists(app0 / f)) before[app0 / f] = StateOf(app0 / f);
        }
        std::string line = std::string("  ") + build.label;
        line.resize(26, ' ');
        for (std::string_view key : {kEnglishKey, kPortugueseBrazilKey}) {
            const TextLanguage& language = ResolveTextLanguage(&build, key);
            const auto start = std::chrono::steady_clock::now();
            const TextTranslation t = TranslateGameText(app0, build, language, corpus);
            const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            CHECK(t.problems.empty());
            for (const std::string& p : t.problems) std::printf("    %s\n", p.c_str());
            if (!IsPortLanguage(language)) {
                CHECK(t.archives.empty());
                line += " English native |";
                continue;
            }
            char part[200];
            std::snprintf(part, sizeof(part), " %s %zu/%zu served, %zu kept, %zu archive(s), %zu visual entries, %lld ms |",
                          key == kEnglishKey ? "English" : "PT-BR", t.served, t.corpus, t.kept, t.archives.size(), t.visuals, ms);
            for (const std::string& skipped : t.visual_skipped) std::printf("    visual skipped: %s\n", skipped.c_str());
            if (key == kEnglishKey) CHECK(t.visuals == 0);
            // The title art is laid on whole or not at all: the European 1.01 layer is current, the others hold an older one.
            if (key == kPortugueseBrazilKey && std::string_view(build.folder) == "CUSA05262-1.01") CHECK(t.visuals > 0 && t.visual_skipped.empty());
            if (key == kPortugueseBrazilKey && t.visuals == 0) CHECK(!t.visual_skipped.empty());
            line += part;
            for (const ServedArchive& a : t.archives) {
                CHECK(a.overlay.length >= std::filesystem::file_size(app0 / a.archive));
                CHECK(a.entries > 0);
                std::set<const void*> unique;
                for (const auto& patch : a.overlay.patches) unique.insert(patch.second.get());
                if (a.overlay.patches.size() > 600) CHECK(unique.size() * 2 < a.overlay.patches.size());
            }
        }
        std::printf("%s\n", line.c_str());
        for (const auto& [file, state] : before) CHECK(StateOf(file) == state);
    }

    // A known record served in Portuguese on the European build, and refused when its fingerprint does not match.
    // The physical base is the Spanish layer; the English source is the paired archive's record at the same index.
    const GameBuild* eu = nullptr;
    for (const GameBuild& b : SupportedBuilds()) {
        if (std::string_view(b.folder) == "CUSA05262-1.01") eu = &b;
    }
    const std::filesystem::path app0 = prepared / "CUSA05262-1.01";
    if (!eu || !std::filesystem::is_regular_file(app0 / "eboot.bin")) {
        std::printf("fingerprint check: DATA UNAVAILABLE (CUSA05262-1.01)\n");
        return;
    }
    CpkArchive uk_re;
    CHECK(uk_re.Open(app0 / "Odin2_RE_UK.CPK"));
    CpkArchive es_re;
    CHECK(es_re.Open(app0 / "Odin2_RE_ES.CPK"));
    const CpkEntry* uk_system_entry = uk_re.Find("msgsheet/SystemMsg.fms");
    const CpkEntry* es_system_entry = es_re.Find("msgsheet/SystemMsg.fms");
    std::vector<std::string> uk_system, es_system;
    if (uk_system_entry) {
        const auto c = ParseTextContainer("msgsheet/SystemMsg.fms", uk_re.Read(*uk_system_entry));
        if (c) uk_system = c->units;
    }
    if (es_system_entry) {
        const auto c = ParseTextContainer("msgsheet/SystemMsg.fms", es_re.Read(*es_system_entry));
        if (c) es_system = c->units;
    }
    CHECK(!uk_system.empty() && uk_system.size() == es_system.size());
    size_t index = 0;
    while (index + 1 < uk_system.size() && !Translatable(uk_system[index])) ++index;
    TextCorpus known;
    known.english = corpus.english;
    known.glyph_builds = corpus.glyph_builds;
    known.portuguese[TextSha256(uk_system[index])] = "Texto de teste: não, informações, opções, coração";
    const TextLanguage& pt = ResolveTextLanguage(eu, kPortugueseBrazilKey);
    CHECK(pt.slot == "ES" && pt.ps4_id == 3 && pt.name == "Portuguese (Brazil)");
    const TextTranslation served = TranslateGameText(app0, *eu, pt, known);
    const auto sheets = std::find_if(served.archives.begin(), served.archives.end(),
                                     [](const ServedArchive& a) { return a.archive == "Odin2_RE_ES.CPK"; });
    CHECK(served.served >= 1 && sheets != served.archives.end());
    if (sheets != served.archives.end()) {
        const std::optional<std::string> unit = ServedUnit(app0, *sheets, "msgsheet/SystemMsg.fms", index);
        CHECK(unit && *unit == "Texto de teste: não, informações, opções, coração");
        CHECK(ServedUnit(app0, *sheets, "msgsheet/SystemMsg.fms", index + 1) == es_system[index + 1]);
    }
    TextCorpus wrong = known;
    wrong.portuguese.clear();
    wrong.portuguese[TextSha256(uk_system[index] + " ")] = "Texto de teste";
    CHECK(TranslateGameText(app0, *eu, pt, wrong).served == 0);
    for (const auto& [why, n] : TranslateGameText(app0, *eu, pt).reasons) std::printf("  Europe 1.01 PT-BR kept %zu: %s\n", n, why.c_str());
    std::printf("fingerprint check: known source served, changed source refused\n");

    // The six chapter prologues: the Spanish layer is the physical base, and the paired English archive gives the
    // source records that the Portuguese corpus is keyed to.
    const TextTranslation full = TranslateGameText(app0, *eu, pt, corpus);
    const auto story = std::find_if(full.archives.begin(), full.archives.end(),
                                    [](const ServedArchive& a) { return a.archive == "Odin2_OR_ES.CPK"; });
    CHECK(story != full.archives.end());

    // Every text container of the Spanish layer, read back from the final overlay, must be the Portuguese container
    // built from the paired English archive; no translatable record may keep its Spanish text.
    size_t es_containers = 0, es_records = 0, es_candidates = 0, es_served = 0, es_unmatched = 0;
    size_t es_spanish_leaks = 0, mismatched_containers = 0;
    std::map<std::string, std::unique_ptr<CpkArchive>> spanish_uk;
    const auto paired_uk = [&](const std::string& name) -> CpkArchive* {
        const std::string suffix = "_ES.CPK";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix)) return nullptr;
        const std::string target = name.substr(0, name.size() - suffix.size()) + "_UK.CPK";
        auto it = spanish_uk.find(target);
        if (it == spanish_uk.end()) {
            auto opened = std::make_unique<CpkArchive>();
            if (!opened->Open(app0 / target)) opened.reset();
            it = spanish_uk.emplace(target, std::move(opened)).first;
        }
        return it->second.get();
    };
    const auto is_es_archive = [](const std::string& name) {
        std::string suffix = name.size() >= 7 ? name.substr(name.size() - 7) : name;
        for (char& c : suffix) c = char(std::toupper(uint8_t(c)));
        return suffix == "_ES.CPK";
    };
    for (const auto& dir : std::filesystem::directory_iterator(app0)) {
        const std::string name = dir.path().filename().string();
        if (!is_es_archive(name)) continue;
        CpkArchive original;
        if (!original.Open(dir.path())) continue;
        const auto served_archive = std::find_if(full.archives.begin(), full.archives.end(),
                                                [&](const ServedArchive& a) { return a.archive == name; });
        std::optional<OverlayBuf> buf;
        CpkArchive overlaid;
        if (served_archive != full.archives.end()) {
            buf.emplace(app0 / name, served_archive->overlay);
            std::istream stream(&*buf);
            overlaid.OpenStream(stream);
        }
        CpkArchive* english = paired_uk(name);
        for (const CpkEntry& entry : original.entries()) {
            const std::optional<std::string> resource = TextResourceOf(entry.path);
            if (!resource || !corpus.english.count(*resource)) continue;
            const std::vector<uint8_t> original_bytes = original.Read(entry);
            const std::optional<TextContainer> was = ParseTextContainer(*resource, original_bytes);
            if (!was) continue;
            const size_t translatable = size_t(std::count_if(was->units.begin(), was->units.end(),
                                                             [](const std::string& u) { return Translatable(u); }));
            const CpkEntry* source_entry = english ? english->Find(entry.path) : nullptr;
            std::optional<TextContainer> source;
            if (source_entry) source = ParseTextContainer(*resource, english->Read(*source_entry));
            if (!source || source->units.size() != was->units.size()) {
                es_unmatched += translatable;
                continue;
            }
            size_t served = 0, candidates = 0;
            std::map<std::string, size_t> why;
            std::vector<std::string> units = ServedUnits(*resource, *was, pt, corpus, &source->units, nullptr, nullptr,
                                                         true, full.glyphs, served, candidates, why);
            es_candidates += candidates;
            es_served += served;
            if (!served) continue;
            TextContainer want = *was;
            want.units = std::move(units);
            std::vector<uint8_t> expected = BuildTextContainer(want);
            std::vector<uint8_t> got = original_bytes;
            const CpkEntry* target = overlaid.Find(entry.path);
            if (target && buf) {
                got.clear();
                buf->Read(target->offset, target->size, got);
            }
            ++es_containers;
            es_records += was->units.size();
            if (got.size() > expected.size() && std::all_of(got.begin() + expected.size(), got.end(),
                                                            [](uint8_t b) { return b == 0; })) {
                expected.resize(got.size(), 0);
            }
            if (got != expected) {
                ++mismatched_containers;
                std::printf("roundtrip: %s:%s served %zu bytes, expected %zu\n", name.c_str(), entry.path.c_str(),
                            got.size(), expected.size());
            }
            const std::optional<TextContainer> shown = ParseTextContainer(*resource, got);
            if (shown) {
                for (const std::string& unit : shown->units) {
                    for (const char* spanish : {"LA VALQUIRIA", "El alma refulge", "Los pájaros", "Presentado",
                                                "Productor"}) {
                        if (unit.find(spanish) != std::string::npos) ++es_spanish_leaks;
                    }
                }
            }
        }
    }
    CHECK(mismatched_containers == 0);
    CHECK(es_unmatched == 0 && es_candidates == es_served && es_spanish_leaks == 0);
    std::printf("ES text: %zu containers, %zu records, %zu/%zu translatable records, %zu unmatched, "
                "%zu Spanish leaks, %zu byte mismatches\n",
                es_containers, es_records, es_served, es_candidates, es_unmatched, es_spanish_leaks,
                mismatched_containers);

    // Japanese books in Portuguese: the page keeps its own layout, only text blocks change.
    const GameBuild* japan = nullptr;
    for (const GameBuild& b : SupportedBuilds()) {
        if (std::string_view(b.folder) == "CUSA01290-1.02") japan = &b;
    }
    const std::filesystem::path jp = prepared / "CUSA01290-1.02";
    if (!japan || !std::filesystem::is_regular_file(jp / "eboot.bin")) {
        std::printf("Japanese ArchiveViewer: DATA UNAVAILABLE (CUSA01290-1.02)\n");
        return;
    }
    const TextTranslation japanese = TranslateGameText(jp, *japan, ResolveTextLanguage(japan, kPortugueseBrazilKey), corpus);
    CHECK(japanese.pages > 200);
    size_t pages = 0, same_layout = 0;
    for (const ServedArchive& a : japanese.archives) {
        CpkArchive original;
        if (!original.Open(jp / a.archive)) continue;
        for (const CpkEntry& e : original.entries()) {
            const std::optional<std::string> r = TextResourceOf(e.path);
            if (!r || KindOf(*r) != TextKind::kArchivePage) continue;
            const std::optional<TextContainer> was = ParseTextContainer(*r, original.Read(e));
            const std::optional<TextContainer> now = ServedContainer(jp, a, e.path);
            if (!was || !now || was->units == now->units) continue;
            ++pages;
            same_layout += was->layout == now->layout && was->units.size() == now->units.size();
        }
    }
    CHECK(pages > 100 && same_layout == pages);
    std::printf("Japanese ArchiveViewer: %zu blocks in Portuguese, %zu pages served with their own layout\n", japanese.pages, pages);
}

} // namespace

// Every composed letter beside its base, as raw RGBA rows of cells, for a person to look at: <dir>/<build>-<entry>.rgba
// with the cell size and count in the name.
int GlyphSheets(const std::filesystem::path& dir) {
    const TextCorpus& corpus = EmbeddedTextCorpus();
    const std::filesystem::path prepared = std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared";
    std::filesystem::create_directories(dir);
    for (const GameBuild& build : SupportedBuilds()) {
        const auto known = std::find_if(corpus.glyph_builds.begin(), corpus.glyph_builds.end(),
                                        [&](const GlyphBuild& g) { return g.eboot_sha256 == build.eboot_sha256; });
        if (known == corpus.glyph_builds.end() || !std::filesystem::is_regular_file(prepared / build.folder / "eboot.bin")) continue;
        const TextGlyphs pt = PrepareGlyphs(prepared / build.folder, build, ResolveTextLanguage(&build, kPortugueseBrazilKey),
                                            build.eboot_sha256, corpus);
        for (const GlyphFamily& family : known->families) {
            for (const GlyphFamily::Atlas& atlas : family.atlases) {
                for (const auto& [archive, entry] : pt.atlases) {
                    if (archive != atlas.archive || entry.first != atlas.entry) continue;
                    const int size = atlas.geometry.cell, count = int(family.glyphs.size());
                    std::vector<uint8_t> out(size_t(size) * size * 2 * count * 4);
                    for (int g = 0; g < count; ++g) {
                        const int cells[2] = {family.glyphs[size_t(g)].base_cell, family.glyphs[size_t(g)].cell};
                        for (int k = 0; k < 2; ++k) {
                            const std::optional<GlyphCell> c = ReadGlyphCell(entry.second, atlas.geometry, cells[k]);
                            for (int y = 0; c && y < size; ++y) {
                                std::memcpy(&out[((size_t(y) * size * 2 * count) + size_t(2 * g + k) * size) * 4],
                                            &c->rgba[size_t(y) * size * 4], size_t(size) * 4);
                            }
                        }
                    }
                    const std::string name = std::string(build.folder) + "-" + atlas.entry + "-" + std::to_string(size) + "x" +
                                             std::to_string(2 * count) + ".rgba";
                    std::ofstream(dir / name, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
                }
            }
        }
    }
    return 0;
}

void TestPrologueMap() {
    CHECK(osl_prologue_record_count() == 41);

    const auto translate = [](const char* resource, size_t index, const char* text) {
        return osl_prologue_translate(resource, index, text, static_cast<size_t>(-1));
    };

    // Inactive: everything passes through unchanged.
    osl_prologue_set_active(0);
    CHECK(std::string_view(translate("a0_2_1", 0, "@f(8)@t(8)@X(3)LA VALQUIRIA")) == "@f(8)@t(8)@X(3)LA VALQUIRIA");
    osl_prologue_set_active(1);

    CHECK(std::string_view(translate("a0_2_1", 0, "@f(8)@t(8)@X(3)LA VALQUIRIA")) == "@f(8)@t(8)@X(3)A VALQUÍRIA");
    CHECK(std::string_view(translate("a0_2_1", 2, "@f(8)@t(8)@X(3)El alma refulge con orgullo")) ==
          "@f(8)@t(8)@X(3)A alma brilha com orgulho");
    CHECK(std::string_view(translate("a0_2_1", 4, "@f(8)@t(8)@X(3)Los pájaros poseen \nla creencia de que")) ==
          "@f(8)@t(8)@X(3)As aves acreditam que");
    CHECK(std::string_view(translate("a0_2_1", 5, "@f(8)@t(8)@X(3)este es el único camino")) ==
          "@f(8)@t(8)@X(3)este é o único meio");
    CHECK(std::string_view(translate("a0_2_1", 6, "@f(8)@t(8)@X(3)para lograr la inmortalidad.")) ==
          "@f(8)@t(8)@X(3)de ganhar uma alma imortal");

    // The source must match exactly: a changed or wrong record is returned unchanged.
    CHECK(std::string_view(translate("a0_2_1", 0, "@f(8)@t(8)@X(3)LA VALQUIRIA!")) == "@f(8)@t(8)@X(3)LA VALQUIRIA!");
    CHECK(std::string_view(translate("a0_2_1", 1, "@f(8)@t(8)@X(3)LA VALQUIRIA")) == "@f(8)@t(8)@X(3)A VALQUÍRIA");
    CHECK(std::string_view(translate("b0_2_1", 0, "@f(8)@t(8)@X(3)LA VALQUIRIA")) == "@f(8)@t(8)@X(3)LA VALQUIRIA");
    CHECK(std::string_view(translate("a0_2_1", 0, "unrelated string")) == "unrelated string");

    // A path-qualified or extension-bearing resource name resolves to the same key.
    CHECK(std::string_view(translate("A0_2_1.txt", 0, "@f(8)@t(8)@X(3)LA VALQUIRIA")) == "@f(8)@t(8)@X(3)A VALQUÍRIA");
    CHECK(std::string_view(translate("story/a0_2_1", 0, "@f(8)@t(8)@X(3)LA VALQUIRIA")) == "@f(8)@t(8)@X(3)A VALQUÍRIA");

    osl_prologue_set_active(0);
    std::printf("prologue runtime map: %zu records\n", osl_prologue_record_count());
}

// An entry of a served archive read back through its overlay, or nothing.
std::optional<std::vector<uint8_t>> ServedEntry(const std::filesystem::path& app0, const ServedArchive& served,
                                                std::string_view entry_path) {
    OverlayBuf buf(app0 / served.archive, served.overlay);
    std::istream in(&buf);
    CpkArchive archive;
    if (!archive.OpenStream(in)) return std::nullopt;
    const CpkEntry* entry = archive.Find(entry_path);
    if (!entry) return std::nullopt;
    std::vector<uint8_t> data;
    if (!buf.Read(entry->offset, entry->size, data)) return std::nullopt;
    if (entry->compressed()) data = CrilaylaDecompress(data);
    return data;
}

// The chapter prologue pages: served with their atlas in the Spanish slot of Europe for Portuguese (Brazil) only.
void TestProloguePages() {
    std::vector<VisualDeltaTarget> prologue;
    for (VisualDeltaTarget& t : PortugueseVisualDeltas()) {
        if (t.file == kPrologueVisuals) prologue.push_back(std::move(t));
    }
    CHECK(prologue.size() == 4);
    const std::filesystem::path prepared = std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared";
    const char* const kPairs[][3] = {{"Odin2_OR_ES.CPK", "Installation.mbs", "Installation.ftx"},
                                     {"Odin2_RE_ES.CPK", "GUI/REHD_Installation.mbs", "GUI/HD_Installation.ftx"}};
    size_t europe = 0;
    for (const GameBuild& build : SupportedBuilds()) {
        const std::filesystem::path app0 = prepared / build.folder;
        if (!std::filesystem::is_regular_file(app0 / "eboot.bin")) continue;
        const bool is_europe = std::string_view(build.folder).rfind("CUSA05262-", 0) == 0;
        for (const TextLanguage& language : TextLanguagesOf(&build)) {
            const TextTranslation t = TranslateGameText(app0, build, language, EmbeddedTextCorpus());
            const bool wanted = is_europe && language.key == kPortugueseBrazilKey;
            CHECK(t.prologue_pages == (wanted ? 2u : 0u));
            if (!wanted) continue;
            ++europe;
            for (const auto& pair : kPairs) {
                const auto served = std::find_if(t.archives.begin(), t.archives.end(),
                                                 [&](const ServedArchive& a) { return a.archive == pair[0]; });
                CHECK(served != t.archives.end());
                if (served == t.archives.end()) continue;
                CpkArchive original;
                CHECK(original.Open(app0 / pair[0]));
                for (const char* entry : {pair[1], pair[2]}) {
                    const std::optional<std::vector<uint8_t>> got = ServedEntry(app0, *served, entry);
                    const CpkEntry* was = original.Find(entry);
                    CHECK(got && was);
                    if (!got || !was) continue;
                    const std::vector<uint8_t> source = original.Read(*was);
                    const auto target = std::find_if(prologue.begin(), prologue.end(), [&](const VisualDeltaTarget& d) {
                        return d.name == entry && d.crc32 == Crc32(source);
                    });
                    CHECK(target != prologue.end());
                    if (target == prologue.end()) continue;
                    CHECK(got->size() == target->result_size && Crc32(*got) == target->result_crc32);
                    std::vector<uint8_t> changed = source;
                    changed[changed.size() / 2] ^= 0xFF;
                    CHECK(!ApplyVisualDelta(*target, changed));
                }
            }
            for (const std::string& skipped : t.visual_skipped) CHECK(skipped.find("prologue") == std::string::npos);
        }
    }
    std::printf("chapter prologue pages: %zu Europe build(s) served in Portuguese (Brazil), every other build and "
                "language left as it is\n", europe);
}

const VisualWatch* WatchOf(const TextTranslation& t, std::string_view archive, std::string_view entry) {
    for (const VisualWatch& w : t.visual_watches) {
        if (w.archive == archive && w.entry == entry) return &w;
    }
    return nullptr;
}

// Localized visual telemetry: which entries a Portuguese (Brazil) run watches, and what the runtime reports when the
// game reads them through the real overlays (Europe 1.01); a prologue source that does not match is reported as a miss.
void TestVisualTelemetry() {
    const std::filesystem::path prepared = std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared";
    const GameBuild* eu = nullptr;
    for (const GameBuild& b : SupportedBuilds()) {
        if (std::string_view(b.folder) == "CUSA05262-1.01") eu = &b;
    }
    const std::filesystem::path app0 = prepared / "CUSA05262-1.01";
    if (!eu || !std::filesystem::is_regular_file(app0 / "eboot.bin")) {
        std::printf("visual telemetry: DATA UNAVAILABLE (CUSA05262-1.01)\n");
        return;
    }
    const TextCorpus& corpus = EmbeddedTextCorpus();
    CHECK(corpus.untranslated_visuals.size() == 61);
    for (const TextLanguage& language : TextLanguagesOf(eu)) {
        if (language.key == kPortugueseBrazilKey) continue;
        CHECK(TranslateGameText(app0, *eu, language, corpus).visual_watches.empty());
    }
    for (const GameBuild& build : SupportedBuilds()) {
        if (!std::filesystem::is_regular_file(prepared / build.folder / "eboot.bin")) continue;
        const TextLanguage& english = ResolveTextLanguage(&build, kEnglishKey);
        if (IsPortLanguage(english)) CHECK(TranslateGameText(prepared / build.folder, build, english, corpus).visual_watches.empty());
    }

    const TextTranslation t = TranslateGameText(app0, *eu, ResolveTextLanguage(eu, kPortugueseBrazilKey), corpus);
    const char* const kPrologue[][2] = {{"Odin2_OR_ES.CPK", "Installation.mbs"}, {"Odin2_OR_ES.CPK", "Installation.ftx"},
                                        {"Odin2_RE_ES.CPK", "GUI/REHD_Installation.mbs"}, {"Odin2_RE_ES.CPK", "GUI/HD_Installation.ftx"}};
    size_t served = 0, untranslated = 0, missed = 0;
    for (const VisualWatch& w : t.visual_watches) {
        served += w.kind == VisualWatchKind::kServed || w.kind == VisualWatchKind::kPrologue;
        untranslated += w.kind == VisualWatchKind::kUntranslated;
        missed += w.kind == VisualWatchKind::kMissed;
    }
    CHECK(served >= t.visuals && missed == 0 && untranslated > 0);
    for (const auto& [archive, entry] : kPrologue) {
        const VisualWatch* w = WatchOf(t, archive, entry);
        CHECK(w && w->kind == VisualWatchKind::kPrologue);
    }
    for (const char* entry : {"Other/HD_M00.ftx", "GUI/HD_ArchiveFrame00.ftx", "GUI/REHD_SKILL_COMMON.mbs"}) {
        const VisualWatch* w = WatchOf(t, "Odin2_RE_ES.CPK", entry);
        CHECK(w && w->kind == VisualWatchKind::kUntranslated);
    }
    CHECK(WatchOf(t, "Odin2_OR_ES.CPK", "Logo.mbs") && WatchOf(t, "Odin2_OR_ES.CPK", "Logo.mbs")->kind == VisualWatchKind::kUntranslated);
    for (const char* entry : {"GUI/HD_fin1.ftx", "GUI/REHD_fin1.mbs", "GUI/HD_logo.ftx", "GUI/HD_Bookshelf.ftx"}) {
        CHECK(!WatchOf(t, "Odin2_RE_ES.CPK", entry));
    }
    for (const VisualWatch& w : t.visual_watches) {
        CHECK(w.archive.find('/') == std::string::npos && w.archive.find('\\') == std::string::npos);
        CHECK(w.size > 0);
    }

    // The game's reads, through the overlays and watches the loader installs.
    const std::filesystem::path user = std::filesystem::temp_directory_path() / "osl-visual-telemetry";
    std::filesystem::create_directories(user);
    runtime_file_configure(app0.string().c_str(), user.string().c_str());
    std::vector<std::vector<RuntimeFilePatch>> tables;
    for (const ServedArchive& a : t.archives) {
        std::vector<RuntimeFilePatch>& table = tables.emplace_back();
        for (const auto& [offset, bytes] : a.overlay.patches) table.push_back({offset, bytes->size(), bytes->data()});
        CHECK(runtime_file_overlay(("/app0/" + a.archive).c_str(), a.overlay.length, table.data(), table.size()) == 0);
    }
    std::map<std::string, std::vector<RuntimeFileWatch>> by_archive;
    for (const VisualWatch& w : t.visual_watches) {
        by_archive[w.archive].push_back({w.offset, w.size, w.entry.c_str(), static_cast<int>(w.kind)});
    }
    for (const auto& [archive, items] : by_archive) {
        CHECK(runtime_file_watch(("/app0/" + archive).c_str(), archive.c_str(), "ES", items.data(), items.size()) == 0);
    }
    const auto read_entry = [&](const VisualWatch& w) {
        const int64_t fd = runtime_file_open(("/app0/" + w.archive).c_str(), 0, 0);
        std::vector<uint8_t> data(size_t(w.size));
        const bool ok = fd >= 3 && runtime_file_pread(int(fd), data.data(), w.size, int64_t(w.offset)) == int64_t(w.size);
        if (fd >= 3) runtime_file_close(int(fd));
        return ok ? data : std::vector<uint8_t>{};
    };
    std::vector<VisualDeltaTarget> deltas;
    for (VisualDeltaTarget& d : PortugueseVisualDeltas()) {
        if (d.file == kPrologueVisuals) deltas.push_back(std::move(d));
    }
    RuntimeFileWatchCounts c{};
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& [archive, entry] : kPrologue) {
            const VisualWatch* w = WatchOf(t, archive, entry);
            if (!w) continue;
            const std::vector<uint8_t> data = read_entry(*w);
            CHECK(std::any_of(deltas.begin(), deltas.end(), [&](const VisualDeltaTarget& d) {
                return d.name == entry && d.result_size == data.size() && d.result_crc32 == Crc32(data);
            }));
        }
        runtime_file_watch_counts(&c);
        CHECK(c.visual_unique_hits == 4 && c.visual_hits == size_t(4 * (pass + 1)) && c.visual_missing == 0);
    }
    for (int pass = 0; pass < 2; ++pass) read_entry(*WatchOf(t, "Odin2_RE_ES.CPK", "Other/HD_M00.ftx"));
    if (CpkArchive re; re.Open(app0 / "Odin2_RE_ES.CPK")) {
        if (const CpkEntry* fin = re.Find("GUI/HD_fin1.ftx")) read_entry({"Odin2_RE_ES.CPK", fin->path, fin->offset, fin->size});
    }
    runtime_file_watch_counts(&c);
    CHECK(c.localized_untranslated_hits == 1 && c.visual_missing == 0 && c.visual_unique_hits == 4);

    // A prologue layout that is not the Spanish one: refused with its atlas, reported as missed, the original kept.
    const std::filesystem::path broken = std::filesystem::temp_directory_path() / "osl-visual-telemetry-app0";
    std::filesystem::remove_all(broken);
    std::filesystem::create_directories(broken);
    CpkArchive or_es;
    CHECK(or_es.Open(app0 / "Odin2_OR_ES.CPK"));
    const CpkEntry* mbs = or_es.Find("Installation.mbs");
    const CpkEntry* ftx = or_es.Find("Installation.ftx");
    CHECK(mbs && ftx);
    if (!mbs || !ftx) return;
    std::string layout = osl_test::cpk_test::Bytes(or_es.Read(*mbs));
    layout[layout.size() / 2] ^= 0x5A;
    osl_test::cpk_test::WriteAll(broken / "Odin2_OR_ES.CPK",
                       osl_test::cpk_test::BuildCpk({{"Installation.mbs", layout, false}, {"Installation.ftx", osl_test::cpk_test::Bytes(or_es.Read(*ftx)), false}}, false));
    const TextTranslation refused = TranslateGameText(broken, *eu, ResolveTextLanguage(eu, kPortugueseBrazilKey), corpus);
    CHECK(refused.prologue_pages == 0);
    CHECK(std::any_of(refused.visual_skipped.begin(), refused.visual_skipped.end(), [](const std::string& s) {
        return s.find("chapter prologues left in Spanish: Odin2_OR_ES.CPK: Installation.mbs") != std::string::npos;
    }));
    CpkArchive kept;
    CHECK(kept.Open(broken / "Odin2_OR_ES.CPK"));
    for (const char* entry : {"Installation.mbs", "Installation.ftx"}) {
        const VisualWatch* w = WatchOf(refused, "Odin2_OR_ES.CPK", entry);
        const CpkEntry* original = kept.Find(entry);
        CHECK(w && w->kind == VisualWatchKind::kMissed && original && w->offset == original->offset);
    }
    CHECK(kept.Find("Installation.mbs") && osl_test::cpk_test::Bytes(kept.Read(*kept.Find("Installation.mbs"))) == layout);
    std::filesystem::remove_all(broken);
    std::printf("visual telemetry: %zu watched entries on Europe 1.01 (%zu served, %zu untranslated), prologue reads "
                "reported once, refused prologue reported as missed\n", t.visual_watches.size(), served, untranslated);
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--glyph-sheet") return GlyphSheets(argv[2]);
    const bool real = argc < 2 || std::string_view(argv[1]) != "--static";
    TestLanguageLists();
    TestContainers();
    TestControlSyntax();
    TestPortugueseLetters();
    TestEmbeddedCorpus();
    TestGlyphData();
    TestGlyphTableInstall();
    TestVisualDelta();
    TestVisualDeltaMalformed();
    TestChunkPool();
    TestPrologueMap();
    if (real) {
        TestRealBuilds();
        TestProloguePages();
        TestVisualTelemetry();
        TestRealGlyphs(std::filesystem::path(OSL_SOURCE_DIR) / "game" / "prepared");
    }
    std::printf("text translation: %s (%d checks, %d failed)\n", failures ? "FAIL" : "OK", checks, failures);
    return failures ? 1 : 0;
}
