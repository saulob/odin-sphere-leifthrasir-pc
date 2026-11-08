#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "incident_client.h"
#include "incident_report.h"
#include "my_reports_page.h"
#include "trg/launcher.h"

namespace osl {

// The launcher's Bug Report page: report the problem, choose one game log, preview what will be sent, and send it once the
// player confirms. Nothing is sent before that. A connection is opened to ask about reports already sent, and only when
// there are some.
class IncidentPage {
public:
    struct Places {
        std::filesystem::path logs_dir;
        std::filesystem::path running_log;
        std::filesystem::path user_dir;
        std::vector<std::filesystem::path> install_folders;
        std::function<void(const std::string&)> log; // called on the UI thread only
    };

    explicit IncidentPage(Places places);

    void Draw(trg::Ui& ui, const ReportFacts& facts);
    // Asks for the statuses of the reports in My Reports; nothing is asked when there are none.
    void CheckReports() { my_reports_.Refresh(service_); }
    bool HasReportUpdates() { return my_reports_.HasUpdates(); }

private:
    // Shared with the sending thread, which may outlive the launcher window.
    struct Delivery {
        std::atomic<bool> done{false};
        ReportReceipt receipt;
        std::shared_ptr<const IncidentPackage> package;
    };

    void FindLogs();
    void ForgetPreview();
    bool AcceptDraft();
    void CollectDelivery();
    std::shared_ptr<const IncidentPackage> BuildPackage(const ReportFacts& facts);
    void TogglePreview(const ReportFacts& facts);
    void AskToSend(const ReportFacts& facts);
    void StartDelivery();
    void Save(const ReportFacts& facts);
    void DrawDescription(trg::Ui& ui, bool sending);
    void DrawLogs(trg::Ui& ui, bool sending);
    void DrawPreview(trg::Ui& ui, const ReportFacts& facts, bool sending);
    void DrawSend(trg::Ui& ui, const ReportFacts& facts, bool sending);
    void DrawConfirmation(trg::Ui& ui);
    void DrawResult(trg::Ui& ui);

    void Log(const std::string& line) const {
        if (places_.log) places_.log(line);
    }

    Places places_;
    ServiceConfig service_;
    char description_[kMostDescriptionCharacters * 4 + 1] = {};
    std::vector<GameLog> logs_;
    bool logs_found_ = false;
    size_t selected_ = 0;
    DraftProblem problem_ = DraftProblem::kNone;
    double problem_time_ = 0.0;
    bool bring_problem_into_view_ = false;
    std::shared_ptr<const IncidentPackage> previewed_;
    std::string preview_text_;
    bool log_failed_ = false;
    bool confirm_requested_ = false;
    std::shared_ptr<Delivery> delivery_;
    std::optional<ReportReceipt> result_;
    bool sent_report_remembered_ = true;
    std::filesystem::path saved_report_;
    bool save_failed_ = false;
    bool bring_result_into_view_ = false;
    MyReports my_reports_;
};

} // namespace osl
