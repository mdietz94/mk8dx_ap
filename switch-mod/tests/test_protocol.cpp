// Host test: wire encode/decode round trips for the MK8DX message set.
//   g++ -std=c++20 -DMK8AP_HOST_TEST switch-mod/tests/test_protocol.cpp \
//       switch-mod/src/ap/ApProtocol.cpp switch-mod/src/util/Json.cpp -Iswitch-mod/src

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

#include "ap/ApProtocol.hpp"

using namespace mk8ap::ap;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool decodeStr(const std::string& s, DecodedMsg& m) {
    std::string copy = s;
    return decode(copy.data(), copy.size(), m);
}

int main() {
    // hello
    {
        Hello h{};
        copyFixed(h.mod_ver, "0.1.0");
        copyFixed(h.game_ver, "4.0.1");
        copyFixed(h.build_id, "158C0C7399AABD7BF83795C2EF3484C7");
        copyFixed(h.unresolved, "ui::SetNextCourse");
        h.resolved = 16; h.total = 17;
        mk8ap::util::json::LineBuffer line;
        encodeHello(line, h);
        std::string s(line.data(), line.size());
        CHECK(s.find("\"t\":\"hello\"") != std::string::npos);
        CHECK(s.find("\"resolved\":16") != std::string::npos);
        CHECK(s.find("\"unresolved\":\"ui::SetNextCourse\"") != std::string::npos);
        CHECK(s.back() == '\n');
    }
    // race_result
    {
        RaceResult r{}; r.seq = 3; r.course_id = 12; r.rule = 1; r.engine = 2; r.mirror = true; r.rank = 0; r.kart_index = 0;
        mk8ap::util::json::LineBuffer line;
        encodeRaceResult(line, r);
        std::string s(line.data(), line.size());
        CHECK(s.find("\"t\":\"race_result\"") != std::string::npos);
        CHECK(s.find("\"mirror\":true") != std::string::npos);
        CHECK(s.find("\"rank\":0") != std::string::npos);
    }
    // play_report hex
    {
        PlayReport p{};
        copyFixed(p.room, "RaceResult");
        p.payload[0] = 0xDE; p.payload[1] = 0x00; p.payload[2] = 0x01; p.size = 3;
        mk8ap::util::json::LineBuffer line;
        encodePlayReport(line, p);
        std::string s(line.data(), line.size());
        CHECK(s.find("\"hex\":\"de0001\"") != std::string::npos);
        CHECK(s.find("\"room\":\"RaceResult\"") != std::string::npos);
    }
    // id_table
    {
        IdTable t{};
        copyFixed(t.kind, "driver");
        t.entries[0].id = 0; copyFixed(t.entries[0].text, "Mario");
        t.entries[1].id = 1; copyFixed(t.entries[1].text, "Luigi");
        t.count = 2;
        mk8ap::util::json::LineBuffer line;
        encodeIdTable(line, t);
        std::string s(line.data(), line.size());
        CHECK(s.find("{\"id\":1,\"text\":\"Luigi\"}") != std::string::npos);
    }
    // decode hello_ack
    {
        DecodedMsg m;
        CHECK(decodeStr("{\"t\":\"hello_ack\",\"ok\":true,\"seed\":\"abc\",\"slot\":\"Mario\",\"client_ver\":\"0.1.0\",\"extra\":{\"x\":[1,2]}}", m));
        CHECK(std::strcmp(m.t, "hello_ack") == 0);
        CHECK(m.hello_ack.ok);
        CHECK(std::strcmp(m.hello_ack.slot, "Mario") == 0);
    }
    // decode state (field order independent, unknown fields skipped)
    {
        DecodedMsg m;
        CHECK(decodeStr("{\"seq\":7,\"enabled\":true,\"cups\":5,\"drivers\":[1,2,3,4],\"bodies\":[9,8],"
                        "\"tires\":255,\"wings\":15,\"items\":32769,\"speed_pct\":60,\"engine_max\":2,"
                        "\"def_driver\":0,\"def_body\":1,\"def_tire\":2,\"def_wing\":3,\"cpu_items\":false,"
                        "\"future\":\"x\",\"t\":\"state\"}", m));
        CHECK(std::strcmp(m.t, "state") == 0);
        CHECK(m.state.seq == 7);
        CHECK(m.state.enabled);
        CHECK(m.state.cups == 5);
        CHECK(m.state.drivers[3] == 4);
        CHECK(m.state.bodies[1] == 8);
        CHECK(m.state.items == 32769);
        CHECK(m.state.speed_pct == 60);
        CHECK(m.state.def_wing == 3);
    }
    // speed clamp + defaults
    {
        DecodedMsg m;
        CHECK(decodeStr("{\"t\":\"state\",\"speed_pct\":500}", m));
        CHECK(m.state.speed_pct == 100);
        CHECK(m.state.cups == 0xFFFFFFFFu);
        CHECK(!m.state.enabled);
    }
    // print / pong / err / unknown / malformed
    {
        DecodedMsg m;
        CHECK(decodeStr("{\"t\":\"print\",\"text\":\"hi \\\"there\\\"\"}", m));
        CHECK(std::strcmp(m.print.text, "hi \"there\"") == 0);
        CHECK(decodeStr("{\"t\":\"pong\",\"ts_ms\":123456789012}", m));
        CHECK(m.pong.ts_ms == 123456789012LL);
        CHECK(decodeStr("{\"t\":\"err\",\"code\":\"busy\",\"ctx\":\"x\"}", m));
        CHECK(std::strcmp(m.err.code, "busy") == 0);
        CHECK(decodeStr("{\"t\":\"whatever\",\"a\":{\"b\":[1,{\"c\":2}]}}", m));
        CHECK(std::strcmp(m.t, "whatever") == 0);
        CHECK(!decodeStr("{\"t\":\"state\",\"cups\":", m));
        CHECK(!decodeStr("not json", m));
    }
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("test_protocol OK\n");
    return 0;
}
