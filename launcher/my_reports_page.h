// The My Reports section of the Bug Report page: the reports sent from this PC, their status and the developer's reply.
// The service is asked about the reports in the list, never about anything else; checks run off the UI thread.
#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "incident_client.h"
#include "report_history.h"
#include "trg/launcher.h"

namespace osl {

class MyReports {
public:
    explicit MyReports(std::filesystem::path history_file);

    void Draw(trg::Ui& ui, const ServiceConfig& service);
    // Asks for the newest statuses unless a check is running or there is nothing to ask about. A failed check keeps the last
    // known statuses.
    void Refresh(const ServiceConfig& service, bool asked_by_player = false);
    // The same, unless a check ran moments ago.
    void RefreshIfStale(const ServiceConfig& service);
    // False when the report could not be added to the saved history; the report itself was sent.
    bool Remember(const IncidentPackage& package, const ReportReceipt& receipt);
    bool HasUpdates();
    // An unseen developer message, which the page shows before a plain status change.
    bool HasNewReply();
    // Opens the report the page notice is about: a reply first, then the most recently updated.
    void ShowUpdate();
    // Applies what finished checks and reads found; called each frame by Draw.
    void Collect();

private:
    // Shared with the thread that asks, which may outlive the launcher window.
    struct Check {
        std::atomic<bool> done{false};
        StatusCheck result;
        std::vector<std::string> asked;
    };
    struct Read {
        std::atomic<bool> done{false};
        std::string ticket;
        SentReportRead result;
    };

    void SaveHistory();
    void Open(const std::string& ticket);
    void Collapse();
    void ForgetSentReport();
    void ReadSentReport(const TrackedReport& report, const ServiceConfig& service);
    void DrawHeader(trg::Ui& ui, const ServiceConfig& service);
    void DrawReports(trg::Ui& ui, const ServiceConfig& service);
    void DrawRow(trg::Ui& ui, const TrackedReport& report, size_t index, std::string& toggled);
    void DrawDetails(trg::Ui& ui, const TrackedReport& report, size_t index, const ServiceConfig& service, std::string& toggled,
                     std::string& removed);
    void DrawActions(trg::Ui& ui, const TrackedReport& report, const ServiceConfig& service, std::string& removed);
    void DrawSentReport(trg::Ui& ui);
    bool ConfirmRemoval(trg::Ui& ui);

    ReportHistory history_;
    std::shared_ptr<Check> check_;
    ReportOutcome check_failure_ = ReportOutcome::kSent;
    bool check_failed_ = false;
    bool asked_by_player_ = false;
    std::optional<std::chrono::steady_clock::time_point> last_check_;
    bool bring_detail_into_view_ = false;
    std::string open_ticket_;
    // The open report whose developer message was unread when it was opened; keeps its "New reply" cue until it closes.
    std::string fresh_reply_;
    std::shared_ptr<Read> reading_;
    std::string sent_ticket_;
    std::string sent_text_;
    bool sent_gone_ = false;
    bool sent_failed_ = false;
};

} // namespace osl
