// Bridge config — defaults baked in at compile time via CMake.
//
// Nothing here is network-specific: the mod finds the PC by UDP discovery
// over loopback and the console's own subnet (see ApDiscovery.cpp), so one
// build works on every LAN and the release ships it prebuilt.
// bridge_host is an OPTIONAL developer seed (-DBRIDGE_HOST=<ip>): when set,
// its /24 is swept as well, for a PC on a different subnet than the console.

#pragma once

#include <cstdint>
#include <string>

#ifndef BRIDGE_HOST_STRING
#define BRIDGE_HOST_STRING ""
#endif
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
    std::string   bridge_host     = BRIDGE_HOST_STRING;
    std::uint16_t bridge_port     = BRIDGE_PORT_VALUE;
    std::uint32_t retry_ms        = BRIDGE_RETRY_MS_VALUE;
    std::uint32_t recv_timeout_ms = BRIDGE_RECV_TIMEOUT_MS_VALUE;
};

ApConfig loadApConfig();

}  // namespace mk8ap::ap
