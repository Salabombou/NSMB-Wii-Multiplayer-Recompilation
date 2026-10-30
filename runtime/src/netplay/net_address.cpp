#include "netplay/net_socket.h"

#include <cstdio>

// The parts of net_socket.h that touch no socket: address text in and out. Kept apart so the unit
// tests (tests/netplay_address_tests.cpp) build without Winsock or the runtime.
namespace Netplay {
namespace {

bool IsSpace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsDigit(char c) {
    return c >= '0' && c <= '9';
}

bool IsLabelChar(char c) {
    return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
}

char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool Fail(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
    return false;
}

} // namespace

std::string IpToString(uint32_t ip) {
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    return text;
}

std::string ToString(const Address& address) {
    return IpToString(address.ip) + ":" + std::to_string(address.port);
}

bool ParseAddress(std::string_view text, uint16_t defaultPort, Address& out) {
    uint32_t parts[4] = {};
    size_t index = 0;
    size_t pos = 0;
    for (; index < 4; ++index) {
        if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') {
            return false;
        }
        uint32_t value = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            value = value * 10 + static_cast<uint32_t>(text[pos++] - '0');
            if (value > 255) {
                return false;
            }
        }
        parts[index] = value;
        if (index < 3) {
            if (pos >= text.size() || text[pos] != '.') {
                return false;
            }
            ++pos;
        }
    }
    uint32_t port = defaultPort;
    if (pos < text.size()) {
        if (text[pos] != ':') {
            return false;
        }
        ++pos;
        port = 0;
        if (pos >= text.size()) {
            return false;
        }
        for (; pos < text.size(); ++pos) {
            if (text[pos] < '0' || text[pos] > '9') {
                return false;
            }
            port = port * 10 + static_cast<uint32_t>(text[pos] - '0');
            if (port > 65535) {
                return false;
            }
        }
    }
    out.ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    out.port = static_cast<uint16_t>(port);
    return out.ip != 0 && out.port != 0;
}

bool SplitHostPort(std::string_view text, uint16_t defaultPort, std::string& host, uint16_t& port, std::string* error) {
    while (!text.empty() && IsSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && IsSpace(text.back())) {
        text.remove_suffix(1);
    }
    // Pasted as a link ("udp://example.com:52130/"): keep what lies between the scheme and the path.
    if (const size_t scheme = text.find("://"); scheme != std::string_view::npos) {
        text.remove_prefix(scheme + 3);
    }
    if (const size_t slash = text.find('/'); slash != std::string_view::npos) {
        text = text.substr(0, slash);
    }
    if (text.empty()) {
        return Fail(error, "Type an address.");
    }
    const size_t colon = text.find(':');
    if (text.front() == '[' || (colon != std::string_view::npos && text.find(':', colon + 1) != std::string_view::npos)) {
        return Fail(error, "IPv6 addresses aren't supported. Use an IPv4 address or a name.");
    }
    std::string_view name = text;
    uint32_t number = defaultPort;
    if (colon != std::string_view::npos) {
        name = text.substr(0, colon);
        const std::string_view digits = text.substr(colon + 1);
        if (digits.empty() || digits.size() > 5) {
            return Fail(error, "The port must be a number from 1 to 65535.");
        }
        number = 0;
        for (const char c : digits) {
            if (!IsDigit(c)) {
                return Fail(error, "The port must be a number from 1 to 65535.");
            }
            number = number * 10 + static_cast<uint32_t>(c - '0');
        }
        if (number == 0 || number > 65535) {
            return Fail(error, "The port must be a number from 1 to 65535.");
        }
    }
    if (!name.empty() && name.back() == '.') {
        name.remove_suffix(1);  // a fully qualified name's root
    }
    if (name.empty()) {
        return Fail(error, "Type an address before the port.");
    }
    if (name.size() > 253) {
        return Fail(error, "That name is too long.");
    }
    bool numeric = true;
    size_t labelStart = 0;
    for (size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '.') {
            const size_t length = i - labelStart;
            if (length == 0 || length > 63) {
                return Fail(error, "That isn't a valid address or name.");
            }
            if (name[labelStart] == '-' || name[i - 1] == '-') {
                return Fail(error, "That isn't a valid address or name.");
            }
            labelStart = i + 1;
            continue;
        }
        if (!IsLabelChar(name[i])) {
            return Fail(error, "That isn't a valid address or name.");
        }
        numeric = numeric && IsDigit(name[i]);
    }
    // Only digits and dots is meant as an IPv4 address, never looked up as a name.
    if (numeric) {
        Address parsed;
        if (!ParseAddress(name, static_cast<uint16_t>(number), parsed)) {
            return Fail(error, "That isn't a valid IPv4 address.");
        }
    }
    host.clear();
    host.reserve(name.size());
    for (const char c : name) {
        host.push_back(Lower(c));
    }
    port = static_cast<uint16_t>(number);
    return true;
}

bool IsTailscaleIp(uint32_t ip) {
    return (ip & 0xFFC00000u) == 0x64400000u;  // 100.64.0.0/10
}

bool IsPrivateIp(uint32_t ip) {
    return (ip & 0xFF000000u) == 0x0A000000u ||  // 10.0.0.0/8
           (ip & 0xFFF00000u) == 0xAC100000u ||  // 172.16.0.0/12
           (ip & 0xFFFF0000u) == 0xC0A80000u ||  // 192.168.0.0/16
           (ip & 0xFFFF0000u) == 0xA9FE0000u ||  // 169.254.0.0/16
           IsTailscaleIp(ip);
}

} // namespace Netplay
