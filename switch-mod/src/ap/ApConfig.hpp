// Bridge config: compile-time defaults (CMake) plus the one runtime value,
// the discovery sweep seed.
//
// Nothing network-specific is compiled in. The seed is the PC's LAN address
// from `<romfs>:/ap_config.json`, which the setup wizard writes next to the
// mod and RomConfigHook reads once at nn::fs::MountRom time (the dread_ap
// scheme). One prebuilt binary therefore serves every network.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#ifndef BRIDGE_PORT_VALUE
#define BRIDGE_PORT_VALUE 17777
#endif
#ifndef BRIDGE_RETRY_MS_VALUE
#define BRIDGE_RETRY_MS_VALUE 3000
#endif
#ifndef BRIDGE_RECV_TIMEOUT_MS_VALUE
#define BRIDGE_RECV_TIMEOUT_MS_VALUE 200
#endif

namespace mk8ap::ap {

struct ApConfig {
    std::string   bridge_host;  // filled by discovery
    std::uint16_t bridge_port     = BRIDGE_PORT_VALUE;
    std::uint32_t retry_ms        = BRIDGE_RETRY_MS_VALUE;
    std::uint32_t recv_timeout_ms = BRIDGE_RECV_TIMEOUT_MS_VALUE;
};

ApConfig loadApConfig();

// The sweep seed from ap_config.json. Set once (game thread, RomConfigHook);
// read by the discovery worker. setConfiguredBridgeHost rejects empty or
// over-long values and anything after the first success.
bool setConfiguredBridgeHost(std::string_view host);
// Copies the seed into out (NUL-terminated) and returns true when one is
// set. With out == nullptr it only reports whether one is set.
bool configuredBridgeHost(char* out, std::size_t cap);

}  // namespace mk8ap::ap
