// The Bug Report page's calls to the report service (server/odin-bug-reports). Each one opens its own short HTTPS
// request, when the player sends a report or asks about one, and nothing else ever does. They block: run them off the UI
// thread.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "http_post.h"
#include "incident_report.h"
#include "report_history.h"

namespace osl {

// Overrides kBugReportServiceUrl (game_profile.h) for one run.
inline constexpr char kBugReportUrlVariable[] = "OSL_BUG_REPORT_URL";

struct ServiceEndpoint {
    std::string url;     // scheme://host[:port], no trailing slash; empty when there is no usable service
    std::string problem; // English, for Tr; why `url` is empty
    bool configured() const { return !url.empty(); }
};

// `override_url` (the environment) wins over `built_in` when it is not empty.
ServiceEndpoint ResolveEndpoint(std::string_view built_in, std::string_view override_url);

struct ServiceConfig {
    ServiceEndpoint endpoint;
    PostJson post = PostJsonHttps;
    HttpLimits limits;
    std::function<void(const std::string&)> log; // never receives a receipt or any report text
};

enum class ReportOutcome { kSent, kRecentLimit, kDailyLimit, kUnreachable, kTimedOut, kCertificate, kRefused, kServiceError, kNotConfigured, kClientMissing };

// What a failed transfer or an answer code means for the page.
ReportOutcome OutcomeOfFailure(HttpFailure failure);
ReportOutcome OutcomeOfAnswer(int status, std::string_view error_code);
// What the page tells the player about a request that did not go through. English, for Tr; nullptr for kSent.
const char* OutcomeTitle(ReportOutcome outcome);
const char* OutcomeAdvice(ReportOutcome outcome);

struct ReportReceipt {
    ReportOutcome outcome = ReportOutcome::kServiceError;
    std::string ticket;
    // Proves ownership of this one report. Never logged.
    std::string credential;
    int64_t submitted_at = 0;
};

// Sends the report and closes. Nothing is sent, and no connection opened, without a configured service.
ReportReceipt FileReport(const ServiceConfig& config, const IncidentPackage& package);

struct StatusCheck {
    bool reached = false;
    ReportOutcome failure = ReportOutcome::kSent; // what stopped it, when `reached` is false
    std::vector<ReportStatusUpdate> statuses;
};
// The receipts are the only proof sent; at most kMostTrackedReports are asked about.
StatusCheck FetchReportStatuses(const ServiceConfig& config, const std::vector<TrackedReport>& reports);

struct SentReportRead {
    bool reached = false;
    ReportOutcome failure = ReportOutcome::kSent;
    std::optional<IncidentPackage> package; // empty when the service no longer has the report
};
SentReportRead FetchSentReport(const ServiceConfig& config, const TrackedReport& report);

// What My Reports keeps of a sent report.
TrackedReport TrackedReportOf(const IncidentPackage& package, const ReportReceipt& receipt);

} // namespace osl
