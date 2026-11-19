// One short outgoing HTTPS request with a JSON body, for the Bug Report page. It opens a connection when called and closes
// it before it returns: the launcher never listens on a port and keeps no connection open. Blocking; call it off the UI
// thread.
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace osl {

enum class HttpFailure { kNone, kInvalidAddress, kUnreachable, kTimedOut, kCertificate, kClientMissing };

struct HttpReply {
    HttpFailure failure = HttpFailure::kNone;
    int status = 0;
    std::string body;
};

struct HttpLimits {
    int connect_ms = 8000;
    int send_ms = 15000;
    int receive_ms = 20000;
    int total_ms = 30000;
    size_t most_reply_bytes = 256 * 1024;
};

struct ServiceAddress {
    std::string host;
    int port = 443;
    std::string path = "/";
};

// `https://host[:port]` followed by an absolute path. No other scheme, user name, query or fragment is accepted.
bool ParseServiceAddress(std::string_view url, ServiceAddress& address);

using PostJson = std::function<HttpReply(const std::string& url, const std::string& body, const HttpLimits& limits)>;

// Certificates are validated by the system (WinHTTP, or curl on Linux); redirects are not followed.
HttpReply PostJsonHttps(const std::string& url, const std::string& body, const HttpLimits& limits);

} // namespace osl
