#include "ApProtocol.hpp"

#include <cstring>

namespace mk8ap::ap {

using util::json::Encoder;
using util::json::LineBuffer;
using util::json::Reader;

namespace {

void finish(LineBuffer& line) { line.append('\n'); }

}  // namespace

void encodeHello(LineBuffer& line, const Hello& h) {
    line.clear();
    Encoder e{line};
    e.beginObject()
        .key("t").value("hello")
        .key("mod_ver").value(h.mod_ver)
        .key("game_ver").value(h.game_ver)
        .key("build_id").value(h.build_id)
        .key("resolved").value(h.resolved)
        .key("total").value(h.total)
        .key("unresolved").value(h.unresolved)
        .key("device_id").value(h.device_id)
     .endObject();
    finish(line);
}

void encodeRaceResult(LineBuffer& line, const RaceResult& r) {
    line.clear();
    Encoder e{line};
    e.beginObject()
        .key("t").value("race_result")
        .key("seq").value(r.seq)
        .key("course_id").value(r.course_id)
        .key("rule").value(r.rule)
        .key("engine").value(r.engine)
        .key("mirror").value(r.mirror)
        .key("rank").value(r.rank)
        .key("kart_index").value(r.kart_index)
     .endObject();
    finish(line);
}

void encodePlayReport(LineBuffer& line, const PlayReport& p) {
    line.clear();
    Encoder e{line};
    e.beginObject()
        .key("t").value("play_report")
        .key("room").value(p.room)
        .key("size").value(static_cast<std::int64_t>(p.size))
        .key("truncated").value(p.truncated)
        .key("hex");
    // Hand-append the hex payload (Encoder::value would copy through a
    // string_view; appending directly avoids a 6 KiB stack temp).
    static constexpr char kHex[] = "0123456789abcdef";
    line.append('"');
    for (std::size_t i = 0; i < p.size && i < kPlayReportMaxBytes; ++i) {
        line.append(kHex[p.payload[i] >> 4]);
        line.append(kHex[p.payload[i] & 0xF]);
    }
    line.append('"');
    e.endObject();
    finish(line);
}

void encodeIdTable(LineBuffer& line, const IdTable& tbl) {
    line.clear();
    Encoder e{line};
    e.beginObject()
        .key("t").value("id_table")
        .key("kind").value(tbl.kind)
        .key("entries").beginArray();
    for (std::size_t i = 0; i < tbl.count && i < IdTable::kMaxEntries; ++i) {
        e.beginObject()
            .key("id").value(tbl.entries[i].id)
            .key("text").value(tbl.entries[i].text)
         .endObject();
    }
    e.endArray().endObject();
    finish(line);
}

void encodeLog(LineBuffer& line, const Log& l) {
    line.clear();
    Encoder e{line};
    e.beginObject()
        .key("t").value("log")
        .key("level").value(l.level)
        .key("msg").value(l.msg)
     .endObject();
    finish(line);
}

void encodePing(LineBuffer& line, const Ping& p) {
    line.clear();
    Encoder e{line};
    e.beginObject()
        .key("t").value("ping")
        .key("ts_ms").value(p.ts_ms)
     .endObject();
    finish(line);
}

// ---- decode -----------------------------------------------------------------

namespace {

bool readMaskArray(Reader& r, std::uint32_t* out, std::size_t n) {
    if (!r.enterArray()) return false;
    std::size_t i = 0;
    while (r.hasMoreInArray()) {
        std::int64_t v = 0;
        if (!r.nextInt(v)) return false;
        if (i < n) out[i] = static_cast<std::uint32_t>(v);
        ++i;
    }
    return r.exitArray();
}

bool decodeHelloAck(Reader& r, HelloAck& out) {
    std::string_view key;
    while (r.nextField(key)) {
        std::string_view sv;
        std::int64_t iv = 0;
        bool bv = false;
        if (key == "ok") { if (!r.nextBool(bv)) return false; out.ok = bv; }
        else if (key == "seed") { if (!r.nextString(sv)) return false; copyFixedN(out.seed, sv.data(), sv.size()); }
        else if (key == "slot") { if (!r.nextString(sv)) return false; copyFixedN(out.slot, sv.data(), sv.size()); }
        else if (key == "client_ver") { if (!r.nextString(sv)) return false; copyFixedN(out.client_ver, sv.data(), sv.size()); }
        else if (key == "err") { if (!r.nextString(sv)) return false; copyFixedN(out.err, sv.data(), sv.size()); }
        else if (key == "t") { if (!r.nextString(sv)) return false; }
        else { (void)iv; if (!r.skipValue()) return false; }
    }
    return true;
}

bool decodeState(Reader& r, State& out) {
    std::string_view key;
    while (r.nextField(key)) {
        std::string_view sv;
        std::int64_t iv = 0;
        bool bv = false;
        if (key == "t") { if (!r.nextString(sv)) return false; }
        else if (key == "seq") { if (!r.nextInt(iv)) return false; out.seq = static_cast<int>(iv); }
        else if (key == "enabled") { if (!r.nextBool(bv)) return false; out.enabled = bv; }
        else if (key == "cups") { if (!r.nextInt(iv)) return false; out.cups = static_cast<std::uint32_t>(iv); }
        else if (key == "drivers") { if (!readMaskArray(r, out.drivers, 4)) return false; }
        else if (key == "bodies") { if (!readMaskArray(r, out.bodies, 2)) return false; }
        else if (key == "tires") { if (!r.nextInt(iv)) return false; out.tires = static_cast<std::uint32_t>(iv); }
        else if (key == "wings") { if (!r.nextInt(iv)) return false; out.wings = static_cast<std::uint32_t>(iv); }
        else if (key == "items") { if (!r.nextInt(iv)) return false; out.items = static_cast<std::uint32_t>(iv); }
        else if (key == "speed_pct") { if (!r.nextInt(iv)) return false; out.speed_pct = static_cast<int>(iv); }
        else if (key == "engine_max") { if (!r.nextInt(iv)) return false; out.engine_max = static_cast<int>(iv); }
        else if (key == "def_driver") { if (!r.nextInt(iv)) return false; out.def_driver = static_cast<int>(iv); }
        else if (key == "def_body") { if (!r.nextInt(iv)) return false; out.def_body = static_cast<int>(iv); }
        else if (key == "def_tire") { if (!r.nextInt(iv)) return false; out.def_tire = static_cast<int>(iv); }
        else if (key == "def_wing") { if (!r.nextInt(iv)) return false; out.def_wing = static_cast<int>(iv); }
        else if (key == "cpu_items") { if (!r.nextBool(bv)) return false; out.cpu_items = bv; }
        else { if (!r.skipValue()) return false; }
    }
    if (out.speed_pct < 1) out.speed_pct = 1;
    if (out.speed_pct > 100) out.speed_pct = 100;
    return true;
}

bool decodePrint(Reader& r, Print& out) {
    std::string_view key;
    while (r.nextField(key)) {
        std::string_view sv;
        if (key == "text") { if (!r.nextString(sv)) return false; copyFixedN(out.text, sv.data(), sv.size()); }
        else if (key == "t") { if (!r.nextString(sv)) return false; }
        else { if (!r.skipValue()) return false; }
    }
    return true;
}

bool decodePong(Reader& r, Pong& out) {
    std::string_view key;
    while (r.nextField(key)) {
        std::string_view sv;
        std::int64_t iv = 0;
        if (key == "ts_ms") { if (!r.nextInt(iv)) return false; out.ts_ms = iv; }
        else if (key == "t") { if (!r.nextString(sv)) return false; }
        else { if (!r.skipValue()) return false; }
    }
    return true;
}

bool decodeErr(Reader& r, Err& out) {
    std::string_view key;
    while (r.nextField(key)) {
        std::string_view sv;
        if (key == "code") { if (!r.nextString(sv)) return false; copyFixedN(out.code, sv.data(), sv.size()); }
        else if (key == "ctx") { if (!r.nextString(sv)) return false; copyFixedN(out.ctx, sv.data(), sv.size()); }
        else if (key == "t") { if (!r.nextString(sv)) return false; }
        else { if (!r.skipValue()) return false; }
    }
    return true;
}

// Peek the "t" discriminator without consuming: scan for the first
// occurrence of "t":" and copy the value. The bodies are then decoded with
// a fresh Reader so field order does not matter.
bool peekType(const char* data, std::size_t len, char (&out)[kShortCap]) {
    const char* needle = "\"t\":";
    const std::size_t nl = std::strlen(needle);
    for (std::size_t i = 0; i + nl < len; ++i) {
        if (std::memcmp(data + i, needle, nl) != 0) continue;
        std::size_t j = i + nl;
        while (j < len && (data[j] == ' ')) ++j;
        if (j >= len || data[j] != '"') return false;
        ++j;
        std::size_t k = 0;
        while (j < len && data[j] != '"' && k + 1 < kShortCap) out[k++] = data[j++];
        out[k] = '\0';
        return j < len;
    }
    return false;
}

}  // namespace

bool decode(char* data, std::size_t len, DecodedMsg& out) {
    if (!data || len == 0 || len > kMaxLineBytes) return false;
    if (!peekType(data, len, out.t)) return false;
    Reader r(data, len);
    if (!r.enterObject()) return false;
    auto eq = [&](const char* s) { return std::strcmp(out.t, s) == 0; };
    bool ok;
    if (eq("hello_ack")) { out.hello_ack = HelloAck{}; ok = decodeHelloAck(r, out.hello_ack); }
    else if (eq("state")) { out.state = State{}; ok = decodeState(r, out.state); }
    else if (eq("print")) { out.print = Print{}; ok = decodePrint(r, out.print); }
    else if (eq("pong")) { out.pong = Pong{}; ok = decodePong(r, out.pong); }
    else if (eq("err")) { out.err = Err{}; ok = decodeErr(r, out.err); }
    else {
        // Unknown type: consume the object so the caller can log it.
        std::string_view key;
        ok = true;
        while (r.nextField(key)) { if (!r.skipValue()) { ok = false; break; } }
    }
    return ok && r.exitObject();
}

}  // namespace mk8ap::ap
