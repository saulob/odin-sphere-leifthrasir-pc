/* The port's own text for code outside the launcher (the in-game overlay, the window title): the launcher's
 * Portuguese (Brazil) table, active only when the saved Menu & Subtitles language is Portuguese (Brazil). */
#ifndef OSL_UI_TEXT_H
#define OSL_UI_TEXT_H

#include <stddef.h>

#ifdef __cplusplus
#include <string>
extern "C" {
#endif

/* The active language's text for an English string; the English text itself when there is no row. */
const char *osl_ui_text(const char *english);

/* The same for text composed with values (rows with %s). Writes it to `out` and returns its length. */
size_t osl_ui_text_value(const char *english, char *out, size_t out_size);

/* The language from the settings file's text_language, read now. */
void osl_ui_text_load_language(const char *settings_file);

#ifdef __cplusplus
}

inline std::string OslUiTextValue(const std::string &english) {
    const size_t length = osl_ui_text_value(english.c_str(), nullptr, 0);
    std::string text(length, '\0');
    osl_ui_text_value(english.c_str(), text.data(), length + 1);
    return text;
}
#endif

#endif
