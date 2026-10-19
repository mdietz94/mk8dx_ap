// Runtime bridge discovery via UDP.
//
// Every (re)connect cycle, ApClient calls `resolveBridge()` before TCP
// connect. Probe order:
//
//   1. UDP probe -> 127.0.0.1:<port> — covers Ryujinx running on the
//      same host as the MK8DX Client.
//   2. UDP unicast sweep over the /24 of the PC address the setup wizard
//      wrote to ap_config.json in the mod's romfs layer (read once at
//      nn::fs::MountRom time by RomConfigHook; the dread_ap scheme). Every
//      .1..254 host gets one probe, fired as a tight burst on a single
//      socket. First reply wins. Replaces the 255.255.255.255 broadcast
//      that travel routers, mesh Wi-Fi extenders, and some VLAN'd setups
//      silently drop. No config file means loopback only.
//
// On success, fills `out` with the bridge's advertised TCP host:port.
// On total failure, returns false; the caller retries the whole loop
// with its existing exponential backoff. Nothing about the player's
// network is compiled in, which is what lets the release ship one
// prebuilt binary.
//
// Discovery's last sweep + last result are also stashed in the static
// resolveBridge() report so the on-Switch debug overlay can show what
// the radio actually tried.
//
// M6.1-safe layout: fixed `char[]` buffers, no std::string growth or
// std::vector. Socket lifetime is one resolveBridge() call.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ApClient.hpp"  // BridgeTarget

namespace mk8ap::ap {

#ifdef DISCOVERY_PORT_VALUE
inline constexpr std::uint16_t kDefaultDiscoveryPort = DISCOVERY_PORT_VALUE;
#else
inline constexpr std::uint16_t kDefaultDiscoveryPort = 17776;
#endif

// Snapshot of the most recent resolveBridge() call. Read by the on-Switch
// debug overlay (ui::ApDebugConsole) — purely diagnostic, no logic gates
// on it. Updated by resolveBridge() atomically by spinlock.
struct DiscoveryReport {
    std::uint32_t self_ip       = 0;  // sweep seed (ap_config.json), host byte order (a.b.c.d -> (a<<24)|b<<16|c<<8|d); 0 = none
    std::uint32_t subnet_mask   = 0;  // always /24 when a seed is set
    std::uint16_t probed_count  = 0;  // number of unicast hosts swept on last call
    std::uint16_t replies       = 0;  // number of valid {"t":"bridge",...} replies seen
    char          last_bridge_host[40] = {0};  // last successful reply's host:port
    std::uint16_t last_bridge_port     = 0;
    std::int64_t  last_attempt_ms      = 0;
    std::int64_t  last_success_ms      = 0;
    bool          loopback_used        = false;
};

// Pull a copy of the most recent discovery report. Safe to call from
// any thread.
void snapshotDiscoveryReport(DiscoveryReport& out);

// Attempt to discover the bridge's TCP host:port. See header comment for
// the probe order. Returns true when a `{"t":"bridge", host, port}` reply
// was received within the timeouts; false otherwise. `out` is filled on
// success and left untouched on failure.
bool resolveBridge(
    BridgeTarget& out,
    std::uint16_t discovery_port = kDefaultDiscoveryPort);

}  // namespace mk8ap::ap
