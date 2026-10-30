// Direct Connect's address text (src/netplay/net_address.cpp): what a player types, "host[:port]"
// with host an IPv4 address or a name like mc.okayu.zip, split and checked without the network.
#include "netplay/net_socket.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void Require(bool value, const std::string& message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

void ExpectSplit(const std::string& text, const std::string& host, uint16_t port) {
    std::string gotHost, error;
    uint16_t gotPort = 0;
    const bool ok = Netplay::SplitHostPort(text, Netplay::kDefaultPort, gotHost, gotPort, &error);
    Require(ok, "'" + text + "' was refused: " + error);
    Require(gotHost == host, "'" + text + "' gave host '" + gotHost + "', expected '" + host + "'");
    Require(gotPort == port, "'" + text + "' gave port " + std::to_string(gotPort) + ", expected " + std::to_string(port));
}

void ExpectRefused(const std::string& text) {
    std::string host, error;
    uint16_t port = 0;
    const bool ok = Netplay::SplitHostPort(text, Netplay::kDefaultPort, host, port, &error);
    Require(!ok, "'" + text + "' was accepted as '" + host + ":" + std::to_string(port) + "'");
    Require(ok || !error.empty(), "'" + text + "' was refused without a message");
}

} // namespace

int main() {
    // IPv4 addresses, with and without a port.
    ExpectSplit("203.0.113.5", "203.0.113.5", Netplay::kDefaultPort);
    ExpectSplit("203.0.113.5:6000", "203.0.113.5", 6000);
    ExpectSplit("100.84.140.32:52130", "100.84.140.32", 52130);
    ExpectSplit("  192.168.1.20:1 \r\n", "192.168.1.20", 1);
    ExpectSplit("10.0.0.1:65535", "10.0.0.1", 65535);

    // Domains: looked up later, so only their form is checked here.
    ExpectSplit("mc.okayu.zip", "mc.okayu.zip", Netplay::kDefaultPort);
    ExpectSplit("mc.okayu.zip:6000", "mc.okayu.zip", 6000);
    ExpectSplit("MC.Okayu.ZIP:52130", "mc.okayu.zip", 52130);
    ExpectSplit("mc.okayu.zip.", "mc.okayu.zip", Netplay::kDefaultPort);
    ExpectSplit("mc.okayu.zip.:7000", "mc.okayu.zip", 7000);
    ExpectSplit("s26-ultra", "s26-ultra", Netplay::kDefaultPort);
    ExpectSplit("my_host.lan", "my_host.lan", Netplay::kDefaultPort);
    ExpectSplit("localhost:5000", "localhost", 5000);
    ExpectSplit("123abc.example.com", "123abc.example.com", Netplay::kDefaultPort);
    ExpectSplit("udp://mc.okayu.zip:6000/", "mc.okayu.zip", 6000);
    ExpectSplit("mc.okayu.zip/room", "mc.okayu.zip", Netplay::kDefaultPort);
    ExpectSplit(std::string(63, 'a') + ".example", std::string(63, 'a') + ".example", Netplay::kDefaultPort);

    // Ports.
    ExpectRefused("mc.okayu.zip:");
    ExpectRefused("mc.okayu.zip:0");
    ExpectRefused("mc.okayu.zip:65536");
    ExpectRefused("mc.okayu.zip:70000");
    ExpectRefused("mc.okayu.zip:999999");
    ExpectRefused("mc.okayu.zip:abc");
    ExpectRefused("mc.okayu.zip:12a");
    ExpectRefused("mc.okayu.zip:-1");
    ExpectRefused("203.0.113.5: 6000");

    // Not addresses.
    ExpectRefused("");
    ExpectRefused("   ");
    ExpectRefused(":52130");
    ExpectRefused(".");
    ExpectRefused("mc..okayu.zip");
    ExpectRefused(".mc.okayu.zip");
    ExpectRefused("-mc.okayu.zip");
    ExpectRefused("mc-.okayu.zip");
    ExpectRefused("mc.okayu .zip");
    ExpectRefused("mc.okayu.zip!");
    ExpectRefused(std::string(64, 'a') + ".example");
    ExpectRefused(std::string(254, 'a'));
    ExpectRefused("256.1.1.1");
    ExpectRefused("1.2.3");
    ExpectRefused("1.2.3.4.5");
    ExpectRefused("0.0.0.0");

    // IPv6 is refused rather than misread as host:port.
    ExpectRefused("::1");
    ExpectRefused("[::1]:52130");
    ExpectRefused("2001:db8::1");

    // ParseAddress stays numeric only.
    {
        Netplay::Address address;
        Require(Netplay::ParseAddress("203.0.113.5:6000", 1, address) && address.ip == 0xCB007105u && address.port == 6000,
                "ParseAddress with a port");
        Require(Netplay::ParseAddress("203.0.113.5", 52130, address) && address.port == 52130, "ParseAddress default port");
        Require(!Netplay::ParseAddress("mc.okayu.zip", 52130, address), "ParseAddress took a name");
        Require(Netplay::ToString(Netplay::Address{0xC0A80114u, 52130}) == "192.168.1.20:52130", "ToString");
    }

    // Address ranges.
    Require(Netplay::IsPrivateIp(0x0A000001u), "10.0.0.1 is private");
    Require(Netplay::IsPrivateIp(0xAC100001u), "172.16.0.1 is private");
    Require(Netplay::IsPrivateIp(0xAC1FFFFFu), "172.31.255.255 is private");
    Require(!Netplay::IsPrivateIp(0xAC200001u), "172.32.0.1 is public");
    Require(Netplay::IsPrivateIp(0xC0A80001u), "192.168.0.1 is private");
    Require(Netplay::IsPrivateIp(0xA9FE0101u), "169.254.1.1 is private");
    Require(Netplay::IsPrivateIp(0x64548C20u), "100.84.140.32 (Tailscale) is private");
    Require(!Netplay::IsPrivateIp(0xCB007105u), "203.0.113.5 is public");
    Require(!Netplay::IsPrivateIp(0x08080808u), "8.8.8.8 is public");
    Require(!Netplay::IsPrivateIp(0x7F000001u), "127.0.0.1 is not a network address");
    Require(Netplay::IsTailscaleIp(0x64400001u) && !Netplay::IsTailscaleIp(0x64000001u), "Tailscale range");

    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "netplay address tests passed\n";
    return 0;
}
