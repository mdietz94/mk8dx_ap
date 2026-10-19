// Pure IPv4 helpers for the discovery sweep. No SDK types, so the host
// tests (switch-mod/tests/test_subnet.cpp) compile them as-is.
//
// All addresses here are HOST byte order: a.b.c.d == (a<<24)|(b<<16)|(c<<8)|d.

#pragma once

#include <cstdint>

namespace mk8ap::ap::subnet {

// Inclusive range of host addresses to probe. Empty when first > last.
struct SweepRange {
    std::uint32_t first = 1;
    std::uint32_t last  = 0;

    constexpr bool empty() const { return first > last; }
    constexpr std::uint32_t count() const { return empty() ? 0 : last - first + 1; }
    constexpr bool contains(std::uint32_t ip) const { return !empty() && ip >= first && ip <= last; }
};

constexpr std::uint32_t kMask24 = 0xFFFFFF00u;

constexpr std::uint32_t byteswap32(std::uint32_t v) {
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8)  | ((v & 0xFF000000u) >> 24);
}

// A netmask is a run of leading ones followed only by zeros.
constexpr bool isContiguousMask(std::uint32_t mask) {
    if (mask == 0) return false;
    const std::uint32_t inv = ~mask;
    return (inv & (inv + 1)) == 0;
}

// An address worth sweeping around: not 0.0.0.0, not loopback, not
// multicast/reserved, not the limited broadcast.
constexpr bool isUsableHost(std::uint32_t ip) {
    const std::uint32_t a = ip >> 24;
    return ip != 0 && a != 0 && a != 127 && a < 224;
}

// Hosts to probe for an interface with address `self` and netmask `mask`.
// A missing or malformed mask means /24. A mask wider than /24 (a /16
// office or campus LAN) is narrowed to the /24 that contains `self`: 65k
// probes per retry is not a sweep, and the PC is almost always in the same
// /24. A mask narrower than /24 is honoured. The network and broadcast
// addresses are skipped; /31 and /32 leave nothing to sweep.
constexpr SweepRange sweepRange(std::uint32_t self, std::uint32_t mask) {
    if (!isUsableHost(self)) return {};
    if (!isContiguousMask(mask) || mask < kMask24) mask = kMask24;
    const std::uint32_t net   = self & mask;
    const std::uint32_t bcast = net | ~mask;
    if (bcast - net < 2) return {};
    return {net + 1, bcast - 1};
}

}  // namespace mk8ap::ap::subnet
