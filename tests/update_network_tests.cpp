// Update network tests: the controller against a scripted loopback HTTP server. Every failure must end in a terminal
// state that offers a retry, and a retry must start clean. Loopback only: no Internet, no window, no game files.
//   osl-update-network-tests
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <miniz.h>
#include <trg/download.h>

#include "updater/sha256.h"
#include "updater/update_controller.h"
#include "updater/update_paths.h"
#include "updater/update_platform.h"

using namespace trg::update;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        ++g_checks;                                                \
        if (!(cond)) {                                             \
            ++g_failures;                                          \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                          \
    } while (0)

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
constexpr int kNoSignal = 0;
void CloseSocket(Socket s) { closesocket(s); }
#else
using Socket = int;
constexpr Socket kNoSocket = -1;
constexpr int kNoSignal = MSG_NOSIGNAL;
void CloseSocket(Socket s) { close(s); }
#endif

bool Readable(Socket socket, int milliseconds) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeval timeout{milliseconds / 1000, (milliseconds % 1000) * 1000};
    return select(int(socket) + 1, &set, nullptr, nullptr, &timeout) > 0;
}

struct Step {
    enum class Kind { kBody, kStatus, kRedirect, kCutShort, kStall };
    Kind kind = Kind::kBody;
    int status = 200;
    std::string body;
    std::string location;
};

Step Body(std::string body) { return {Step::Kind::kBody, 200, std::move(body), {}}; }
Step Status(int status) { return {Step::Kind::kStatus, status, "unavailable", {}}; }
Step Redirect(std::string location) { return {Step::Kind::kRedirect, 302, {}, std::move(location)}; }
Step CutShort(std::string body) { return {Step::Kind::kCutShort, 200, std::move(body), {}}; }
Step Stall(std::string body) { return {Step::Kind::kStall, 200, std::move(body), {}}; }

// Serves one connection at a time. A path's steps answer its requests in order; the last one repeats.
class Server {
public:
    Server() {
#ifdef _WIN32
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        listen(listener_, 8);
        socklen_t length = sizeof(address);
        getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { Run(); });
    }
    ~Server() {
        stop_ = true;
        thread_.join();
        CloseSocket(listener_);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }
    bool Allows(const std::string& url) const { return url.rfind(Url("/"), 0) == 0; }

    void Script(const std::string& path, std::vector<Step> steps) {
        std::lock_guard<std::mutex> lock(mutex_);
        scripts_[path] = std::move(steps);
        counts_[path] = 0;
    }
    int Requests(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        return counts_[path];
    }
    int Requests() {
        std::lock_guard<std::mutex> lock(mutex_);
        int total = 0;
        for (const auto& entry : counts_) total += entry.second;
        return total + unscripted_;
    }

private:
    void Run() {
        while (!stop_) {
            if (!Readable(listener_, 50)) continue;
            const Socket client = accept(listener_, nullptr, nullptr);
            if (client != kNoSocket) {
                Serve(client);
                CloseSocket(client);
            }
        }
    }

    void Send(Socket client, const std::string& text) { send(client, text.data(), int(text.size()), kNoSignal); }

    void Serve(Socket client) {
        std::string request;
        char buffer[2048];
        while (request.find("\r\n\r\n") == std::string::npos && Readable(client, 2000)) {
            const int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) return;
            request.append(buffer, size_t(count));
        }
        const size_t start = request.find(' ');
        const size_t end = request.find(' ', start + 1);
        if (start == std::string::npos || end == std::string::npos) return;
        const std::string path = request.substr(start + 1, end - start - 1);

        Step step = Status(404);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto script = scripts_.find(path);
            if (script != scripts_.end()) {
                const int index = counts_[path]++;
                step = script->second[std::min<size_t>(size_t(index), script->second.size() - 1)];
            } else {
                ++unscripted_;
            }
        }

        const auto head = [&](int status, size_t length, const std::string& extra) {
            return "HTTP/1.1 " + std::to_string(status) + " Scripted\r\nContent-Length: " + std::to_string(length) +
                   "\r\nConnection: close\r\n" + extra + "\r\n";
        };
        switch (step.kind) {
        case Step::Kind::kBody:
        case Step::Kind::kStatus:
            Send(client, head(step.status, step.body.size(), "") + step.body);
            break;
        case Step::Kind::kRedirect:
            Send(client, head(step.status, 0, "Location: " + step.location + "\r\n"));
            break;
        case Step::Kind::kCutShort:
            Send(client, head(200, step.body.size(), "") + step.body.substr(0, step.body.size() / 2));
            break;
        case Step::Kind::kStall:
            Send(client, head(200, step.body.size(), "") + step.body.substr(0, step.body.size() / 2));
            while (!stop_ && !Readable(client, 50)) {
            }
            break;
        }
    }

    Socket listener_ = kNoSocket;
    unsigned short port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::map<std::string, std::vector<Step>> scripts_;
    std::map<std::string, int> counts_;
    int unscripted_ = 0;
};

class TempDir {
public:
    TempDir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("osl-update-network-tests-" + std::to_string(now));
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
};

std::string MakeZip(const std::string& name, const std::string& content) {
    mz_zip_archive archive{};
    if (mz_zip_writer_init_heap(&archive, 0, 0) == MZ_FALSE ||
        mz_zip_writer_add_mem(&archive, name.c_str(), content.data(), content.size(), MZ_BEST_COMPRESSION) == MZ_FALSE) {
        return {};
    }
    void* data = nullptr;
    size_t size = 0;
    std::string result;
    if (mz_zip_writer_finalize_heap_archive(&archive, &data, &size) != MZ_FALSE) {
        result.assign(static_cast<const char*>(data), size);
        mz_free(data);
    }
    mz_zip_writer_end(&archive);
    return result;
}

const std::string kPackageName = std::string(kReleaseName) + "-v1.1.0-windows-x64.zip";
const std::string kChecksumName = std::string(kReleaseName) + "-v1.1.0-windows-x64.sha256";
const std::string kPackageRoot = std::string(kReleaseName) + "-v1.1.0-windows-x64";
constexpr char kExecutable[] = "odin_sphere_leifthrasir.exe";

struct Fixture {
    Server server;
    TempDir temp;
    std::string package = MakeZip(kPackageRoot + "/" + kExecutable, "new launcher");

    std::string ReleaseJson(bool with_digest) {
        const std::string digest = with_digest ? ",\"digest\":\"sha256:" + Sha256Hex(package) + "\"" : std::string();
        const std::string checksum = "{\"name\":\"" + kChecksumName + "\",\"browser_download_url\":\"" +
                                     server.Url("/checksum") + "\",\"size\":100}";
        return "{\"tag_name\":\"v1.1.0\",\"draft\":false,\"prerelease\":false,\"html_url\":\"" + server.Url("/page") +
               "\",\"assets\":[{\"name\":\"" + kPackageName + "\",\"browser_download_url\":\"" + server.Url("/package") +
               "\",\"size\":" + std::to_string(package.size()) + digest + "}," + checksum + "]}";
    }

    UpdateConfig Config() {
        UpdateConfig config;
        config.current_version = "1.0.0";
        config.releases_url = server.Url("/release");
        config.user_agent = "update-network-tests";
        config.asset_name = [](const SemanticVersion&) { return kPackageName; };
        config.checksum_asset_name = [](const SemanticVersion&) { return kChecksumName; };
        config.package_root = [](const SemanticVersion&) { return kPackageRoot; };
        config.install_dir = temp.path / "install";
        config.restart_executable = config.install_dir / kExecutable;
        config.required_staged_files = {kExecutable};
        config.install_supported = true;
        config.url_filter = [this](const std::string& url) { return server.Allows(url); };
        config.connect_timeout_ms = 2000;
        config.receive_timeout_ms = 400;
        return config;
    }

    void ServeRelease(bool with_digest) { server.Script("/release", {Body(ReleaseJson(with_digest))}); }
    void ServeChecksum() { server.Script("/checksum", {Body(Sha256Hex(package) + "  " + kPackageName + "\n")}); }
};

bool Working(UpdateState state) {
    return state == UpdateState::kChecking || state == UpdateState::kDownloading || state == UpdateState::kVerifying;
}

// Polls like the launcher does until the controller leaves its working states, or the deadline passes.
UpdateSnapshot Settle(UpdateController& controller, int limit_ms = 15000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit_ms);
    do {
        controller.Poll();
        if (!Working(controller.snapshot().state) && !controller.Busy()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < deadline);
    return controller.snapshot();
}

UpdateSnapshot CheckAndSettle(UpdateController& controller) {
    controller.StartCheck(true);
    return Settle(controller);
}

UpdateSnapshot DownloadAndSettle(UpdateController& controller) {
    controller.StartDownload();
    return Settle(controller);
}

UpdateSnapshot RetryAndSettle(UpdateController& controller) {
    controller.Retry();
    return Settle(controller);
}

int Workspaces() {
    int count = 0;
    const std::string prefix = "trg-update-" + std::to_string(CurrentProcessId()) + "-";
    std::error_code ec;
    for (std::filesystem::directory_iterator it(std::filesystem::temp_directory_path(), ec), end; !ec && it != end;
         it.increment(ec)) {
        if (Utf8Path(it->path().filename()).rfind(prefix, 0) == 0) ++count;
    }
    return count;
}

bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

void ExpectFailedWithRetry(const UpdateSnapshot& snapshot) {
    CHECK(snapshot.state == UpdateState::kFailed);
    CHECK(snapshot.retry_available);
    CHECK(!snapshot.error.empty());
}

void TestReleaseCheckFailures() {
    std::printf("release check failures\n");
    for (const int status : {404, 500, 504}) {
        Fixture fixture;
        UpdateController controller(fixture.Config());
        fixture.server.Script("/release", {Status(status), Body(fixture.ReleaseJson(true))});
        UpdateSnapshot snapshot = CheckAndSettle(controller);
        ExpectFailedWithRetry(snapshot);
        CHECK(snapshot.retry_check);
        CHECK(Contains(snapshot.error, std::to_string(status).c_str()));
        snapshot = RetryAndSettle(controller);
        CHECK(snapshot.state == UpdateState::kAvailable);
        CHECK(snapshot.latest_version == "1.1.0");
        CHECK(fixture.server.Requests("/release") == 2);
    }

    Fixture closed;
    UpdateController closed_controller(closed.Config());
    closed.server.Script("/release", {CutShort(closed.ReleaseJson(true)), Body(closed.ReleaseJson(true))});
    UpdateSnapshot snapshot = CheckAndSettle(closed_controller);
    ExpectFailedWithRetry(snapshot);
    snapshot = RetryAndSettle(closed_controller);
    CHECK(snapshot.state == UpdateState::kAvailable);

    Fixture stalled;
    UpdateController stalled_controller(stalled.Config());
    stalled.server.Script("/release", {Stall(stalled.ReleaseJson(true)), Body(stalled.ReleaseJson(true))});
    const auto started = std::chrono::steady_clock::now();
    snapshot = CheckAndSettle(stalled_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
    snapshot = RetryAndSettle(stalled_controller);
    CHECK(snapshot.state == UpdateState::kAvailable);

    Fixture large;
    UpdateController large_controller(large.Config());
    large.server.Script("/release", {Body(std::string(size_t(5) << 20, ' '))});
    ExpectFailedWithRetry(CheckAndSettle(large_controller));

    Fixture garbage;
    UpdateController garbage_controller(garbage.Config());
    garbage.server.Script("/release", {Body("not json")});
    ExpectFailedWithRetry(CheckAndSettle(garbage_controller));
}

void TestChecksumFailures() {
    std::printf("checksum request failures\n");
    for (const int status : {404, 500, 504}) {
        Fixture fixture;
        UpdateController controller(fixture.Config());
        fixture.ServeRelease(false);
        fixture.server.Script("/package", {Body(fixture.package)});
        fixture.server.Script("/checksum", {Status(status), Body(Sha256Hex(fixture.package) + "  " + kPackageName + "\n")});
        CHECK(CheckAndSettle(controller).state == UpdateState::kAvailable);

        const int workspaces = Workspaces();
        UpdateSnapshot snapshot = DownloadAndSettle(controller);
        ExpectFailedWithRetry(snapshot);
        CHECK(!snapshot.retry_check);
        CHECK(Contains(snapshot.error, std::to_string(status).c_str()));
        CHECK(fixture.server.Requests("/package") == 0);
        CHECK(Workspaces() == workspaces);

        snapshot = RetryAndSettle(controller);
        CHECK(snapshot.state == UpdateState::kReadyToRestart);
        CHECK(fixture.server.Requests("/checksum") == 2);
    }

    Fixture wrong;
    UpdateController wrong_controller(wrong.Config());
    wrong.ServeRelease(false);
    wrong.server.Script("/package", {Body(wrong.package)});
    wrong.server.Script("/checksum", {Body(std::string(64, '0') + "  " + kPackageName + "\n")});
    CHECK(CheckAndSettle(wrong_controller).state == UpdateState::kAvailable);
    UpdateSnapshot snapshot = DownloadAndSettle(wrong_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(Contains(snapshot.error, "SHA-256"));

    Fixture invalid;
    UpdateController invalid_controller(invalid.Config());
    invalid.ServeRelease(false);
    invalid.server.Script("/checksum", {Body("not a checksum\n")});
    CHECK(CheckAndSettle(invalid_controller).state == UpdateState::kAvailable);
    ExpectFailedWithRetry(DownloadAndSettle(invalid_controller));
    CHECK(invalid.server.Requests("/package") == 0);
}

void TestPackageFailures() {
    std::printf("package download failures\n");
    for (const int status : {404, 500, 504}) {
        Fixture fixture;
        UpdateController controller(fixture.Config());
        fixture.ServeRelease(true);
        CHECK(CheckAndSettle(controller).state == UpdateState::kAvailable);

        fixture.server.Script("/package", {Status(status), Body(fixture.package)});
        const int workspaces = Workspaces();
        UpdateSnapshot snapshot = DownloadAndSettle(controller);
        ExpectFailedWithRetry(snapshot);
        CHECK(Contains(snapshot.error, std::to_string(status).c_str()));
        CHECK(snapshot.progress < 0.0f);
        CHECK(Workspaces() == workspaces);
        snapshot = RetryAndSettle(controller);
        CHECK(snapshot.state == UpdateState::kReadyToRestart);
    }

    Fixture closed;
    UpdateController closed_controller(closed.Config());
    closed.ServeRelease(true);
    CHECK(CheckAndSettle(closed_controller).state == UpdateState::kAvailable);
    closed.server.Script("/package", {CutShort(closed.package), CutShort(closed.package), Body(closed.package)});
    const int closed_workspaces = Workspaces();
    UpdateSnapshot snapshot = DownloadAndSettle(closed_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(Workspaces() == closed_workspaces);
    snapshot = RetryAndSettle(closed_controller);
    ExpectFailedWithRetry(snapshot);
    snapshot = RetryAndSettle(closed_controller);
    CHECK(snapshot.state == UpdateState::kReadyToRestart);
    CHECK(closed.server.Requests("/package") == 3);

    Fixture stalled;
    UpdateController stalled_controller(stalled.Config());
    stalled.ServeRelease(true);
    CHECK(CheckAndSettle(stalled_controller).state == UpdateState::kAvailable);
    stalled.server.Script("/package", {Stall(stalled.package), Body(stalled.package)});
    const int stalled_workspaces = Workspaces();
    const auto started = std::chrono::steady_clock::now();
    snapshot = DownloadAndSettle(stalled_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(!stalled_controller.Busy());
    CHECK(Workspaces() == stalled_workspaces);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
    snapshot = RetryAndSettle(stalled_controller);
    CHECK(snapshot.state == UpdateState::kReadyToRestart);

    Fixture oversized;
    UpdateController oversized_controller(oversized.Config());
    oversized.ServeRelease(true);
    CHECK(CheckAndSettle(oversized_controller).state == UpdateState::kAvailable);
    oversized.server.Script("/package", {Body(std::string(size_t(2) << 20, 'x'))});
    const int oversized_workspaces = Workspaces();
    ExpectFailedWithRetry(DownloadAndSettle(oversized_controller));
    CHECK(Workspaces() == oversized_workspaces);

    Fixture corrupt;
    UpdateController corrupt_controller(corrupt.Config());
    corrupt.ServeRelease(true);
    CHECK(CheckAndSettle(corrupt_controller).state == UpdateState::kAvailable);
    std::string altered = corrupt.package;
    altered.back() = static_cast<char>(altered.back() ^ 0x55);
    corrupt.server.Script("/package", {Body(altered)});
    const int corrupt_workspaces = Workspaces();
    snapshot = DownloadAndSettle(corrupt_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(Contains(snapshot.error, "SHA-256"));
    CHECK(Workspaces() == corrupt_workspaces);

    Fixture misplaced;
    misplaced.package = MakeZip("elsewhere/" + std::string(kExecutable), "new launcher");
    UpdateController misplaced_controller(misplaced.Config());
    misplaced.ServeRelease(true);
    misplaced.server.Script("/package", {Body(misplaced.package)});
    CHECK(CheckAndSettle(misplaced_controller).state == UpdateState::kAvailable);
    ExpectFailedWithRetry(DownloadAndSettle(misplaced_controller));
}

void TestCancellation() {
    std::printf("cancellation\n");
    Fixture fixture;
    UpdateController controller(fixture.Config());
    fixture.ServeRelease(true);
    CHECK(CheckAndSettle(controller).state == UpdateState::kAvailable);
    fixture.server.Script("/package", {Stall(fixture.package), Body(fixture.package)});

    const int workspaces = Workspaces();
    controller.StartDownload();
    const auto started = std::chrono::steady_clock::now();
    while (fixture.server.Requests("/package") == 0 && std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
        controller.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(fixture.server.Requests("/package") == 1);
    controller.Cancel();
    const UpdateSnapshot snapshot = Settle(controller);
    CHECK(!controller.Busy());
    CHECK(snapshot.state != UpdateState::kDownloading && snapshot.state != UpdateState::kReadyToRestart);
    CHECK(Workspaces() == workspaces);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(10));
}

void TestRedirects() {
    std::printf("redirects\n");
    Fixture fixture;
    UpdateController controller(fixture.Config());
    fixture.ServeRelease(true);
    CHECK(CheckAndSettle(controller).state == UpdateState::kAvailable);

    fixture.server.Script("/package", {Redirect(fixture.server.Url("/asset"))});
    fixture.server.Script("/asset", {Body(fixture.package)});
    CHECK(DownloadAndSettle(controller).state == UpdateState::kReadyToRestart);

    Fixture foreign;
    UpdateController foreign_controller(foreign.Config());
    foreign.ServeRelease(true);
    CHECK(CheckAndSettle(foreign_controller).state == UpdateState::kAvailable);
    foreign.server.Script("/package", {Redirect("http://localhost:1/package")});
    UpdateSnapshot snapshot = DownloadAndSettle(foreign_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(Contains(snapshot.error, "not allowed"));

    Fixture loop;
    UpdateController loop_controller(loop.Config());
    loop.ServeRelease(true);
    CHECK(CheckAndSettle(loop_controller).state == UpdateState::kAvailable);
    loop.server.Script("/package", {Redirect(loop.server.Url("/package"))});
    const int loop_workspaces = Workspaces();
    snapshot = DownloadAndSettle(loop_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(Contains(snapshot.error, "redirected too many times"));
    CHECK(loop.server.Requests("/package") == 6);
    CHECK(Workspaces() == loop_workspaces);

    Fixture malformed;
    UpdateController malformed_controller(malformed.Config());
    malformed.ServeRelease(true);
    CHECK(CheckAndSettle(malformed_controller).state == UpdateState::kAvailable);
    malformed.server.Script("/package", {Redirect("::not a url")});
    ExpectFailedWithRetry(DownloadAndSettle(malformed_controller));

    Fixture release_redirect;
    UpdateController release_controller(release_redirect.Config());
    release_redirect.server.Script("/release", {Redirect("http://localhost:1/release")});
    snapshot = CheckAndSettle(release_controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(Contains(snapshot.error, "not allowed"));
}

void TestAddressPolicy() {
    std::printf("address policy in the controller\n");
    Fixture fixture;
    UpdateConfig config = fixture.Config();
    config.url_filter = nullptr;
    config.releases_url = "https://example.com/releases/latest";
    config.download_url_prefix = "https://github.com/owner/project/releases/download/";
    UpdateController controller(config);
    const UpdateSnapshot snapshot = CheckAndSettle(controller);
    ExpectFailedWithRetry(snapshot);
    CHECK(snapshot.retry_check);

    UpdateConfig http_config = fixture.Config();
    http_config.url_filter = nullptr;
    UpdateController http_controller(http_config);
    fixture.ServeRelease(true);
    ExpectFailedWithRetry(CheckAndSettle(http_controller));
    CHECK(fixture.server.Requests() == 0);

    const std::filesystem::path release_file = fixture.temp.path / "release.json";
    const auto release_with = [&](const std::string& package_url) {
        std::ofstream(release_file, std::ios::binary | std::ios::trunc)
            << "{\"tag_name\":\"v1.1.0\",\"draft\":false,\"prerelease\":false,\"html_url\":\"https://github.com/owner/project\","
               "\"assets\":[{\"name\":\"" + kPackageName + "\",\"browser_download_url\":\"" + package_url +
                   "\",\"size\":1000,\"digest\":\"sha256:" + std::string(64, 'a') + "\"}]}";
    };
    const auto check_with = [&](const std::string& package_url) {
        release_with(package_url);
        UpdateConfig local = fixture.Config();
        local.url_filter = nullptr;
        local.release_json_override = release_file;
        local.download_url_prefix = "https://github.com/owner/project/releases/download/";
        UpdateController local_controller(local);
        return CheckAndSettle(local_controller);
    };
    const std::string good = "https://github.com/owner/project/releases/download/v1.1.0/" + kPackageName;
    CHECK(check_with(good).state == UpdateState::kAvailable);
    CHECK(check_with("https://github.com/other/project/releases/download/v1.1.0/" + kPackageName).state == UpdateState::kFailed);
    CHECK(check_with("http://github.com/owner/project/releases/download/v1.1.0/" + kPackageName).state == UpdateState::kFailed);
    CHECK(check_with("https://example.com/owner/project/releases/download/v1.1.0/" + kPackageName).state == UpdateState::kFailed);
    CHECK(check_with(fixture.server.Url("/" + kPackageName)).state == UpdateState::kFailed);
    CHECK(fixture.server.Requests() == 0);
}

void TestTransportTimeouts() {
    std::printf("transport timeouts\n");
    Fixture fixture;
    trg::HttpOptions options;
    options.receive_timeout_ms = 300;
    options.total_timeout_ms = 3000;
    options.allow_url = [&](const std::string& url) { return fixture.server.Allows(url); };

    std::string out;
    fixture.server.Script("/slow", {Stall("0123456789")});
    const auto started = std::chrono::steady_clock::now();
    CHECK(!trg::HttpGet(fixture.server.Url("/slow"), out, nullptr, options).empty());
    CHECK(out.empty());
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(3));

    fixture.server.Script("/ok", {Body("hello")});
    CHECK(trg::HttpGet(fixture.server.Url("/ok"), out, nullptr, options).empty());
    CHECK(out == "hello");
    CHECK(!trg::HttpGet("http://127.0.0.1:1/ok", out, nullptr, options).empty());
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestReleaseCheckFailures();
    TestChecksumFailures();
    TestPackageFailures();
    TestCancellation();
    TestRedirects();
    TestAddressPolicy();
    TestTransportTimeouts();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
