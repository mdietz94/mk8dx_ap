// Wire format for the Switch <-> PC client channel (mirror of
// apworld/mk8dx_archipelago/client/protocol.py; spec in docs/wire-protocol.md).
//
// One persistent TCP connection, one '\n'-terminated UTF-8 JSON object per
// message, field "t" is the type. Max line 8 KiB.
//
// All structs use fixed char[] buffers (no std::string) so every encode /
// decode path is allocation-free on both the worker and frame threads.

#pragma once

#include <cstddef>
#include <cstdint>

#include "../util/Json.hpp"

namespace mk8ap::ap {

inline constexpr std::size_t kMaxLineBytes = 8 * 1024;
inline constexpr std::size_t kShortCap = 32;
inline constexpr std::size_t kMedCap = 64;
inline constexpr std::size_t kLongCap = 256;
inline constexpr std::size_t kPrintCap = 512;

template <std::size_t N>
inline void copyFixed(char (&dst)[N], const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    std::size_t i = 0;
    while (i + 1 < N && src[i] != '\0') { dst[i] = src[i]; ++i; }
    dst[i] = '\0';
}

template <std::size_t N>
inline void copyFixedN(char (&dst)[N], const char* src, std::size_t n) {
    const std::size_t take = (n < N - 1) ? n : (N - 1);
    for (std::size_t i = 0; i < take; ++i) dst[i] = src[i];
    dst[take] = '\0';
}

// ---- Switch -> PC -----------------------------------------------------------

struct Hello {
    char mod_ver[kShortCap] = {};
    char game_ver[kShortCap] = {};
    char build_id[40] = {};
    int resolved = 0;
    int total = 0;
    char device_id[kMedCap] = {};
};

// Race finished, from the in-game hook path (ui::RaceWindow::onFakeGoal).
struct RaceResult {
    int seq = 0;
    int course_id = -1;
    int rule = -1;         // gear::ERaceRule
    int engine = -1;       // gear::EEngineLevelSlot
    bool mirror = false;
    int rank = -1;         // 0-based finishing position
    int kart_index = -1;
};

// Serialized nn::prepo PlayReport captured at the IPC layer. Carries the
// game's own end-of-race telemetry (Course / Rank / Rule / Engine / Driver /
// Body / Tire / Wing / FinishReason ...) as a MessagePack blob; the PC
// decodes it. Hex-encoded to stay inside the JSON line.
inline constexpr std::size_t kPlayReportMaxBytes = 3000;
struct PlayReport {
    char room[kMedCap] = {};
    std::uint8_t payload[kPlayReportMaxBytes] = {};
    std::size_t size = 0;
    bool truncated = false;
};

// Learned id -> name pairs. The PC owns the id tables; we just report what
// the game tells us so the mapping is self-calibrating across versions.
struct CourseName {
    int course_id = -1;
    char res[kMedCap] = {};  // CourseInfo::mCourseIconResource
};

struct IdEntry {
    int id = -1;
    char text[kShortCap] = {};
};

struct IdTable {
    static constexpr std::size_t kMaxEntries = 64;
    char kind[8] = {};  // "driver" | "body" | "tire" | "wing"
    IdEntry entries[kMaxEntries] = {};
    std::size_t count = 0;
};

struct Log {
    char level[8] = {};
    char msg[kLongCap] = {};
};

struct Ping {
    std::int64_t ts_ms = 0;
};

void encodeHello(util::json::LineBuffer&, const Hello&);
void encodeRaceResult(util::json::LineBuffer&, const RaceResult&);
void encodePlayReport(util::json::LineBuffer&, const PlayReport&);
void encodeCourseName(util::json::LineBuffer&, const CourseName&);
void encodeIdTable(util::json::LineBuffer&, const IdTable&);
void encodeLog(util::json::LineBuffer&, const Log&);
void encodePing(util::json::LineBuffer&, const Ping&);

// ---- PC -> Switch -----------------------------------------------------------

struct HelloAck {
    bool ok = false;
    char seed[kMedCap] = {};
    char slot[kMedCap] = {};
    char client_ver[kShortCap] = {};
    char err[kLongCap] = {};
};

// Full restriction snapshot. Idempotent: the PC re-sends the whole thing on
// every change and after every HELLO, so there is no item replay to get
// wrong. Bit i of a mask = id i allowed.
struct State {
    int seq = 0;
    bool enabled = false;       // false = play vanilla (no restrictions)
    std::uint32_t cups = 0xFFFFFFFFu;       // bit = ECup - 1 (0..23)
    std::uint32_t drivers[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::uint32_t bodies[2] = {0xFFFFFFFFu, 0xFFFFFFFFu};
    std::uint32_t tires = 0xFFFFFFFFu;
    std::uint32_t wings = 0xFFFFFFFFu;
    std::uint32_t items = 0xFFFFFFFFu;      // bit = gear::EItemSlot
    int speed_pct = 100;        // 1..100
    int engine_max = 3;         // highest allowed EEngineLevelSlot (informational)
    int def_driver = 0;         // substitutes for locked selections
    int def_body = 0;
    int def_tire = 0;
    int def_wing = 0;
    bool cpu_items = false;     // also filter CPU item rolls
};

struct Print {
    char text[kPrintCap] = {};
};

struct Pong {
    std::int64_t ts_ms = 0;
};

struct Err {
    char code[kMedCap] = {};
    char ctx[kLongCap] = {};
};

struct DecodedMsg {
    char t[kShortCap] = {};
    HelloAck hello_ack{};
    State state{};
    Print print{};
    Pong pong{};
    Err err{};
};

// `data` must be writable (string escapes are decoded in place).
bool decode(char* data, std::size_t len, DecodedMsg& out);

}  // namespace mk8ap::ap
