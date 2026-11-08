// The launcher's page icons and header art without a window: every page has an icon that rasterizes centred, the icon
// set is exactly what the pages use, its sources and licences are written down, and the header art is placed, split and
// scaled as the software renderer needs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "header_art.h"
#include "options.h"
#include "page_icons.h"

namespace fs = std::filesystem;
using namespace osl;

namespace {

int checks = 0;
int failures = 0;
#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        ++checks;                                                                                                      \
        if (!(condition)) {                                                                                            \
            ++failures;                                                                                                \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                                                   \
        }                                                                                                              \
    } while (0)

std::string Read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct Box {
    int min_x = 1 << 30, min_y = 1 << 30, max_x = -1, max_y = -1;
};

Box InkBox(const std::vector<uint8_t>& rgba, int pixels) {
    Box box;
    for (int y = 0; y < pixels; ++y) {
        for (int x = 0; x < pixels; ++x) {
            if (rgba[(size_t(y) * size_t(pixels) + size_t(x)) * 4 + 3] < 24) continue;
            box.min_x = std::min(box.min_x, x);
            box.min_y = std::min(box.min_y, y);
            box.max_x = std::max(box.max_x, x);
            box.max_y = std::max(box.max_y, y);
        }
    }
    return box;
}

void TestPageIcons() {
    const fs::path icons = fs::path(OSL_SOURCE_DIR) / "assets" / "launcher" / "icons";
    std::set<std::string> used;
    for (const PageId page : kPageOrder) {
        const PageIconAsset* asset = FindPageIcon(page);
        CHECK(asset != nullptr);
        if (!asset) continue;
        used.insert(asset->file);
        CHECK(asset->optical_scale > 0.5f && asset->optical_scale <= 1.0f);
        CHECK(fs::exists(icons / asset->file));
        for (const int pixels : {16, 21, 24, 32, 42, 64}) {
            const std::vector<uint8_t> rgba = RenderPageIcon(page, pixels);
            CHECK(rgba.size() == size_t(pixels) * size_t(pixels) * 4);
            if (rgba.size() != size_t(pixels) * size_t(pixels) * 4) continue;
            bool white = true;
            size_t inked = 0;
            for (size_t i = 0; i < rgba.size(); i += 4) {
                white = white && rgba[i] == 255 && rgba[i + 1] == 255 && rgba[i + 2] == 255;
                inked += rgba[i + 3] > 0;
            }
            CHECK(white && inked > 0 && inked < rgba.size() / 4);
            const Box box = InkBox(rgba, pixels);
            const int slack = 1 + pixels / 24;
            CHECK(std::abs((box.min_x + box.max_x + 1) - pixels) <= slack && std::abs((box.min_y + box.max_y + 1) - pixels) <= slack);
            CHECK(std::max(box.max_x - box.min_x, box.max_y - box.min_y) + 1 <= pixels);
        }
    }
    CHECK(used.size() == std::size(kPageOrder));

    std::set<std::string> shipped;
    for (const fs::directory_entry& entry : fs::directory_iterator(icons)) {
        if (entry.path().extension() == ".svg") shipped.insert(entry.path().filename().string());
    }
    CHECK(shipped == used);

    const std::string sources = Read(icons / "SOURCES.md");
    const std::string licenses = Read(fs::path(OSL_SOURCE_DIR) / "packaging" / "licenses" / "THIRD-PARTY-LICENSES.txt");
    for (const std::string& file : shipped) {
        CHECK(Has(sources, file) && Has(licenses, file));
    }
    CHECK(Has(licenses, "nanosvg") && Has(licenses, "CC Attribution") && Has(licenses, "Vectopus"));

    CHECK(FindPageIcon(PageId::kBugReport) && std::string(FindPageIcon(PageId::kBugReport)->file) == "bug-report.svg");
    CHECK(RenderPageIcon(PageId::kPlay, 0).empty() && RenderPageIcon(PageId::kPlay, 2000).empty() && RenderPageIcon(PageId::kPlay, -4).empty());
    const char garbage[] = "this is not an svg";
    CHECK(RasterizeIcon(nullptr, 10, 1.0f, 24).empty() && RasterizeIcon(reinterpret_cast<const uint8_t*>(garbage), 0, 1.0f, 24).empty());
    CHECK(RasterizeIcon(reinterpret_cast<const uint8_t*>(garbage), sizeof(garbage) - 1, 1.0f, 24).empty());
    const char empty_svg[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"></svg>";
    CHECK(RasterizeIcon(reinterpret_cast<const uint8_t*>(empty_svg), sizeof(empty_svg) - 1, 1.0f, 24).empty());
}

void TestHeaderArt() {
    const float aspect = 3840.0f / 432.0f;
    const HeaderArtRect rect = PlaceHeaderArt(10.0f, 1200.0f, 150.5f, aspect);
    CHECK(rect.max_x == 1200.0f && rect.min_y == 10.0f);
    CHECK(rect.max_y - rect.min_y == 141.0f);
    CHECK(rect.max_x - rect.min_x == std::round(141.0f * aspect));

    for (const float width : {1.0f, 127.0f, 128.0f, 129.0f, 1250.0f}) {
        const HeaderArtRect wide{0.0f, 0.0f, width, 100.0f};
        const std::vector<HeaderArtStrip> strips = SplitHeaderArt(wide);
        CHECK(!strips.empty() && strips.front().min_x == 0.0f && strips.front().min_u == 0.0f);
        CHECK(strips.back().max_x == width && strips.back().max_u == 1.0f);
        for (size_t i = 0; i < strips.size(); ++i) {
            CHECK(strips[i].max_x - strips[i].min_x <= float(kHeaderArtStripWidth) && strips[i].max_x > strips[i].min_x);
            if (i > 0) CHECK(strips[i].min_x == strips[i - 1].max_x && strips[i].min_u == strips[i - 1].max_u);
        }
    }

    std::vector<uint8_t> solid(8 * 8 * 4);
    for (size_t i = 0; i < solid.size(); i += 4) {
        solid[i] = 200;
        solid[i + 1] = 100;
        solid[i + 2] = 50;
        solid[i + 3] = 255;
    }
    const std::vector<uint8_t> same = ScaleArtRgba(solid.data(), 8, 8, 8, 8);
    CHECK(same == solid);
    const std::vector<uint8_t> small = ScaleArtRgba(solid.data(), 8, 8, 3, 5);
    CHECK(small.size() == 3 * 5 * 4);
    for (size_t i = 0; i + 3 < small.size(); i += 4) {
        CHECK(small[i] == 200 && small[i + 1] == 100 && small[i + 2] == 50 && small[i + 3] == 255);
    }

    // Fully transparent texels add no colour to a stroke's edge.
    std::vector<uint8_t> half(2 * 1 * 4);
    half[0] = 255; half[1] = 255; half[2] = 255; half[3] = 255;
    half[4] = 0; half[5] = 0; half[6] = 0; half[7] = 0;
    const std::vector<uint8_t> blended = ScaleArtRgba(half.data(), 2, 1, 1, 1);
    CHECK(blended.size() == 4 && blended[0] == 255 && blended[1] == 255 && blended[2] == 255 && blended[3] >= 127 && blended[3] <= 128);

    CHECK(ScaleArtRgba(nullptr, 8, 8, 4, 4).empty() && ScaleArtRgba(solid.data(), 0, 8, 4, 4).empty());
    CHECK(ScaleArtRgba(solid.data(), 8, 8, 0, 4).empty() && ScaleArtRgba(solid.data(), 8, 8, 4, -1).empty());

    const fs::path art = fs::path(OSL_SOURCE_DIR) / "assets" / "launcher" / "header-art.png";
    const std::string png = Read(art);
    CHECK(png.size() > 8 && png.compare(0, 4, "\x89PNG") == 0);
    CHECK(png.size() > 24 && uint8_t(png[16]) == 0 && uint8_t(png[17]) == 0 && uint8_t(png[18]) == 0x0F && uint8_t(png[19]) == 0 &&
          uint8_t(png[20]) == 0 && uint8_t(png[21]) == 0 && uint8_t(png[22]) == 0x01 && uint8_t(png[23]) == 0xB0);
}

} // namespace

int main() {
    TestPageIcons();
    TestHeaderArt();
    std::printf("launcher assets: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
