// Small drawing helpers of the Bug Report page and its My Reports section.
#pragma once

#include <cfloat>
#include <ctime>
#include <string>

#include "imgui.h"
#include "incident_report.h"
#include "localization/ui_text.h"
#include "trg/launcher.h"

namespace osl {

inline constexpr char kMiddleDot[] = " \xC2\xB7 ";

inline void AccentText(trg::Ui& ui, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().accent);
    ImGui::TextUnformatted(Tr(text));
    ImGui::PopStyleColor();
}

inline void WarningText(trg::Ui& ui, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().warn_dot);
    ImGui::TextWrapped("%s", Tr(text));
    ImGui::PopStyleColor();
}

inline void HelpLine(const ImVec4& color, const std::string& text, bool wrap = false) {
    ImGui::PushFont(nullptr, trg::layout::kTextHelp);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    if (wrap) ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    if (wrap) ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// Text that cannot change: ImGui never writes to a read-only buffer.
inline void ReadOnlyBox(const char* id, std::string& text, float height) {
    ImGui::InputTextMultiline(id, text.data(), text.size() + 1, ImVec2(-FLT_MIN, height),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
}

// `milliseconds` since the epoch, as the service counts them.
inline std::string DateText(int64_t milliseconds) { return LogDateText(std::time_t(milliseconds / 1000)); }

} // namespace osl
