#pragma once

#include <string>

// Network play: this device's public IPv4 address, as the internet sees it, for a host to tell Direct
// Connect players (netplay_lobby.h). Asked of a web service (api.ipify.org, else ipv4.icanhazip.com)
// in the background, only when asked for: while hosting a room, or from the LAN Play settings.
namespace PublicIp {

enum class State { Idle, Looking, Known, Failed };

struct Result {
    State state = State::Idle;
    std::string ip;  // "a.b.c.d" once Known
};

// Starts a lookup unless one is running, or one succeeded in the last ten minutes (`force`: look
// again anyway). Returns at once.
void Request(bool force = false);
Result Get();

} // namespace PublicIp
