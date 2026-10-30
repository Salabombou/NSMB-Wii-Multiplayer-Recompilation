#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Network play, the lobby: rooms before a session starts. A host opens a room on UDP port 52130 (or
// the port picked in Config.toml's [netplay] host_port); other devices find it by broadcasting on every
// local network and by asking Tailscale devices
// directly, since Tailscale carries no broadcasts: the PC's `tailscale status` peers and the known
// devices (KnownDevices). Hosts also announce their room to those devices, and phones listen on
// the room port while looking, so a phone finds a room without knowing its host. Every room answer
// and search passes on the Tailscale devices the sender knows, so all devices on a tailnet that
// run the game learn each other through any one they meet. Direct Connect (Connect) skips the
// search: the player types the host's address or name and port, which is looked up and asked for
// its room, the way to play over the internet with a host whose router forwards the port. Starting
// the room hands every member the host's save, waits until all of them have stored it, and tells
// everyone to restart into the session together (netplay_start.h).
namespace NetplayLobby {

enum class Phase {
    Off,
    Hosting,     // a room is open
    Browsing,    // looking for rooms
    Joining,     // asked a host to join
    InRoom,      // a member of someone's room
    Starting,    // host: handing out the save and waiting for everyone; member: receiving it
    Restarting,  // everyone is ready: restart now
    Failed,
    // Direct Connect: looking the typed name up, then asking its address for the room. Last, because
    // members send their phase to the host (kKeepAlive) and the values above must not move.
    Connecting,
};

struct Member {
    uint8_t slot = 0;
    std::string name;
};

struct Room {
    uint64_t id = 0;
    std::string name;
    std::string host;
    uint8_t maxPlayers = 0;
    uint8_t players = 0;
    std::string address;  // ip:port
    bool tailscale = false;
    bool full = false;
};

struct Snapshot {
    Phase phase = Phase::Off;
    std::string message;  // what went wrong, or what is happening
    std::string roomName;
    uint8_t maxPlayers = 0;
    uint8_t localSlot = 0;
    uint16_t hostPort = 0;        // host: the UDP port its room is on
    bool direct = false;          // reached through Direct Connect (Connect)
    std::vector<Member> members;  // slot order
    std::vector<Room> rooms;      // while browsing, newest first
};
Snapshot Get();

// Opens a room for `maxPlayers` (2-4) on HostPort(). `saveFile` is the save slot (0-2) the host picked.
void Host(uint8_t maxPlayers, int saveFile);
void Browse();
void Join(uint64_t roomId);
// Direct Connect: `address` is what the player typed, "host" or "host:port" (port 52130 when none),
// host an IPv4 address or a name such as mc.okayu.zip. The name is looked up off this thread, every
// address it stands for is asked for its room, and the first room that answers is joined. Goes
// through Connecting to Joining, or to Failed with a message saying why.
void Connect(const std::string& address);
// Host: start the session with everyone in the room.
void Start();
// Leave the room, close it, or stop looking.
void Stop();

// The UDP port rooms are hosted on: Config.toml's [netplay] host_port, else 52130.
uint16_t HostPort();
// The address the last successful Direct Connect used, as typed ("" before the first): Netplay/direct.txt.
std::string LastDirectAddress();

// What a restart into the session needs; filled once the phase reaches Restarting.
struct Plan {
    uint64_t sessionId = 0;
    uint8_t localSlot = 0;
    uint8_t players = 0;
    uint32_t inputDelay = 3;
    std::string host;  // clients: the host's ip:port as this device reached it
    uint16_t port = 0; // host: the UDP port its room was on, which the session keeps (forwarded routers)
    int saveFile = 0;
    std::vector<std::string> names;
    std::map<std::string, std::vector<uint8_t>> nandFiles;  // NAND path -> bytes
};
bool TakePlan(Plan& out);

// Devices asked for rooms directly: Netplay/hosts.txt, one Tailscale name (MagicDNS) or address
// (with :port when the room isn't on 52130) per line. Filled by playing together on a local network
// or tailnet, by other devices passing on the ones they know, and by hand.
std::vector<std::string> KnownDevices();
// False when it is already known or isn't a name or address.
bool AddKnownDevice(const std::string& nameOrAddress);
void RemoveKnownDevice(const std::string& nameOrAddress);
// This device's addresses, Tailscale first: what another player types in to find it.
std::vector<std::string> OwnAddresses();

} // namespace NetplayLobby
