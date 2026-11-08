#include "report_history.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include "incident_report.h"
#include "json_lite.h"
#include "localization/ui_text.h"

namespace osl {
namespace fs = std::filesystem;
namespace {

constexpr int kHistoryVersion = 1;
constexpr size_t kPreviewCharacters = 60;

constexpr std::string_view kStatuses[][2] = {
    {"new", "Open"},
    {"investigating", "Investigating"},
    {"fixed", "Fixed"},
    {"cannot_reproduce", "Cannot reproduce"},
    {"duplicate", "Duplicate"},
    {"not_a_bug", "Not a bug"},
    {"closed", "Closed"},
};

void AppendField(std::string& out, const char* key, const std::string& value) {
    out += std::string("\"") + key + "\":" + json::Quote(value) + ",";
}

std::string EntryJson(const TrackedReport& report) {
    std::string out = "{";
    AppendField(out, "ticket", report.ticket);
    AppendField(out, "receipt", report.receipt);
    out += "\"submitted_at\":" + std::to_string(report.submitted_at) + ",";
    AppendField(out, "description", report.description);
    AppendField(out, "log_file", report.log_file);
    AppendField(out, "launcher_version", report.launcher_version);
    AppendField(out, "game_build", report.game_build);
    AppendField(out, "game_version", report.game_version);
    AppendField(out, "region", report.region);
    AppendField(out, "operating_system", report.operating_system);
    AppendField(out, "renderer", report.renderer);
    AppendField(out, "gpu", report.gpu);
    AppendField(out, "status", report.status);
    out += "\"status_updated_at\":" + std::to_string(report.status_updated_at) + ",";
    AppendField(out, "fixed_version", report.fixed_version);
    AppendField(out, "player_message", report.player_message);
    out += std::string("\"unseen\":") + (report.unseen ? "true" : "false") + ",";
    out += std::string("\"message_unseen\":") + (report.message_unseen ? "true" : "false") + ",";
    out += std::string("\"unavailable\":") + (report.unavailable ? "true" : "false") + "}";
    return out;
}

TrackedReport EntryOf(const json::Value& entry) {
    TrackedReport report;
    report.ticket = entry.StringOr("ticket");
    report.receipt = entry.StringOr("receipt");
    report.submitted_at = entry.NumberOr("submitted_at");
    report.description = entry.StringOr("description");
    report.log_file = entry.StringOr("log_file");
    report.launcher_version = entry.StringOr("launcher_version");
    report.game_build = entry.StringOr("game_build");
    report.game_version = entry.StringOr("game_version");
    report.region = entry.StringOr("region");
    report.operating_system = entry.StringOr("operating_system");
    report.renderer = entry.StringOr("renderer");
    report.gpu = entry.StringOr("gpu");
    report.status = entry.StringOr("status");
    report.status_updated_at = entry.NumberOr("status_updated_at");
    report.fixed_version = entry.StringOr("fixed_version");
    report.player_message = entry.StringOr("player_message");
    report.unseen = entry.BoolOr("unseen");
    report.message_unseen = entry.BoolOr("message_unseen") && !report.player_message.empty();
    report.unavailable = entry.BoolOr("unavailable");
    return report;
}

void NewestFirst(std::vector<TrackedReport>& reports) {
    std::stable_sort(reports.begin(), reports.end(),
                     [](const TrackedReport& a, const TrackedReport& b) { return a.submitted_at > b.submitted_at; });
    if (reports.size() > kMostTrackedReports) reports.resize(kMostTrackedReports);
}

void SetAside(const fs::path& file) {
    fs::path damaged = file;
    damaged += ".damaged";
    std::error_code ec;
    fs::rename(file, damaged, ec);
}

} // namespace

const char* ReportStatusText(std::string_view status) {
    const auto found = std::find_if(std::begin(kStatuses), std::end(kStatuses), [&](const auto& pair) { return pair[0] == status; });
    return found == std::end(kStatuses) ? nullptr : (*found)[1].data();
}

std::string ReportStatusLine(const TrackedReport& report) {
    if (report.status == "fixed" && !report.fixed_version.empty()) return std::string(Tr("Fixed in version")) + " " + report.fixed_version;
    const char* text = ReportStatusText(report.status);
    return text ? Tr(text) : report.status;
}

std::string ReportPreview(std::string_view description) {
    std::string line(description.substr(0, BytesOfCharacters(description, kPreviewCharacters)));
    std::replace(line.begin(), line.end(), '\n', ' ');
    return line.size() < description.size() ? line + "..." : line;
}

std::string ReportHistory::Dump() const {
    std::string list;
    for (const TrackedReport& report : reports_) list += (list.empty() ? "" : ",") + EntryJson(report);
    return "{\"version\":" + std::to_string(kHistoryVersion) + ",\"reports\":[" + list + "]}";
}

void ReportHistory::Load() {
    reports_.clear();
    std::error_code ec;
    if (!fs::exists(file_, ec)) return;
    std::string text;
    {
        // Closed before parsing: Windows cannot rename a damaged file while this stream holds it.
        std::ifstream in(file_, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const std::optional<json::Value> root = json::Parse(text);
    const json::Value* list = root && root->IsObject() ? root->Find("reports") : nullptr;
    if (!list || !list->IsArray()) {
        SetAside(file_);
        return;
    }
    for (const json::Value& entry : list->array) {
        if (!entry.IsObject()) continue;
        TrackedReport report = EntryOf(entry);
        if (IsTicket(report.ticket) && IsReceipt(report.receipt) && ReportStatusText(report.status)) reports_.push_back(std::move(report));
    }
    NewestFirst(reports_);
}

bool ReportHistory::Save() const {
    std::error_code ec;
    fs::create_directories(file_.parent_path(), ec);
    fs::path temporary = file_;
    temporary += ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << Dump();
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::permissions(temporary, fs::perms::owner_read | fs::perms::owner_write, ec);
    fs::rename(temporary, file_, ec);
    if (!ec) return true;
    std::error_code ignored;
    fs::remove(temporary, ignored);
    return false;
}

bool ReportHistory::HasUnseen() const {
    return std::any_of(reports_.begin(), reports_.end(), [](const TrackedReport& report) { return report.unseen; });
}

bool ReportHistory::HasUnseenMessage() const {
    return std::any_of(reports_.begin(), reports_.end(), [](const TrackedReport& report) { return report.message_unseen; });
}

std::string ReportHistory::NextUnseen() const {
    const TrackedReport* best = nullptr;
    for (const TrackedReport& report : reports_) {
        if (!report.unseen && !report.message_unseen) continue;
        const bool better = !best || (report.message_unseen && !best->message_unseen) ||
                            (report.message_unseen == best->message_unseen && report.status_updated_at > best->status_updated_at);
        if (better) best = &report;
    }
    return best ? best->ticket : std::string();
}

bool ReportHistory::Add(TrackedReport report) {
    Remove(report.ticket);
    reports_.push_back(std::move(report));
    NewestFirst(reports_);
    return Save();
}

bool ReportHistory::Apply(const std::vector<ReportStatusUpdate>& statuses, const std::vector<std::string>& asked) {
    bool changed = false;
    for (const ReportStatusUpdate& status : statuses) {
        if (!ReportStatusText(status.status)) continue;
        const auto found = std::find_if(reports_.begin(), reports_.end(), [&](const TrackedReport& r) { return r.ticket == status.ticket; });
        if (found == reports_.end()) continue;
        if (std::exchange(found->unavailable, false)) changed = true;
        if (found->status == status.status && found->status_updated_at == status.updated_at &&
            found->fixed_version == status.fixed_version && found->player_message == status.player_message) {
            continue;
        }
        if (found->player_message != status.player_message) found->message_unseen = !status.player_message.empty();
        found->status = status.status;
        found->status_updated_at = status.updated_at;
        found->fixed_version = status.fixed_version;
        found->player_message = status.player_message;
        found->unseen = true;
        changed = true;
    }
    for (const std::string& ticket : asked) {
        const bool answered = std::any_of(statuses.begin(), statuses.end(), [&](const ReportStatusUpdate& s) { return s.ticket == ticket; });
        const auto found = std::find_if(reports_.begin(), reports_.end(), [&](const TrackedReport& r) { return r.ticket == ticket; });
        if (answered || found == reports_.end() || found->unavailable) continue;
        found->unavailable = true;
        changed = true;
    }
    return changed;
}

bool ReportHistory::MarkSeen(std::string_view ticket) {
    const auto found = std::find_if(reports_.begin(), reports_.end(), [&](const TrackedReport& r) { return r.ticket == ticket; });
    if (found == reports_.end() || (!found->unseen && !found->message_unseen)) return false;
    found->unseen = false;
    found->message_unseen = false;
    return true;
}

bool ReportHistory::Remove(std::string_view ticket) {
    return std::erase_if(reports_, [&](const TrackedReport& r) { return r.ticket == ticket; }) > 0;
}

} // namespace osl
