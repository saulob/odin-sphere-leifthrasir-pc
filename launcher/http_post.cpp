#include "http_post.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

#ifndef OSL_PORT_VERSION
#define OSL_PORT_VERSION "0"
#endif

namespace osl {
namespace {

bool IsAddressLiteral(std::string_view text) {
    return text.size() > 2 && std::all_of(text.begin() + 1, text.end() - 1, [](char c) { return c == ':' || c == '.' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); });
}

bool IsHostCharacter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
}

bool ParsePort(std::string_view text, int& port) {
    if (text.empty() || text.size() > 5 || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    port = std::stoi(std::string(text));
    return port >= 1 && port <= 65535;
}

} // namespace

bool ParseServiceAddress(std::string_view url, ServiceAddress& address) {
    ServiceAddress parsed;
    if (url.starts_with("https://")) {
        url.remove_prefix(8);
    } else {
        return false;
    }
    const size_t slash = url.find('/');
    std::string_view authority = url.substr(0, slash);
    parsed.path = slash == std::string_view::npos ? "/" : std::string(url.substr(slash));
    if (parsed.path.find_first_of("?#\\ ") != std::string::npos || authority.empty()) return false;
    if (std::any_of(url.begin(), url.end(), [](char c) { return static_cast<unsigned char>(c) <= 0x20 || static_cast<unsigned char>(c) >= 0x7F; })) return false;

    std::string_view host = authority;
    if (authority.front() == '[') {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) return false;
        host = authority.substr(0, close + 1);
        if (!IsAddressLiteral(host)) return false;
        const std::string_view rest = authority.substr(close + 1);
        if (!rest.empty() && (rest.front() != ':' || !ParsePort(rest.substr(1), parsed.port))) return false;
    } else {
        const size_t colon = authority.find(':');
        host = authority.substr(0, colon);
        if (colon != std::string_view::npos && !ParsePort(authority.substr(colon + 1), parsed.port)) return false;
        if (host.empty() || !std::all_of(host.begin(), host.end(), IsHostCharacter) || host.front() == '.' || host.front() == '-') return false;
    }
    parsed.host = std::string(host);
    address = std::move(parsed);
    return true;
}

#ifdef _WIN32

namespace {

std::wstring Widen(std::string_view text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), size);
    return wide;
}

HttpFailure FailureOf(DWORD error) {
    switch (error) {
    case ERROR_WINHTTP_TIMEOUT: return HttpFailure::kTimedOut;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
    case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
    case ERROR_WINHTTP_SECURE_INVALID_CA:
    case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
    case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
    case ERROR_WINHTTP_SECURE_INVALID_CERT:
    case ERROR_WINHTTP_SECURE_CERT_REVOKED:
    case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: return HttpFailure::kCertificate;
    default: return HttpFailure::kUnreachable;
    }
}

class Handle {
public:
    explicit Handle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~Handle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return handle_ != nullptr; }
    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_;
};

bool ReadBody(HINTERNET request, const HttpLimits& limits, std::chrono::steady_clock::time_point deadline, HttpReply& reply) {
    for (;;) {
        if (std::chrono::steady_clock::now() > deadline) {
            reply.failure = HttpFailure::kTimedOut;
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            reply.failure = FailureOf(GetLastError());
            return false;
        }
        if (available == 0) return true;
        if (reply.body.size() + available > limits.most_reply_bytes) {
            reply.failure = HttpFailure::kUnreachable;
            return false;
        }
        const size_t at = reply.body.size();
        reply.body.resize(at + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, reply.body.data() + at, available, &read)) {
            reply.failure = FailureOf(GetLastError());
            return false;
        }
        reply.body.resize(at + read);
    }
}

} // namespace

HttpReply PostJsonHttps(const std::string& url, const std::string& body, const HttpLimits& limits) {
    HttpReply reply;
    ServiceAddress address;
    if (!ParseServiceAddress(url, address) || body.size() > 0x7FFFFFFF) {
        reply.failure = HttpFailure::kInvalidAddress;
        return reply;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.total_ms);
    const std::wstring agent = Widen(std::string("Odin-Sphere-Leifthrasir-PC/") + OSL_PORT_VERSION + " bug-report");
    Handle session(WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        reply.failure = HttpFailure::kUnreachable;
        return reply;
    }
    WinHttpSetTimeouts(session.get(), limits.connect_ms, limits.connect_ms, limits.send_ms, limits.receive_ms);
#if defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2) && defined(WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3)
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
#endif
    Handle connection(WinHttpConnect(session.get(), Widen(address.host.front() == '[' ? address.host.substr(1, address.host.size() - 2) : address.host).c_str(),
                                     INTERNET_PORT(address.port), 0));
    if (!connection) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    Handle request(WinHttpOpenRequest(connection.get(), L"POST", Widen(address.path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects));
    const wchar_t headers[] = L"Content-Type: application/json\r\nAccept: application/json\r\n";
    if (!WinHttpSendRequest(request.get(), headers, DWORD(-1), const_cast<char*>(body.data()), DWORD(body.size()), DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        reply.failure = FailureOf(GetLastError());
        return reply;
    }
    reply.status = int(status);
    ReadBody(request.get(), limits, deadline, reply);
    return reply;
}

#else

namespace {

HttpFailure FailureOfCurl(int exit_code) {
    switch (exit_code) {
    case 28: return HttpFailure::kTimedOut;
    case 35:
    case 51:
    case 58:
    case 60:
    case 77:
    case 83: return HttpFailure::kCertificate;
    default: return HttpFailure::kUnreachable;
    }
}

// A write to a curl that already exited must fail with EPIPE, not end the process.
class SigpipeBlocked {
public:
    SigpipeBlocked() {
        sigset_t pipe;
        sigemptyset(&pipe);
        sigaddset(&pipe, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &pipe, &previous_);
    }
    ~SigpipeBlocked() {
        sigset_t pipe;
        sigemptyset(&pipe);
        sigaddset(&pipe, SIGPIPE);
        const timespec none{0, 0};
        while (sigtimedwait(&pipe, nullptr, &none) > 0) {
        }
        pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
    }
    SigpipeBlocked(const SigpipeBlocked&) = delete;
    SigpipeBlocked& operator=(const SigpipeBlocked&) = delete;

private:
    sigset_t previous_;
};

void CloseDescriptor(int& descriptor) {
    if (descriptor >= 0) close(descriptor);
    descriptor = -1;
}

int SecondsCeil(int milliseconds) { return std::max(1, (milliseconds + 999) / 1000); }

long long MillisecondsLeft(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
}

bool WriteAll(int descriptor, const std::string& data, std::chrono::steady_clock::time_point deadline) {
    size_t sent = 0;
    while (sent < data.size()) {
        const long long left = MillisecondsLeft(deadline);
        pollfd wait{descriptor, POLLOUT, 0};
        if (left <= 0) return false;
        if (poll(&wait, 1, int(std::min<long long>(left, 1000))) < 0 && errno != EINTR) return false;
        if (!(wait.revents & (POLLOUT | POLLERR | POLLHUP))) continue;
        const ssize_t written = write(descriptor, data.data() + sent, data.size() - sent);
        if (written < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return false;
        }
        sent += size_t(written);
    }
    return true;
}

} // namespace

// curl does the TLS and the certificate checks. The address and headers are arguments, never a shell string, and the body
// goes through the child's standard input.
HttpReply PostJsonHttps(const std::string& url, const std::string& body, const HttpLimits& limits) {
    HttpReply reply;
    ServiceAddress address;
    if (!ParseServiceAddress(url, address)) {
        reply.failure = HttpFailure::kInvalidAddress;
        return reply;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(limits.total_ms);
    const std::string agent = std::string("Odin-Sphere-Leifthrasir-PC/") + OSL_PORT_VERSION + " bug-report";
    const std::string connect = std::to_string(SecondsCeil(limits.connect_ms));
    const std::string total = std::to_string(SecondsCeil(limits.total_ms));
    const std::vector<std::string> arguments = {
        "curl",      "--silent",     "--proto",       "=https",        "--tlsv1.2",      "--max-redirs", "0",
        "--connect-timeout",         connect,         "--max-time",    total,            "--request",    "POST",
        "--header",  "Content-Type: application/json", "--header",     "Accept: application/json",
        "--user-agent",              agent,           "--data-binary", "@-",             "--write-out",  "\n%{http_code}",
        "--",        url};
    std::vector<char*> argv;
    for (const std::string& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);

    int to_child[2] = {-1, -1}, from_child[2] = {-1, -1};
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        for (int descriptor : {to_child[0], to_child[1], from_child[0], from_child[1]}) {
            if (descriptor >= 0) close(descriptor);
        }
        reply.failure = HttpFailure::kUnreachable;
        return reply;
    }
    const SigpipeBlocked sigpipe_blocked;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
    for (int descriptor : {to_child[0], to_child[1], from_child[0], from_child[1]}) {
        posix_spawn_file_actions_addclose(&actions, descriptor);
    }
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    sigset_t no_signals;
    sigemptyset(&no_signals);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    pid_t child = 0;
    const int spawned = posix_spawnp(&child, "curl", &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    CloseDescriptor(to_child[0]);
    CloseDescriptor(from_child[1]);
    if (spawned != 0) {
        CloseDescriptor(to_child[1]);
        CloseDescriptor(from_child[0]);
        reply.failure = HttpFailure::kUnreachable;
        return reply;
    }

    const bool body_sent = WriteAll(to_child[1], body, deadline);
    CloseDescriptor(to_child[1]);
    std::string output;
    while (body_sent) {
        const long long left = MillisecondsLeft(deadline);
        if (left <= 0) {
            reply.failure = HttpFailure::kTimedOut;
            break;
        }
        pollfd wait{from_child[0], POLLIN, 0};
        const int ready = poll(&wait, 1, int(std::min<long long>(left, 1000)));
        if (ready < 0 && errno != EINTR) break;
        if (ready <= 0) continue;
        char chunk[4096];
        const ssize_t got = read(from_child[0], chunk, sizeof(chunk));
        if (got == 0) break;
        if (got < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }
        output.append(chunk, size_t(got));
        if (output.size() > limits.most_reply_bytes + 16) {
            reply.failure = HttpFailure::kUnreachable;
            break;
        }
    }
    CloseDescriptor(from_child[0]);
    if (!body_sent || reply.failure != HttpFailure::kNone) kill(child, SIGKILL);
    int wait_status = 0;
    while (waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {
    }
    if (reply.failure != HttpFailure::kNone) return reply;
    if (!body_sent) {
        reply.failure = HttpFailure::kUnreachable;
        return reply;
    }
    if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0) {
        reply.failure = WIFEXITED(wait_status) ? FailureOfCurl(WEXITSTATUS(wait_status)) : HttpFailure::kUnreachable;
        return reply;
    }
    const size_t line = output.rfind('\n');
    if (line == std::string::npos) {
        reply.failure = HttpFailure::kUnreachable;
        return reply;
    }
    reply.status = std::atoi(output.c_str() + line + 1);
    reply.body = output.substr(0, line);
    if (reply.body.size() > limits.most_reply_bytes) {
        reply.body.clear();
        reply.failure = HttpFailure::kUnreachable;
    }
    return reply;
}

#endif

} // namespace osl
