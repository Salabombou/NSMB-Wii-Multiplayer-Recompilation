#include "netplay/public_ip.h"

#include "netplay/net_socket.h"
#include "runtime_log.h"

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
namespace AndroidBridge {
std::string HttpGetText(const std::string& url);
}
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace PublicIp {
namespace {

using Clock = std::chrono::steady_clock;

// Both answer with the bare address over IPv4 only, so the answer is never an IPv6 one.
constexpr const char* kServices[] = {"https://api.ipify.org", "https://ipv4.icanhazip.com"};
constexpr auto kKeepFor = std::chrono::minutes(10);

std::mutex g_mutex;
Result g_result;
Clock::time_point g_knownAt{};

#if defined(__ANDROID__)
std::string Fetch(const std::string& url) {
    return AndroidBridge::HttpGetText(url);  // GameActivity.httpGetText: HttpURLConnection
}
#elif defined(_WIN32)
std::string Fetch(const std::string& url) {
    const std::wstring wide(url.begin(), url.end());  // the services' URLs are ASCII
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
        return {};
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    const std::wstring path = parts.dwUrlPathLength > 0 ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) : L"/";
    HINTERNET session = WinHttpOpen(L"NSMBW-Netplay", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {  // before Windows 8.1
        session = WinHttpOpen(L"NSMBW-Netplay", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == nullptr) {
        return {};
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
    HINTERNET connect = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    HINTERNET request = connect != nullptr
                            ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                 parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                            : nullptr;
    std::string body;
    DWORD status = 0, size = sizeof(status);
    if (request != nullptr &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX) &&
        status == 200) {
        char buffer[256];
        DWORD read = 0;
        while (body.size() < 256 && WinHttpReadData(request, buffer, sizeof(buffer), &read) && read > 0) {
            body.append(buffer, read);
        }
    }
    if (request != nullptr) WinHttpCloseHandle(request);
    if (connect != nullptr) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return body;
}
#else
// argv[0] from the PATH, its standard output collected; "" when it can't run or fails.
std::string RunForOutput(const std::vector<std::string>& args) {
    int pipeEnds[2];
    if (pipe(pipeEnds) != 0) {
        return {};
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipeEnds[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipeEnds[0]);
    posix_spawn_file_actions_addclose(&actions, pipeEnds[1]);
    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipeEnds[1]);
    std::string out;
    if (spawned == 0) {
        char buffer[256];
        ssize_t got = 0;
        while (out.size() < 256 && (got = read(pipeEnds[0], buffer, sizeof(buffer))) > 0) {
            out.append(buffer, static_cast<size_t>(got));
        }
    }
    close(pipeEnds[0]);
    if (spawned != 0) {
        return {};
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? out : std::string();
}

std::string Fetch(const std::string& url) {
    std::string out = RunForOutput({"curl", "-fsS", "--max-time", "5", "-A", "NSMBW-Netplay", url});
    if (out.empty()) {
        out = RunForOutput({"wget", "-qO-", "-T", "5", "-U", "NSMBW-Netplay", url});
    }
    return out;
}
#endif

// The service's answer, if it is a bare IPv4 address (around some white space).
std::string AsAddress(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n')) {
        ++start;
    }
    text.erase(0, start);
    Netplay::Address address;
    if (text.size() > 15 || text.find(':') != std::string::npos || !Netplay::ParseAddress(text, 1, address)) {
        return {};
    }
    return Netplay::IpToString(address.ip);
}

void Lookup() {
    std::string ip;
    for (const char* service : kServices) {
        ip = AsAddress(Fetch(service));
        if (!ip.empty()) {
            break;
        }
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (ip.empty()) {
        g_result = Result{State::Failed, {}};
        RT_LOGF(RT_TAG_RUNTIME, "netplay: couldn't look up the public IP address\n");
    } else {
        g_result = Result{State::Known, ip};
        g_knownAt = Clock::now();
        RT_LOGF(RT_TAG_RUNTIME, "netplay: public IP address %s\n", ip.c_str());
    }
}

} // namespace

void Request(bool force) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_result.state == State::Looking ||
        (!force && g_result.state == State::Known && Clock::now() - g_knownAt < kKeepFor)) {
        return;
    }
    g_result.state = State::Looking;
    std::thread(Lookup).detach();
}

Result Get() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_result;
}

} // namespace PublicIp
