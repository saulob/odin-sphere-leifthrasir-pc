#include "incident_page.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <utility>

#include "game_profile.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "localization/ui_text.h"
#include "page_margin.h"
#include "report_ui.h"
#include "trg/platform.h"
#include "utf8.h"

namespace osl {
namespace {

constexpr float kDescriptionHeight = 112.0f;
constexpr float kPreviewHeight = 300.0f;
constexpr float kAlertTint = 0.16f, kAlertFlashTint = 0.5f;
constexpr float kAlertFlashSeconds = 0.9f;
constexpr float kConfirmWidth = 440.0f, kConfirmButtonWidth = 150.0f;

const char* kPrivacyHeadline = "Nothing is sent unless you press Send report.";
const char* kPrivacyNote =
    "Before anything is sent, the launcher takes addresses, user and computer names, private folders, passwords and other "
    "private details out of the selected log. Automatic filtering cannot promise to catch everything, so check the preview. "
    "The launcher and the game collect no usage data and keep no player identity: a report gets a random ticket and a "
    "private receipt that stay on this PC. The service sees your connection address briefly to limit abuse and does not "
    "store it with the report.";

int KeepDescriptionShort(ImGuiInputTextCallbackData* data) {
    const size_t keep = BytesOfCharacters(std::string_view(data->Buf, size_t(data->BufTextLen)), kMostDescriptionCharacters);
    if (keep < size_t(data->BufTextLen)) data->DeleteChars(int(keep), data->BufTextLen - int(keep));
    return 0;
}

std::string LogDetails(const GameLog& log) { return LogDateText(log.written) + kMiddleDot + LogSizeText(log.bytes); }

void LogRoleLine(trg::Ui& ui, LogRole role) {
    const char* text = LogRoleText(role);
    if (!text) return;
    ImGui::PushFont(nullptr, trg::layout::kTextHelp);
    AccentText(ui, text);
    ImGui::PopFont();
}

ImVec4 AlertedColor(const ImVec4& color, const ImVec4& alert, double seconds) {
    const float flash = std::max(0.0f, 1.0f - float(seconds) / kAlertFlashSeconds);
    const float t = kAlertTint + (kAlertFlashTint - kAlertTint) * flash;
    return ImVec4(color.x + (alert.x - color.x) * t, color.y + (alert.y - color.y) * t, color.z + (alert.z - color.z) * t, color.w);
}

float SemiboldWidth(trg::Ui& ui, const char* text) {
    ImGui::PushFont(ui.fonts().semibold, trg::layout::kTextLabel);
    const float width = ImGui::CalcTextSize(Tr(text)).x;
    ImGui::PopFont();
    return width;
}

void FactLabel(trg::Ui& ui, const char* text) {
    ImGui::PushFont(ui.fonts().semibold, trg::layout::kTextLabel);
    ImGui::TextUnformatted(Tr(text));
    ImGui::PopFont();
}

void FactValue(trg::Ui& ui, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ui.theme().dim);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void DrawFactLine(trg::Ui& ui, float left, float right, float y) {
    const float top = std::floor(y);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(right, top + 1.0f), ImGui::GetColorU32(ui.theme().border_row));
}

// The launcher's two-pair grid (Ui::InfoGrid) with the button in its empty second-row cell, flush right like the page's other
// buttons. True when it was pressed.
bool DrawLogFacts(trg::Ui& ui, const GameLog& log) {
    const float scale = ui.scale();
    const float padding = trg::layout::kInfoGridPadding * scale;
    const std::string written = LogDateText(log.written);
    const std::string size = LogSizeText(log.bytes);
    const float left = ImGui::GetCursorScreenPos().x;
    const float room = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, padding));
    if (!ImGui::BeginTable("##selected_log", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoClip)) {
        ImGui::PopStyleVar();
        return false;
    }
    const float weights[4] = {std::max(SemiboldWidth(ui, "Selected log"), SemiboldWidth(ui, "Size")),
                              std::max(ImGui::CalcTextSize(log.name.c_str()).x, ImGui::CalcTextSize(size.c_str()).x),
                              SemiboldWidth(ui, "Last written"), ImGui::CalcTextSize(written.c_str()).x};
    for (const float weight : weights) ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthStretch, std::max(weight, 1.0f));
    ImGui::TableNextColumn();
    FactLabel(ui, "Selected log");
    ImGui::TableNextColumn();
    FactValue(ui, log.name);
    ImGui::TableNextColumn();
    FactLabel(ui, "Last written");
    ImGui::TableNextColumn();
    FactValue(ui, written);

    ImGui::TableNextColumn();
    const float second_row = ImGui::GetCursorScreenPos().y - padding;
    ImGui::AlignTextToFramePadding();
    FactLabel(ui, "Size");
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    FactValue(ui, size);
    ImGui::TableNextColumn();
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float width = std::min(trg::layout::ControlColumnWidth(room, scale), left + room - cell.x);
    ImGui::SetCursorScreenPos(ImVec2(left + room - width, cell.y));
    const bool pressed = ImGui::Button(Tr("View selected log"), ImVec2(width, 0));
    ImGui::EndTable();
    ImGui::PopStyleVar();
    DrawFactLine(ui, left, left + room, second_row);
    DrawFactLine(ui, left, left + room, ImGui::GetItemRectMax().y);
    return pressed;
}

std::string EnvironmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

} // namespace

IncidentPage::IncidentPage(Places places)
    : places_(std::move(places)), my_reports_(places_.user_dir / kReportHistoryFile) {
    service_.endpoint = ResolveEndpoint(kBugReportServiceUrl, EnvironmentValue(kBugReportUrlVariable));
    Log(service_.endpoint.configured() ? "Bug report: service " + service_.endpoint.url : "Bug report: no service configured");
}

void IncidentPage::Draw(trg::Ui& ui, const ReportFacts& facts) {
    if (!logs_found_) FindLogs();
    CollectDelivery();
    my_reports_.RefreshIfStale(service_);
    const bool sending = delivery_ != nullptr;
    if (CheckDraft(description_, !logs_.empty()) != problem_) problem_ = DraftProblem::kNone;
    KeepClearOfScrollbar(std::floor(trg::layout::kContentPadding * ui.scale()));
    AccentText(ui, kPrivacyHeadline);
    ui.Paragraph(kPrivacyNote);
    if (!service_.endpoint.configured()) {
        WarningText(ui, service_.endpoint.problem.c_str());
        ui.Help("You can still save the report on this PC and attach it to a GitHub issue yourself.");
    }
    if (my_reports_.HasUpdates()) {
        const bool reply = my_reports_.HasNewReply();
        if (ui.ButtonRow(reply ? "A report has a new reply" : "A report has an update",
                         reply ? "The developer replied to one of your reports." : "Its status changed since you last looked.", "Show")) {
            my_reports_.ShowUpdate();
        }
    }
    ui.Heading("1. Report the problem");
    DrawDescription(ui, sending);
    ui.Heading("2. Choose one game log");
    DrawLogs(ui, sending);
    ui.Heading("3. Preview what will be sent");
    DrawPreview(ui, facts, sending);
    ui.Heading("4. Send");
    DrawSend(ui, facts, sending);
    ui.EndRows();
    my_reports_.Draw(ui, service_);
    ui.EndRows();
}

void IncidentPage::FindLogs() {
    logs_ = FindGameLogs(places_.logs_dir, places_.running_log);
    logs_found_ = true;
    selected_ = DefaultGameLog(logs_);
}

void IncidentPage::ForgetPreview() {
    previewed_.reset();
    preview_text_.clear();
    log_failed_ = false;
}

bool IncidentPage::AcceptDraft() {
    problem_ = CheckDraft(description_, !logs_.empty());
    problem_time_ = ImGui::GetTime();
    bring_problem_into_view_ = problem_ != DraftProblem::kNone;
    return problem_ == DraftProblem::kNone;
}

void IncidentPage::CollectDelivery() {
    if (!delivery_ || !delivery_->done) return;
    result_ = delivery_->receipt;
    if (result_->outcome == ReportOutcome::kSent) {
        Log("Bug report: sent as " + result_->ticket);
        sent_report_remembered_ = my_reports_.Remember(*delivery_->package, *result_);
        description_[0] = '\0';
        ForgetPreview();
    } else {
        Log(std::string("Bug report: not sent (") + OutcomeTitle(result_->outcome) + ")");
    }
    delivery_.reset();
    bring_result_into_view_ = true;
}

std::shared_ptr<const IncidentPackage> IncidentPage::BuildPackage(const ReportFacts& facts) {
    log_failed_ = false;
    const PrivateFacts private_facts = GatherPrivateFacts(places_.install_folders);
    std::optional<IncidentPackage> package = BuildIncidentPackage(description_, logs_[selected_], facts, private_facts);
    if (!package) {
        log_failed_ = true;
        bring_problem_into_view_ = true;
        FindLogs();
        return nullptr;
    }
    return std::make_shared<const IncidentPackage>(std::move(*package));
}

void IncidentPage::TogglePreview(const ReportFacts& facts) {
    if (previewed_) {
        ForgetPreview();
        return;
    }
    if (!AcceptDraft()) return;
    previewed_ = BuildPackage(facts);
    if (previewed_) preview_text_ = IncidentPreviewText(*previewed_);
}

// Builds the report, shows it in the preview and asks for confirmation; the player sees exactly what a confirmed send holds.
void IncidentPage::AskToSend(const ReportFacts& facts) {
    if (!AcceptDraft()) return;
    if (!previewed_) {
        previewed_ = BuildPackage(facts);
        if (previewed_) preview_text_ = IncidentPreviewText(*previewed_);
    }
    confirm_requested_ = previewed_ != nullptr;
}

void IncidentPage::StartDelivery() {
    if (!previewed_ || delivery_ || !service_.endpoint.configured()) return;
    result_.reset();
    delivery_ = std::make_shared<Delivery>();
    delivery_->package = previewed_;
    bring_result_into_view_ = true;
    std::thread([delivery = delivery_, service = service_] {
        delivery->receipt = FileReport(service, *delivery->package);
        delivery->done = true;
    }).detach();
}

void IncidentPage::Save(const ReportFacts& facts) {
    if (!AcceptDraft()) return;
    std::shared_ptr<const IncidentPackage> package = previewed_ ? previewed_ : BuildPackage(facts);
    if (!package) return;
    saved_report_ = SaveIncidentReport(*package, places_.logs_dir / "reports");
    save_failed_ = saved_report_.empty();
    bring_result_into_view_ = true;
    if (!save_failed_) {
        description_[0] = '\0';
        ForgetPreview();
    }
}

void IncidentPage::DrawDescription(trg::Ui& ui, bool sending) {
    ui.EndRows();
    const bool too_short = problem_ == DraftProblem::kDescriptionTooShort;
    if (too_short && std::exchange(bring_problem_into_view_, false)) ImGui::SetKeyboardFocusHere();
    const ImVec4 frame = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
    ImGui::BeginDisabled(sending);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, too_short ? AlertedColor(frame, ui.theme().warn_dot, ImGui::GetTime() - problem_time_) : frame);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_CallbackEdit;
    if (ImGui::InputTextMultiline("##description", description_, sizeof(description_), ImVec2(-FLT_MIN, kDescriptionHeight * ui.scale()),
                                  flags, KeepDescriptionShort)) {
        ForgetPreview();
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    const size_t used = CountCharacters(description_);
    const std::string counter = std::to_string(used) + " / " + std::to_string(kMostDescriptionCharacters);
    ImGui::PushFont(nullptr, trg::layout::kTextHelp);
    ImGui::TextColored(used >= kMostDescriptionCharacters ? ui.theme().warn_dot : ui.theme().dim, "%s", counter.c_str());
    if (too_short) {
        ImGui::SameLine();
        ImGui::TextColored(ui.theme().warn_dot, "%s", Tr("Please describe the problem, minimum 10 characters"));
    }
    ImGui::PopFont();
    ui.Help("What were you doing, what went wrong, and does it happen every time?");
}

void IncidentPage::DrawLogs(trg::Ui& ui, bool sending) {
    if (logs_.empty()) {
        ui.Paragraph("No game log is available yet, so a report cannot be made. Start the game once and try again. With "
                     "Detailed logging off on the About page, only sessions that crashed keep a log.");
        if (problem_ == DraftProblem::kNoLog) {
            if (std::exchange(bring_problem_into_view_, false)) ImGui::SetScrollHereY();
            WarningText(ui, "There is no game log to report.");
        }
        return;
    }
    ui.Help("The newest logs of the game, most recent first. Only the one you choose goes into the report.");
    for (size_t i = 0; i < logs_.size(); ++i) {
        const std::string details = LogDetails(logs_[i]);
        ui.RowLeft(logs_[i].name.c_str(), details.c_str());
        LogRoleLine(ui, logs_[i].role);
        ui.RowControl();
        ImGui::PushID(int(i));
        ImGui::BeginDisabled(sending);
        ImGui::AlignTextToFramePadding();
        if (ImGui::RadioButton(Tr("Use this log"), selected_ == i) && selected_ != i) {
            selected_ = i;
            ForgetPreview();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ui.EndRows();
    if (log_failed_) {
        if (std::exchange(bring_problem_into_view_, false)) ImGui::SetScrollHereY();
        WarningText(ui, "That log could not be read. Choose another one and try again.");
    }
    const GameLog& log = logs_[selected_];
    if (DrawLogFacts(ui, log)) trg::OpenUrl(Utf8(log.path));
}

void IncidentPage::DrawPreview(trg::Ui& ui, const ReportFacts& facts, bool sending) {
    ImGui::BeginDisabled(sending);
    const bool clicked = ui.ButtonRow("Preview", "Shows exactly what will be sent, after the privacy filter.", PreviewButtonText(previewed_ != nullptr));
    ImGui::EndDisabled();
    if (clicked) TogglePreview(facts);
    ui.EndRows();
    if (previewed_) ReadOnlyBox("##preview", preview_text_, kPreviewHeight * ui.scale());
}

void IncidentPage::DrawSend(trg::Ui& ui, const ReportFacts& facts, bool sending) {
    ImGui::BeginDisabled(sending || !service_.endpoint.configured());
    const bool send = ui.ButtonRow("Send report", "Sends what the preview shows to the developer, after you confirm.",
                                   sending ? "Sending..." : "Send report", true);
    ImGui::EndDisabled();
    if (send) AskToSend(facts);
    if (ui.ButtonRow("Save a copy", "Writes the same filtered report to a text file in the logs folder instead of sending it.", "Save report")) {
        Save(facts);
    }
    if (ui.ButtonRow("Logs folder", "The game logs, and the copies saved here.", "Open logs folder")) {
        std::error_code ec;
        std::filesystem::create_directories(places_.logs_dir, ec);
        trg::OpenInFileManager(Utf8(places_.logs_dir));
    }
    if (ui.ButtonRow("Report on GitHub", "Opens the issue tracker in your browser. Attach a saved copy yourself.", "Open GitHub issues")) {
        trg::OpenUrl(kIssuesUrl);
    }
    ui.EndRows();
    DrawConfirmation(ui);
    DrawResult(ui);
    if (std::exchange(bring_result_into_view_, false)) ImGui::SetScrollHereY(1.0f);
}

void IncidentPage::DrawConfirmation(trg::Ui& ui) {
    const float scale = ui.scale();
    if (std::exchange(confirm_requested_, false)) ImGui::OpenPopup("##confirm_send");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale, 20 * scale));
    const bool open = ImGui::BeginPopupModal("##confirm_send", nullptr,
                                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!open) return;
    ImGui::PushFont(ui.fonts().semibold, 0.0f);
    ImGui::TextUnformatted(Tr("Send this report to the developer?"));
    ImGui::PopFont();
    ImGui::PushTextWrapPos(kConfirmWidth * scale);
    ImGui::TextUnformatted(Tr("The preview shows exactly what is sent: your description, your system details and the selected "
                              "log after the privacy filter. Nothing else leaves this PC."));
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(kConfirmWidth * scale, 6 * scale));
    if (ui.AccentButton("Send report", ImVec2(kConfirmButtonWidth * scale, 0))) {
        ImGui::CloseCurrentPopup();
        StartDelivery();
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("Cancel"), ImVec2(kConfirmButtonWidth * scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void IncidentPage::DrawResult(trg::Ui& ui) {
    if (delivery_) {
        ui.Spacer(4);
        ui.StatusCard(trg::Status::kBusy, "Sending your report...", "This takes a few seconds.");
        return;
    }
    if (result_) {
        ui.Spacer(4);
        if (result_->outcome == ReportOutcome::kSent) {
            const std::string id = std::string(Tr("Report ID:")) + " " + result_->ticket;
            ui.StatusCard(trg::Status::kReady, "Report sent successfully", id.c_str());
            ui.Spacer(4);
            if (ImGui::Button(Tr("Copy report ID"))) {
                ImGui::SetClipboardText(result_->ticket.c_str());
                ui.SetStatus("Report ID copied.");
            }
            if (!sent_report_remembered_) ui.Help("The report was sent, but it could not be added to My Reports on this PC.");
        } else {
            ui.StatusCard(trg::Status::kAttention, OutcomeTitle(result_->outcome), OutcomeAdvice(result_->outcome));
        }
    }
    if (save_failed_) {
        ui.Spacer(4);
        WarningText(ui, "The report could not be saved.");
    } else if (!saved_report_.empty()) {
        ui.Spacer(4);
        const std::string name = Utf8(saved_report_.filename());
        ui.StatusCard(trg::Status::kReady, "Report saved", name.c_str());
    }
}

} // namespace osl
