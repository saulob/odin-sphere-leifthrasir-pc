#include "incident_client.h"

#include <algorithm>

#include "json_lite.h"

namespace osl {
namespace {

constexpr char kReportsPath[] = "/v1/reports";
constexpr char kStatusPath[] = "/v1/reports/status";
constexpr char kReadPath[] = "/v1/reports/read";

void Say(const ServiceConfig& config, const std::string& line) {
    if (config.log) config.log(line);
}

std::string TrimmedAscii(std::string_view text) {
    const auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!text.empty() && blank(text.front())) text.remove_prefix(1);
    while (!text.empty() && blank(text.back())) text.remove_suffix(1);
    return std::string(text);
}

std::string ClaimJson(const TrackedReport& report) {
    return "{\"ticket\":" + json::Quote(report.ticket) + ",\"receipt\":" + json::Quote(report.receipt) + "}";
}

struct Answer {
    ReportOutcome failure = ReportOutcome::kSent;
    int status = 0;
    std::optional<json::Value> body;
    std::string error_code;
};

// A reply is trusted only when the transfer finished and the body is JSON.
Answer Ask(const ServiceConfig& config, const char* path, const std::string& body, const char* what) {
    Answer answer;
    if (!config.endpoint.configured()) {
        answer.failure = ReportOutcome::kNotConfigured;
        return answer;
    }
    const HttpReply reply = config.post(config.endpoint.url + path, body, config.limits);
    if (reply.failure != HttpFailure::kNone) {
        answer.failure = OutcomeOfFailure(reply.failure);
        Say(config, std::string("Bug report: ") + what + ": no answer (" + std::to_string(int(reply.failure)) + ")");
        return answer;
    }
    answer.status = reply.status;
    answer.body = json::Parse(reply.body);
    if (answer.body) answer.error_code = answer.body->StringOr("error");
    if (reply.status < 200 || reply.status >= 300) {
        answer.failure = OutcomeOfAnswer(reply.status, answer.error_code);
        Say(config, std::string("Bug report: ") + what + ": the service answered " + std::to_string(reply.status));
    } else if (!answer.body || !answer.body->IsObject()) {
        answer.failure = ReportOutcome::kServiceError;
        Say(config, std::string("Bug report: ") + what + ": the answer could not be read");
    }
    return answer;
}

bool Ok(const Answer& answer) { return answer.failure == ReportOutcome::kSent; }

} // namespace

ServiceEndpoint ResolveEndpoint(std::string_view built_in, std::string_view override_url) {
    ServiceEndpoint endpoint;
    const std::string requested = TrimmedAscii(override_url);
    const std::string chosen = requested.empty() ? TrimmedAscii(built_in) : requested;
    ServiceAddress address;
    if (chosen.empty() || !ParseServiceAddress(chosen, address) || (address.path != "/" && !address.path.empty())) {
        endpoint.problem = "The bug report service address is not valid.";
        return endpoint;
    }
    endpoint.url = "https://" + address.host + (address.port == 443 ? "" : ":" + std::to_string(address.port));
    return endpoint;
}

ReportOutcome OutcomeOfFailure(HttpFailure failure) {
    switch (failure) {
    case HttpFailure::kNone: return ReportOutcome::kSent;
    case HttpFailure::kTimedOut: return ReportOutcome::kTimedOut;
    case HttpFailure::kCertificate: return ReportOutcome::kCertificate;
    case HttpFailure::kInvalidAddress: return ReportOutcome::kNotConfigured;
    case HttpFailure::kUnreachable: break;
    }
    return ReportOutcome::kUnreachable;
}

ReportOutcome OutcomeOfAnswer(int status, std::string_view error_code) {
    if (status >= 200 && status < 300) return ReportOutcome::kSent;
    if (error_code == "hourly_limit" || error_code == "limit") return ReportOutcome::kRecentLimit;
    if (error_code == "daily_limit") return ReportOutcome::kDailyLimit;
    if (status == 400 || status == 403 || status == 413 || status == 415) return ReportOutcome::kRefused;
    if (status == 429) return ReportOutcome::kRecentLimit;
    return ReportOutcome::kServiceError;
}

const char* OutcomeTitle(ReportOutcome outcome) {
    switch (outcome) {
    case ReportOutcome::kSent: break;
    case ReportOutcome::kRecentLimit: return "Too many reports were sent recently";
    case ReportOutcome::kDailyLimit: return "Daily report limit reached";
    case ReportOutcome::kUnreachable: return "Cannot reach the report service";
    case ReportOutcome::kTimedOut: return "The report service did not answer in time";
    case ReportOutcome::kCertificate: return "The report service's security certificate was not accepted";
    case ReportOutcome::kRefused: return "The report was not accepted";
    case ReportOutcome::kServiceError: return "The report service could not take the report";
    case ReportOutcome::kNotConfigured: return "The report service address is not valid";
    }
    return nullptr;
}

const char* OutcomeAdvice(ReportOutcome outcome) {
    switch (outcome) {
    case ReportOutcome::kSent: break;
    case ReportOutcome::kRecentLimit:
    case ReportOutcome::kDailyLimit: return "Please try again later";
    case ReportOutcome::kUnreachable:
        return "Check your connection and press Send report again; your text is kept. A firewall or security program may be "
               "blocking outgoing HTTPS (port 443).";
    case ReportOutcome::kTimedOut: return "Check your connection and press Send report again; your text is kept.";
    case ReportOutcome::kCertificate:
        return "Nothing was sent. The clock of this PC, a proxy or a security program may be interfering with secure connections.";
    case ReportOutcome::kRefused: return "Change the description or choose another log, then send it again.";
    case ReportOutcome::kServiceError: return "This is not your fault. Try again in a few minutes; your text is kept.";
    case ReportOutcome::kNotConfigured: return "Nothing was sent. Check OSL_BUG_REPORT_URL, or save the report on this PC.";
    }
    return nullptr;
}

ReportReceipt FileReport(const ServiceConfig& config, const IncidentPackage& package) {
    ReportReceipt receipt;
    const Answer answer = Ask(config, kReportsPath, IncidentRequestJson(package), "send");
    if (!Ok(answer)) {
        receipt.outcome = answer.failure;
        return receipt;
    }
    const std::string ticket = answer.body->StringOr("ticket");
    const std::string credential = answer.body->StringOr("receipt");
    if (!IsTicket(ticket) || !IsReceipt(credential)) {
        receipt.outcome = ReportOutcome::kServiceError;
        return receipt;
    }
    Say(config, "Bug report: sent as " + ticket);
    receipt.outcome = ReportOutcome::kSent;
    receipt.ticket = ticket;
    receipt.credential = credential;
    receipt.submitted_at = answer.body->NumberOr("submitted_at");
    return receipt;
}

StatusCheck FetchReportStatuses(const ServiceConfig& config, const std::vector<TrackedReport>& reports) {
    StatusCheck check;
    std::string claims;
    for (size_t i = 0; i < std::min(reports.size(), kMostTrackedReports); ++i) claims += (claims.empty() ? "" : ",") + ClaimJson(reports[i]);
    if (claims.empty()) return check;
    const Answer answer = Ask(config, kStatusPath, "{\"reports\":[" + claims + "]}", "status");
    if (!Ok(answer)) {
        check.failure = answer.failure;
        return check;
    }
    const json::Value* list = answer.body->Find("reports");
    if (!list || !list->IsArray()) {
        check.failure = ReportOutcome::kServiceError;
        return check;
    }
    check.reached = true;
    for (const json::Value& entry : list->array) {
        if (!entry.IsObject() || !IsTicket(entry.StringOr("ticket"))) continue;
        check.statuses.push_back({entry.StringOr("ticket"), entry.StringOr("status"), entry.NumberOr("status_updated_at"),
                                  entry.StringOr("fixed_version"), entry.StringOr("player_message")});
    }
    return check;
}

SentReportRead FetchSentReport(const ServiceConfig& config, const TrackedReport& report) {
    SentReportRead read;
    const Answer answer = Ask(config, kReadPath, ClaimJson(report), "read");
    if (answer.status == 404) {
        read.reached = true;
        return read;
    }
    if (!Ok(answer)) {
        read.failure = answer.failure;
        return read;
    }
    const json::Value* stored = answer.body->Find("report");
    if (!stored || !stored->IsObject()) {
        read.failure = ReportOutcome::kServiceError;
        return read;
    }
    read.reached = true;
    read.package = SentReportOf(*stored);
    return read;
}

TrackedReport TrackedReportOf(const IncidentPackage& package, const ReportReceipt& receipt) {
    TrackedReport report;
    report.ticket = receipt.ticket;
    report.receipt = receipt.credential;
    report.submitted_at = receipt.submitted_at;
    report.status_updated_at = receipt.submitted_at;
    report.description = package.Value("description");
    report.log_file = package.Value("log_file");
    report.launcher_version = package.Value("launcher_version");
    report.game_build = package.Value("game_build");
    report.game_version = package.Value("game_version");
    report.region = package.Value("region");
    report.operating_system = package.Value("operating_system");
    report.renderer = package.Value("renderer");
    report.gpu = package.Value("gpu");
    return report;
}

} // namespace osl
