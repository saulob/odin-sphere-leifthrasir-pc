// The launcher's and the in-game port UI's Portuguese (Brazil) text without a window: the table, the language switch,
// text composed with values, the C lookup, the tables the pages draw from, and a scan of the sources for UI text
// without a row.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cheats_page.h"
#include "controller_prompts.h"
#include "game_files.h"
#include "game_install.h"
#include "game_profile.h"
#include "incident_client.h"
#include "incident_report.h"
#include "localization/ui_text.h"
#include "options.h"
#include "osl_cheats.h"
#include "osl_keys.h"
#include "osl_pad.h"
#include "osl_resolution.h"
#include "osl_ui_text.h"
#include "report_history.h"
#include "settings.h"
#include "trophies/trophy_data.h"

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

bool ValidUtf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size() || (length == 2 && c < 0xC2)) {
            return false;
        }
        for (size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

// Text decoded as Latin-1 or Windows-1252 and saved again as UTF-8: "Ã©" for "é", "Â·" for "·".
bool LooksMisencoded(std::string_view text) {
    return text.find("\xC3\x83") != std::string_view::npos || text.find("\xC3\x82") != std::string_view::npos ||
           text.find("\xEF\xBF\xBD") != std::string_view::npos;
}

// The printf conversions in order ("%d%d%%" as "%d %d"); %% is not one.
std::string Conversions(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            continue;
        }
        if (i + 1 < text.size() && text[i + 1] == '%') {
            ++i;
            continue;
        }
        size_t end = i + 1;
        while (end < text.size() && !std::isalpha(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        out += text.substr(i, end - i + 1) + " ";
        i = end;
    }
    return out;
}

const std::set<std::string>& English() {
    static const std::set<std::string> english = [] {
        std::set<std::string> keys;
        for (const localization::UiTextRow& row : localization::PortugueseBrazilUiText().rows) {
            keys.insert(row.english);
        }
        return keys;
    }();
    return english;
}

// A row exists for the text, or the text is a row's English with values in its %s places.
bool Covered(const std::string& english) {
    if (English().count(english)) {
        return true;
    }
    localization::SetActiveLanguage(kPortugueseBrazilKey);
    const bool composed = TrValue(english) != english;
    localization::SetActiveLanguage(kEnglishKey);
    return composed;
}

void TestTable() {
    for (const char* file : {"launcher/localization/ui_text.h", "launcher/localization/ui_text.cpp",
                             "launcher/localization/pt_br_ui_text.cpp", "src/osl_ui_text.h"}) {
        const std::string bytes = Read(fs::path(OSL_SOURCE_DIR) / file);
        CHECK(!bytes.empty());
        CHECK(ValidUtf8(bytes));
        CHECK(!LooksMisencoded(bytes));
        CHECK(bytes.find('\r') == std::string::npos);
    }
    const localization::UiTextTable& table = localization::PortugueseBrazilUiText();
    CHECK(table.errors.empty());
    for (const std::string& error : table.errors) {
        std::printf("  table: %s\n", error.c_str());
    }
    CHECK(table.rows.size() > 400);
    std::set<std::string> english;
    for (const localization::UiTextRow& row : table.rows) {
        CHECK(!row.english.empty() && !row.text.empty());
        CHECK(ValidUtf8(row.english) && ValidUtf8(row.text));
        CHECK(!LooksMisencoded(row.english) && !LooksMisencoded(row.text));
        if (!english.insert(row.english).second) {
            std::printf("  duplicate row: %s\n", row.english.c_str());
            CHECK(false);
        }
        if (Conversions(row.english) != Conversions(row.text)) {
            std::printf("  conversions differ: %s\n", row.english.c_str());
            CHECK(false);
        }
        CHECK(std::count(row.english.begin(), row.english.end(), '\n') ==
              std::count(row.text.begin(), row.text.end(), '\n'));
        CHECK(row.english.find("%s%s") == std::string::npos);
    }

    const auto parsed = localization::ParseUiTextTable("A\\sB\tC\\nD\n\nE\tF\r\nno tab\nG\t\nH\tI\tJ\nK\\q\tL\n");
    CHECK(parsed.rows.size() == 2 && parsed.errors.size() == 4);
    CHECK(parsed.rows.size() == 2 && parsed.rows[0].english == "A B" && parsed.rows[0].text == "C\nD" &&
          parsed.rows[1].english == "E" && parsed.rows[1].text == "F");
}

void TestLanguageSwitch() {
    localization::SetActiveLanguage(kPortugueseBrazilKey);
    CHECK(localization::PortugueseBrazilSelected());
    const std::map<std::string, std::string> expected = {
        {"Play", "Jogar"},
        {"Display", "Tela"},
        {"Graphics", "Gráficos"},
        {"Gameplay", "Jogabilidade"},
        {"Controls", "Controles"},
        {"Cheats", "Trapaças"},
        {"Trophies", "Troféus"},
        {"About", "Sobre"},
        {"Bug Report", "Relatar problema"},
        {"My Reports", "Meus relatórios"},
        {"Menu & Subtitles", "Menus e legendas"},
        {"Renderer", "Renderizador"},
        {"Render resolution", "Resolução de renderização"},
        {"Aspect Ratio", "Proporção de tela"},
        {"Frame rate", "Taxa de quadros"},
        {"Skip intro", "Pular introdução"},
        {"Unlock Bookshelf", "Desbloquear Estante"},
        {"Controller prompts", "Ícones de botões"},
        {"Portuguese (Brazil)", "Português (Brasil)"},
        {"English", "Inglês"},
        {"Japanese", "Japonês"},
        {"Traditional Chinese", "Chinês tradicional"},
        {"Korean", "Coreano"},
        {"PLAY", "JOGAR"},
        {"CHEATS", "TRAPAÇAS"},
        {"UPDATE NOW", "ATUALIZAR AGORA"},
        {"Vulkan", "Vulkan"},
    };
    for (const auto& [english, text] : expected) {
        if (std::string(Tr(english.c_str())) != text) {
            std::printf("  %s: %s\n", english.c_str(), Tr(english.c_str()));
            CHECK(false);
        }
        CHECK(Tr(english) == text);
    }
    for (const localization::UiTextRow& row : localization::PortugueseBrazilUiText().rows) {
        CHECK(std::string(Tr(row.english.c_str())) == row.text);
    }
    const std::string unknown = "Not a launcher string 12345";
    CHECK(Tr(unknown) == unknown && std::string(Tr(unknown.c_str())) == unknown && TrValue(unknown) == unknown);
    CHECK(Tr(static_cast<const char*>(nullptr)) == nullptr);

    for (const char* key : {"english", "french", "german", "spanish", "italian", "japanese", "traditional_chinese",
                            "korean", "", "portuguese", "Portuguese (Brazil)", "pt_br", "portuguese_brazil "}) {
        localization::SetActiveLanguage(key);
        CHECK(!localization::PortugueseBrazilSelected());
        CHECK(std::string(Tr("Play")) == "Play" && Tr(std::string("Graphics")) == "Graphics");
        CHECK(TrValue("Install the X update package to play it.") == "Install the X update package to play it.");
        CHECK(std::string(localization::TranslateLauncherText("Save")) == "Save");
        CHECK(std::string(osl_ui_text("CHEATS")) == "CHEATS");
    }

    // Live: the Menu & Subtitles choice switches back and forth with no restart.
    for (int round = 0; round < 3; ++round) {
        localization::SetActiveLanguage(kPortugueseBrazilKey);
        CHECK(std::string(Tr("Play")) == "Jogar" && std::string(osl_ui_text("CHEATS")) == "TRAPAÇAS");
        localization::SetActiveLanguage("french");
        CHECK(std::string(Tr("Play")) == "Play" && std::string(osl_ui_text("CHEATS")) == "CHEATS");
        localization::SetActiveLanguage(kEnglishKey);
        CHECK(std::string(Tr("Play")) == "Play");
    }
}

void TestValues() {
    localization::SetActiveLanguage(kPortugueseBrazilKey);
    CHECK(TrValue("Install the CUSA05083 1.01 USA update package to play it.") ==
          "Instale o pacote de atualização CUSA05083 1.01 USA para jogar essa versão.");
    CHECK(TrValue("This system's Vulkan runtime supports only 1.0, but Vulkan 1.3 or newer is required.") ==
          "O runtime Vulkan deste sistema suporta apenas a versão 1.0, mas é necessário o Vulkan 1.3 ou mais recente.");
    CHECK(TrValue("Cannot write the update file a/b %s %d.") ==
          "Não foi possível gravar o arquivo de atualização a/b %s %d.");
    CHECK(TrValue("Cannot write C:\\x.") == "Não foi possível gravar C:\\x.");
    CHECK(TrValue("Version 0.3.0 is available") == "A versão 0.3.0 está disponível");
    CHECK(TrValue("Display 2") == "Tela 2");
    CHECK(TrValue("F9 Close") == "F9 Fechar");
    CHECK(TrValue("Version  is available") == "Version  is available");
    CHECK(TrValue("Install failed: " + Tr(std::string("Not enough free space"))) ==
          "Falha na instalação: Espaço livre insuficiente");
    std::string help = kCheatsHelp;
    help.replace(help.find(kCheatsHotkeyName), sizeof(kCheatsHotkeyName) - 1, "Home");
    CHECK(TrValue(help).find("Pressione Home durante o jogo") != std::string::npos);
    CHECK(TrValue(kCheatsHelp).find("Pressione F10 durante o jogo") != std::string::npos);

    // TRG keeps the pointer it was given for the frame: composed text is translated once and kept.
    const char* first = localization::TranslateLauncherText("Reading Odin2_RE_US.CPK...");
    const char* second = localization::TranslateLauncherText("Reading Odin2_RE_US.CPK...");
    CHECK(std::string(first) == "Lendo Odin2_RE_US.CPK..." && first == second);
    const char* exact = localization::TranslateLauncherText("Save");
    CHECK(std::string(exact) == "Salvar");
    const char* missing = "No row 987";
    CHECK(localization::TranslateLauncherText(missing) == missing);
    CHECK(std::string(localization::TranslateLauncherText("Native (1080p) \xE2\x80\x94 1920 \xC3\x97 1080")) ==
          "Nativa (1080p) \xE2\x80\x94 1920 \xC3\x97 1080");

    char buffer[64];
    CHECK(osl_ui_text_value("F10 Close", buffer, sizeof(buffer)) == std::string("F10 Fechar").size());
    CHECK(std::string(buffer) == "F10 Fechar");
    CHECK(osl_ui_text_value("F10 Close", buffer, 4) == std::string("F10 Fechar").size());
    CHECK(std::string(buffer) == "F10");
    CHECK(OslUiTextValue("Skip Intro: On") == "Pular introdução: ligado");
    CHECK(OslUiTextValue("Home Close") == "Home Fechar");
    CHECK(std::string(osl_ui_text("Loading shader cache %u%%")) == "Carregando o cache de shaders %u%%");
    localization::SetActiveLanguage(kEnglishKey);
    CHECK(OslUiTextValue("F10 Close") == "F10 Close");
}

void TestSettingsFile() {
    const fs::path folder = fs::temp_directory_path() / "osl_ui_text_tests";
    fs::create_directories(folder);
    const fs::path file = folder / "osl.ini";
    const auto write = [&](const char* text) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << text;
    };
    write("text_language = portuguese_brazil\n");
    osl_ui_text_load_language(file.string().c_str());
    CHECK(localization::PortugueseBrazilSelected() && std::string(osl_ui_text("Save game")) == "Salvar jogo");
    write("text_language = french\n");
    osl_ui_text_load_language(file.string().c_str());
    CHECK(!localization::PortugueseBrazilSelected() && std::string(osl_ui_text("Save game")) == "Save game");
    write("text_language = portuguese_brazil\n");
    osl_ui_text_load_language(file.string().c_str());
    CHECK(localization::PortugueseBrazilSelected());
    write("vsync = true\n");
    osl_ui_text_load_language(file.string().c_str());
    CHECK(!localization::PortugueseBrazilSelected());
    osl_ui_text_load_language((folder / "missing.ini").string().c_str());
    CHECK(!localization::PortugueseBrazilSelected());
    osl_ui_text_load_language(nullptr);
    CHECK(!localization::PortugueseBrazilSelected());

    // The launcher's pending choice decides, exactly as the Gameplay page sets it.
    LauncherSettings settings(file);
    settings.Set(kTextLanguageKey, std::string(kPortugueseBrazilKey));
    localization::SetActiveLanguage(settings.Get(kTextLanguageKey, kEnglishKey));
    CHECK(std::string(Tr("Play")) == "Jogar");
    settings.Set(kTextLanguageKey, "japanese");
    localization::SetActiveLanguage(settings.Get(kTextLanguageKey, kEnglishKey));
    CHECK(std::string(Tr("Play")) == "Play");
    fs::remove_all(folder);
}

void ExpectCovered(const std::string& english, const char* where, int& missing) {
    if (english.empty() || Covered(english)) {
        return;
    }
    ++missing;
    std::printf("  no row (%s): %s\n", where, english.c_str());
}

void TestDataTables() {
    int missing = 0;
    for (int page = 0; page < static_cast<int>(std::size(kPageOrder)); ++page) {
        ExpectCovered(PageName(kPageOrder[page]), "page", missing);
    }
    for (const GameBuild& build : SupportedBuilds()) {
        for (const TextLanguage& language : TextLanguagesOf(&build)) {
            ExpectCovered(std::string(language.name), "Menu & Subtitles", missing);
        }
    }
    // Button names, the same in every language.
    const std::set<std::string> button_names = {"A", "B", "X", "Y", "View / Back", "Menu / Start", "LB / L1", "RB / R1",
                                                "LT / L2", "RT / R2", "L1 (LB)", "R1 (RB)", "L2 (LT)", "R2 (RT)",
                                                "Options (Start)", "Options (Menu)", "Touchpad (View)", "L1", "R1",
                                                "L2", "R2", "L3", "R3"};
    for (int action = 0; action < OSL_KEY_COUNT; ++action) {
        if (!button_names.count(osl_key_actions[action].label)) {
            ExpectCovered(osl_key_actions[action].label, "key", missing);
        }
    }
    for (int action = 0; action < OSL_PAD_COUNT; ++action) {
        if (!button_names.count(osl_pad_actions[action].label)) {
            ExpectCovered(osl_pad_actions[action].label, "controller row", missing);
        }
    }
    for (int source = 0; source < 256; ++source) {
        const char* label = osl_pad_source_label(source);
        if (label && *label && !button_names.count(label)) {
            ExpectCovered(label, "controller button", missing);
        }
    }
    for (const OslCheatInfo& cheat : osl_cheats) {
        ExpectCovered(cheat.label, "cheat", missing);
        ExpectCovered(cheat.description, "cheat", missing);
        ExpectCovered(cheat.section, "cheat section", missing);
    }
    ExpectCovered(osl_experience_multiplier.label, "cheat", missing);
    ExpectCovered(osl_experience_multiplier.description, "cheat", missing);
    ExpectCovered(osl_experience_label(OSL_EXPERIENCE_OFF), "cheat", missing);
    ExpectCovered(kExperienceSection, "cheat section", missing);
    for (const RendererChoice& choice : kRendererChoices) {
        if (choice.name.find("experimental") == std::string_view::npos && choice.name != "Vulkan") {
            ExpectCovered(std::string(choice.name), "renderer", missing);
        }
    }
    for (const PresentationChoice& choice : kPresentationChoices) {
        ExpectCovered(std::string(choice.name), "presentation", missing);
    }
    ExpectCovered(ResolutionLabel(osl_resolutions[osl_resolution_index("native")]), "render resolution", missing);
    ExpectCovered(osl_aspect_ratios[0].name, "aspect ratio", missing);
    ExpectCovered(FrameRateLabel(0), "frame rate", missing);
    ExpectCovered(FrameRateLabel(60), "frame rate", missing);
    for (int status = 0; status <= static_cast<int>(InstallStatus::kUpToDate); ++status) {
        const InstallMessage message = DescribeInstall(static_cast<InstallStatus>(status));
        ExpectCovered(message.title, "install", missing);
        ExpectCovered(message.text, "install", missing);
    }
    for (int outcome = 1; outcome <= static_cast<int>(ReportOutcome::kClientMissing); ++outcome) {
        ExpectCovered(OutcomeTitle(static_cast<ReportOutcome>(outcome)), "bug report", missing);
        ExpectCovered(OutcomeAdvice(static_cast<ReportOutcome>(outcome)), "bug report", missing);
    }
    for (const char* status :
         {"new", "investigating", "fixed", "cannot_reproduce", "duplicate", "not_a_bug", "closed"}) {
        ExpectCovered(ReportStatusText(status), "report status", missing);
    }
    for (int role = 1; role <= static_cast<int>(LogRole::kOtherLauncher); ++role) {
        ExpectCovered(LogRoleText(static_cast<LogRole>(role)), "log role", missing);
    }
    ExpectCovered(PreviewButtonText(true), "bug report", missing);
    ExpectCovered(PreviewButtonText(false), "bug report", missing);
    for (const trophies::Grade grade : {trophies::Grade::kPlatinum, trophies::Grade::kGold, trophies::Grade::kSilver,
                                        trophies::Grade::kBronze, trophies::Grade::kUnknown}) {
        ExpectCovered(trophies::GradeName(grade), "trophy grade", missing);
    }
    for (const char* month : {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}) {
        ExpectCovered(month, "month", missing);
    }
    ExpectCovered(InstalledGameName(GameFileState::kMissing, nullptr), "About", missing);
    // The in-game port UI and the start checks outside the launcher's own sources.
    for (const char* english :
         {"Skip Intro: On", "Unlock Bookshelf: On", "Save game", "Load game", "Delete save", "New save", "Text",
          "On", "Off", "Unavailable", "Not saved to the launcher", "F10 Close",
          "This processor lacks MOVBE, which the game's PS4 code needs.",
          "The game needs an Intel Core 2nd generation (Sandy Bridge), AMD FX or newer processor.",
          "Update your graphics driver, or choose another renderer.", "Vulkan is not installed on this system.",
          "Install your graphics driver's Vulkan support, or choose another renderer.",
          "This system's Vulkan runtime supports only 1.0, but Vulkan 1.3 or newer is required.",
          "No Vulkan graphics driver was found.", "Vulkan could not be started (VK_ERROR_INITIALIZATION_FAILED).",
          "No graphics card here supports Vulkan 1.3 with the features the game needs.",
          "Restoring Odin2_RE_US.CPK...", "Patching Odin2_RE_US.CPK...", "Writing Interface.ftx...",
          "Updating Odin2_RE_US.CPK...", "The server answered 404.", "Cannot write C:/x.zip.", "All files",
          "Checking the package...", "Switching to Xbox prompts...", "Restoring PlayStation prompts...",
          "Updating the game files...", "Checking for updates...", "Downloading update...", "Cancelled",
          "Could not change the game files", "Controller prompts are not available for this game version"}) {
        ExpectCovered(english, "in-game and start checks", missing);
    }
    std::printf("  data tables: %d without a row\n", missing);
    CHECK(missing == 0);

    // Labels composed by the launcher keep English exactly when Portuguese (Brazil) is not selected.
    localization::SetActiveLanguage(kPortugueseBrazilKey);
    CHECK(WindowSizeLabel(kGamePictureSize).find("(tamanho original do jogo)") != std::string::npos);
    std::vector<Display> displays(1);
    displays[0].primary = true;
    displays[0].bounds.w = 1920;
    displays[0].bounds.h = 1080;
    CHECK(MonitorLabel(1, displays).starts_with("Tela 1 (principal)   1920"));
    CHECK(MonitorLabel(2, displays) == "Tela 2 (não conectada)");
    localization::SetActiveLanguage(kEnglishKey);
    CHECK(WindowSizeLabel(kGamePictureSize).find("   (the game's own size)") != std::string::npos);
    CHECK(MonitorLabel(1, displays).starts_with("Display 1 (primary)   1920"));
    CHECK(MonitorLabel(2, displays) == "Display 2 (not connected)");
}

// ---------------------------------------------------------------------------------------------------- source scan

// A C/C++ string literal's bytes; false when it is not one the scan understands.
bool DecodeLiteral(std::string_view body, std::string& out) {
    out.clear();
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] != '\\') {
            out.push_back(body[i]);
            continue;
        }
        if (++i >= body.size()) {
            return false;
        }
        const char c = body[i];
        if (c == 'n') out.push_back('\n');
        else if (c == 't') out.push_back('\t');
        else if (c == '\\' || c == '"' || c == '\'') out.push_back(c);
        else if (c == 'x') {
            unsigned value = 0;
            size_t digits = 0;
            while (i + 1 < body.size() && std::isxdigit(static_cast<unsigned char>(body[i + 1])) && digits < 2) {
                value = value * 16 + unsigned(std::stoi(std::string(1, body[++i]), nullptr, 16));
                ++digits;
            }
            if (!digits) return false;
            out.push_back(static_cast<char>(value));
        } else if (c == 'u' && i + 4 < body.size()) {
            const unsigned code = unsigned(std::stoul(std::string(body.substr(i + 1, 4)), nullptr, 16));
            i += 4;
            if (code < 0x80) out.push_back(char(code));
            else if (code < 0x800) {
                out.push_back(char(0xC0 | (code >> 6)));
                out.push_back(char(0x80 | (code & 0x3F)));
            } else {
                out.push_back(char(0xE0 | (code >> 12)));
                out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(char(0x80 | (code & 0x3F)));
            }
        } else {
            return false;
        }
    }
    return true;
}

// The source without comments; string and character literals are kept.
std::string StripComments(const std::string& source) {
    std::string out;
    for (size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '"' || c == '\'') {
            const size_t start = i;
            for (++i; i < source.size() && source[i] != c; ++i) {
                if (source[i] == '\\') ++i;
            }
            out.append(source, start, i - start + 1);
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
            while (i < source.size() && source[i] != '\n') ++i;
            out.push_back('\n');
        } else if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
            i = source.find("*/", i + 2);
            if (i == std::string::npos) break;
            ++i;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// The text between the parenthesis at `open` and its match.
std::string_view Arguments(std::string_view source, size_t open) {
    int depth = 0;
    for (size_t i = open; i < source.size(); ++i) {
        const char c = source[i];
        if (c == '"' || c == '\'') {
            for (++i; i < source.size() && source[i] != c; ++i) {
                if (source[i] == '\\') ++i;
            }
        } else if (c == '(' || c == '{' || c == '[') {
            ++depth;
        } else if (c == ')' || c == '}' || c == ']') {
            if (--depth == 0) return source.substr(open + 1, i - open - 1);
        }
    }
    return {};
}

struct Piece {
    bool literal = false;
    std::string text;
};

// The arguments as literals (adjacent ones joined, as the compiler does) and the code between them.
std::vector<Piece> Pieces(std::string_view args) {
    std::vector<Piece> pieces;
    for (size_t i = 0; i < args.size();) {
        if (args[i] == '"' && (i == 0 || (args[i - 1] != 'R' && args[i - 1] != '\''))) {
            size_t end = i + 1;
            while (end < args.size() && args[end] != '"') {
                if (args[end] == '\\') ++end;
                ++end;
            }
            std::string text;
            if (!DecodeLiteral(args.substr(i + 1, end - i - 1), text)) text = "<undecodable>";
            if (!pieces.empty() && pieces.back().literal) {
                pieces.back().text += text;
            } else {
                pieces.push_back({true, text});
            }
            i = end + 1;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(args[i]))) {
            ++i;
            continue;
        }
        if (pieces.empty() || pieces.back().literal) pieces.push_back({false, ""});
        if (args[i] == '\'') {
            const size_t end = args.find('\'', i + (args[i + 1] == '\\' ? 3 : 2));
            pieces.back().text.append(args.substr(i, end - i + 1));
            i = end + 1;
            continue;
        }
        pieces.back().text.push_back(args[i++]);
    }
    return pieces;
}

// Literals that are setting keys or values, ImGui ids, formats or numbers, never words the player reads.
bool NotText(const std::string& text) {
    if (text.empty() || text.starts_with("##") || text.starts_with("*.")) return true;
    std::string letters;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 1 < text.size()) {
            ++i;
            while (i < text.size() && !std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(text[i]))) letters.push_back(text[i]);
    }
    if (letters.empty()) return true;
    const bool key = std::all_of(text.begin(), text.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_';
    });
    const bool scale = text.size() >= 2 && text.back() == 'x' &&
                       std::all_of(text.begin(), text.end() - 1,
                                   [](char c) { return std::isdigit(static_cast<unsigned char>(c)); });
    return key || scale;
}

// Platform names and the like, drawn as they are in every language.
const std::set<std::string> kSameInEveryLanguage = {"Windows x64", "Linux x86-64 (experimental)", "F10",
                                                    "Odin Sphere Leifthrasir PC", "Odin Sphere Leifthrasir"};

bool IdentifierBefore(std::string_view source, size_t at) {
    return at > 0 && (std::isalnum(static_cast<unsigned char>(source[at - 1])) || source[at - 1] == '_');
}

// The arguments with every TrValue(...) emptied: its composed text is checked as a whole.
std::string WithoutTrValue(std::string_view args) {
    std::string out(args);
    for (size_t at = out.find("TrValue("); at != std::string::npos; at = out.find("TrValue(", at + 1)) {
        const std::string_view inner = Arguments(out, at + 7);
        out.replace(at + 8, inner.size(), "value");
    }
    return out;
}

struct ScanResult {
    std::set<std::string> found;
    std::vector<std::string> missing;
    std::vector<std::string> unwrapped;
};

void Expect(ScanResult& result, const std::string& text, const std::string& where) {
    if (NotText(text) || kSameInEveryLanguage.count(text)) return;
    if (result.found.insert(text).second && !Covered(text)) result.missing.push_back(where + ": " + text);
}

// Calls whose literal arguments are drawn: Tr and TrValue, the TRG rows, the status line and the page helpers.
void ScanCalls(const std::string& name, const std::string& source, ScanResult& result) {
    static const std::vector<std::string> kDrawn = {
        "Tr", "osl_ui_text", "OslUiTextValue", "Row", "RowLeft", "Toggle", "Combo", "Choice", "Info", "Help", "Heading",
        "Paragraph", "ButtonRow", "ConfirmRow", "FolderRow", "StatusCard", "SetStatus", "AccentButton", "Section",
        "SliderInt", "SponsorCard", "KeyBindRow", "AccentText", "WarningText", "FactLabel", "Fact", "SemiboldWidth",
        "ShowNotice", "DrawKeySetup", "phase", "PlayCheck", "QueueStartupNotice", "Fail", "BrowseForFile"};
    for (const std::string& call : kDrawn) {
        const std::string open = call + (call == "PlayCheck" ? "{" : "(");
        for (size_t at = source.find(open); at != std::string::npos; at = source.find(open, at + 1)) {
            if (IdentifierBefore(source, at)) continue;
            const std::string args = WithoutTrValue(Arguments(source, at + open.size() - 1));
            for (const Piece& piece : Pieces(args)) {
                if (piece.literal) Expect(result, piece.text, name + " " + call);
            }
        }
    }
    // TrValue's argument is one composed text: its literals, and %s for each value between them.
    for (size_t at = source.find("TrValue("); at != std::string::npos; at = source.find("TrValue(", at + 1)) {
        if (IdentifierBefore(source, at)) continue;
        std::string composed;
        bool any = false;
        for (const Piece& piece : Pieces(Arguments(source, at + 7))) {
            if (piece.literal) {
                composed += piece.text;
                any = true;
            } else {
                std::string code = piece.text;
                code.erase(std::remove(code.begin(), code.end(), '+'), code.end());
                if (code == "std::string(" || code == ")" || code == ")std::string(") continue;
                if (!code.empty()) composed += "%s";
            }
        }
        if (any) Expect(result, composed, name + " TrValue");
    }
    // Text given straight to ImGui must go through Tr.
    for (const char* call : {"ImGui::Text(", "ImGui::TextUnformatted(", "ImGui::TextDisabled(", "ImGui::TextWrapped(",
                             "ImGui::TextColored(", "ImGui::Button(", "ImGui::Checkbox(", "ImGui::Selectable(",
                             "ImGui::RadioButton(", "ImGui::TextLink(", "ImGui::BeginCombo(", "ImGui::OpenPopup(",
                             "ImGui::BeginPopupModal(", "ImGui::SetItemTooltip(", "ImGui::CalcTextSize("}) {
        for (size_t at = source.find(call); at != std::string::npos; at = source.find(call, at + 1)) {
            const std::string_view args = Arguments(source, at + std::strlen(call) - 1);
            std::string code;
            for (const Piece& piece : Pieces(args)) {
                if (!piece.literal) {
                    code += piece.text;
                    continue;
                }
                const bool wrapped = code.ends_with("Tr(") || code.ends_with("osl_ui_text(") || code.ends_with("?") ||
                                     code.ends_with(":");
                if (!NotText(piece.text) && !wrapped) result.unwrapped.push_back(name + " " + call + " " + piece.text);
                code += "\"\"";
            }
        }
    }
    // The pages' help and title constants.
    for (const char* declaration : {"constexpr char k", "const char* k", "constexpr const char* k"}) {
        for (size_t at = source.find(declaration); at != std::string::npos; at = source.find(declaration, at + 1)) {
            const size_t name_end = source.find_first_of("[ =", at + std::strlen(declaration));
            const size_t name_start = at + std::strlen(declaration) - 1;
            const std::string constant = source.substr(name_start, name_end - name_start);
            const bool drawn = constant.ends_with("Help") || constant.ends_with("Title") ||
                               constant.ends_with("Subtitle") ||
                               constant.ends_with("Headline") || constant.ends_with("Note") ||
                               constant.ends_with("Section") ||
                               constant == "kText";
            if (!drawn) continue;
            const size_t equals = source.find('=', name_end);
            size_t end = equals;
            for (bool quoted = false; end < source.size() && (quoted || source[end] != ';'); ++end) {
                if (source[end] == '\\' && quoted) ++end;
                else if (source[end] == '"') quoted = !quoted;
            }
            std::string text;
            for (const Piece& piece : Pieces(std::string_view(source).substr(equals + 1, end - equals - 1))) {
                if (piece.literal) text += piece.text;
            }
            Expect(result, text, name + " " + constant);
        }
    }
}

void TestSourceScan() {
    const fs::path root(OSL_SOURCE_DIR);
    std::vector<fs::path> files;
    for (const fs::path& folder : {root / "launcher", root / "launcher" / "updater"}) {
        for (const auto& entry : fs::directory_iterator(folder)) {
            const std::string extension = entry.path().extension().string();
            if (entry.is_regular_file() && (extension == ".cpp" || extension == ".h")) files.push_back(entry.path());
        }
    }
    // Log and diagnostics text, and code with no drawn text: they stay English and are not scanned.
    const std::set<std::string> logs_only = {"build_diagnostics.cpp", "build_scan.cpp", "boot_image.cpp",
                                             "pkg_extract.cpp",
                                             "game_source.cpp", "log_scrubber.cpp", "json_lite.cpp", "http_post.cpp",
                                             "crypto_win.cpp", "crypto_openssl.cpp", "helper_main.cpp", "json.cpp",
                                             "install_transaction.cpp", "sha256.cpp", "version.cpp", "update_paths.cpp",
                                             "cpk_patch.cpp", "settings.cpp", "game_profile.cpp", "game_files.cpp",
                                             "title_exit_assets.cpp", "controller_prompts.cpp", "header_art.cpp",
                                             "page_icons.cpp", "host.cpp", "paths.cpp", "trophy_content.cpp",
                                             "game_translation.cpp", "incident_client.cpp", "report_history.cpp",
                                             "pad_capture.cpp", "shift_key.cpp"};
    for (const char* in_game : {"gpu/shim/osl_overlay.cpp", "gpu/shim/window.cpp", "gpu/shim/oslgpu.cpp"}) {
        files.push_back(root / in_game);
    }
    ScanResult result;
    size_t scanned = 0;
    for (const fs::path& file : files) {
        const std::string name = file.filename().string();
        if (logs_only.count(name) || file.parent_path().filename() == "localization") continue;
        ++scanned;
        ScanCalls(name, StripComments(Read(file)), result);
    }
    // TRG's own text, when its patched copy is at hand.
    const fs::path trg(OSL_TRG_SOURCE_DIR);
    std::error_code ec;
    size_t trg_scanned = 0;
    for (const char* file : {"src/launcher.cpp", "src/platform.cpp"}) {
        if (fs::is_regular_file(trg / file, ec)) {
            ++trg_scanned;
            const std::string source = StripComments(Read(trg / file));
            for (const char* call : {"T(", "Translate("}) {
                for (size_t at = source.find(call); at != std::string::npos; at = source.find(call, at + 1)) {
                    if (IdentifierBefore(source, at)) continue;
                    for (const Piece& piece : Pieces(Arguments(source, at + std::strlen(call) - 1))) {
                        if (piece.literal) Expect(result, piece.text, std::string("TRG ") + file);
                    }
                }
            }
        }
    }
    std::printf("  source scan: %zu files (+%zu TRG), %zu UI literals, %zu without a row, %zu drawn without Tr\n",
                scanned, trg_scanned, result.found.size(), result.missing.size(), result.unwrapped.size());
    for (const std::string& line : result.missing) std::printf("  no row: %s\n", line.c_str());
    for (const std::string& line : result.unwrapped) std::printf("  not translated: %s\n", line.c_str());
    CHECK(result.found.size() > 250);

    // The scan itself: a row-less label, a literal drawn without Tr, and composed text read as one row.
    ScanResult probe;
    ScanCalls("probe",
              "ui.Toggle(\"Zzz no row\", nullptr, kKey, false, \"Off\", \"On\");\n"
              "ImGui::Button(\"Raw button\");\nui.SetStatus(TrValue(\"Display \" + n));\n",
              probe);
    CHECK(probe.missing.size() == 1 && probe.unwrapped.size() == 1 && probe.found.count("Display %s") == 1 &&
          probe.found.count("Display ") == 0);
    CHECK(result.missing.empty());
    CHECK(result.unwrapped.empty());

    // Wiring: TRG's translator, the language before the first frame and on every Menu & Subtitles change, and the
    // in-game UI from the same setting at game start.
    const std::string launcher = Read(root / "launcher" / "launcher.cpp");
    const size_t translator = launcher.find("trg::SetTranslator(&localization::TranslateLauncherText);");
    const size_t first_language = launcher.find("UseTextLanguageOf(settings_);", translator);
    const size_t first_frame = launcher.find("outcome = Frame();", first_language);
    CHECK(translator != std::string::npos && first_language != std::string::npos && first_frame != std::string::npos);
    CHECK(launcher.find("    Outcome Frame() {\n        UseTextLanguageOf(settings_);") != std::string::npos);
    CHECK(launcher.find("if (ui.Combo(\"Menu & Subtitles\"") != std::string::npos);
    const size_t run = launcher.find("extern \"C\" OslLauncherResult osl_launcher_run(");
    CHECK(run != std::string::npos &&
          launcher.find("UseTextLanguageOf(settings);", run) < launcher.find("PlayOrExplain(*config)", run));
    const std::string gpu = Read(root / "gpu" / "shim" / "oslgpu.cpp");
    CHECK(gpu.find("osl_ui_text_load_language(config->settings_file);") != std::string::npos);
    // Logs stay English.
    for (const char* file : {"launcher/launcher.cpp", "gpu/shim/osl_overlay.cpp", "launcher/game_install.cpp"}) {
        const std::string source = Read(root / file);
        CHECK(source.find("Log(Tr(") == std::string::npos && source.find("printf(Tr(") == std::string::npos &&
              source.find("printf(osl_ui_text(") == std::string::npos);
    }
}

} // namespace

int main() {
    std::printf("UI text (Portuguese (Brazil)):\n");
    TestTable();
    TestLanguageSwitch();
    TestValues();
    TestSettingsFile();
    TestDataTables();
    TestSourceScan();
    localization::SetActiveLanguage(kEnglishKey);
    std::printf("  %zu rows\n", localization::PortugueseBrazilUiText().rows.size());
    std::printf("%s: %d checks, %d failures\n", failures ? "FAILED" : "OK", checks, failures);
    return failures ? 1 : 0;
}
