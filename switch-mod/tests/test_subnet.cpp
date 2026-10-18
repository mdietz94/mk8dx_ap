// Host test: discovery sweep range derivation.
//   g++ -std=c++20 -DMK8AP_HOST_TEST switch-mod/tests/test_subnet.cpp -Iswitch-mod/src

#include <cstdio>

#include "ap/ApSubnet.hpp"

using namespace mk8ap::ap::subnet;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

constexpr std::uint32_t ip(unsigned a, unsigned b, unsigned c, unsigned d) {
    return (a << 24) | (b << 16) | (c << 8) | d;
}

int main() {
    // Typical home LAN.
    SweepRange r = sweepRange(ip(192, 168, 1, 57), ip(255, 255, 255, 0));
    CHECK(r.first == ip(192, 168, 1, 1) && r.last == ip(192, 168, 1, 254));
    CHECK(r.count() == 254);
    CHECK(r.contains(ip(192, 168, 1, 187)));
    CHECK(!r.contains(ip(192, 168, 2, 1)));

    // No mask / garbage mask -> /24.
    r = sweepRange(ip(10, 0, 5, 9), 0);
    CHECK(r.first == ip(10, 0, 5, 1) && r.last == ip(10, 0, 5, 254));
    r = sweepRange(ip(10, 0, 5, 9), ip(255, 0, 255, 0));
    CHECK(r.first == ip(10, 0, 5, 1) && r.count() == 254);

    // Wider than /24 is narrowed to the /24 holding self.
    r = sweepRange(ip(172, 16, 40, 2), ip(255, 255, 0, 0));
    CHECK(r.first == ip(172, 16, 40, 1) && r.last == ip(172, 16, 40, 254));

    // Narrower than /24 is honoured.
    r = sweepRange(ip(192, 168, 1, 200), ip(255, 255, 255, 128));
    CHECK(r.first == ip(192, 168, 1, 129) && r.last == ip(192, 168, 1, 254));
    CHECK(r.count() == 126);
    r = sweepRange(ip(192, 168, 1, 5), ip(255, 255, 255, 252));
    CHECK(r.first == ip(192, 168, 1, 5) && r.last == ip(192, 168, 1, 6));

    // Nothing to sweep.
    CHECK(sweepRange(ip(192, 168, 1, 5), ip(255, 255, 255, 254)).empty());
    CHECK(sweepRange(ip(192, 168, 1, 5), 0xFFFFFFFFu).empty());
    CHECK(sweepRange(0, ip(255, 255, 255, 0)).empty());
    CHECK(sweepRange(ip(127, 0, 0, 1), ip(255, 0, 0, 0)).empty());
    CHECK(sweepRange(ip(224, 0, 0, 5), ip(255, 255, 255, 0)).empty());
    CHECK(SweepRange{}.empty() && SweepRange{}.count() == 0);

    CHECK(isContiguousMask(ip(255, 255, 255, 0)));
    CHECK(isContiguousMask(0xFFFFFFFFu));
    CHECK(!isContiguousMask(0));
    CHECK(!isContiguousMask(ip(255, 0, 255, 0)));
    CHECK(byteswap32(0x0100A8C0u) == ip(192, 168, 0, 1));

    if (g_fail == 0) std::printf("test_subnet: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
