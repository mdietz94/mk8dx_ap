#include "ApFrameBridge.hpp"

#include <atomic>
#include <cstring>

#include "ApState.hpp"
#include "../util/Log.hpp"

namespace mk8ap::ap {

namespace {

// outbound_logs producer-side spinlock: the ring is single-producer but
// log() can be called from any thread.
std::atomic_flag g_logs_push_lock = ATOMIC_FLAG_INIT;
}  // namespace

void reportRaceResult(int course_id, int rule, int engine, bool mirror,
                      int rank, int kart_index) {
    auto& st = ApState::instance();
    RaceResult r{};
    r.seq = st.next_seq.fetch_add(1, std::memory_order_relaxed);
    r.course_id = course_id;
    r.rule = rule;
    r.engine = engine;
    r.mirror = mirror;
    r.rank = rank;
    r.kart_index = kart_index;
    if (!st.outbound_results.push(r)) {
        MK8AP_LOG_WARN("[bridge] outbound_results FULL; dropped race_result seq=%d", r.seq);
    }
}

void reportPlayReport(const char* room, std::size_t room_len,
                      const void* payload, std::size_t size) {
    auto& st = ApState::instance();
    // PlayReport is ~3 KiB; keep it static to stay off the hook thread's stack.
    // Producer is the prepo IPC hook (one thread at a time in practice); the
    // ring copy happens under the same call so a static staging buffer is
    // safe enough for our purposes.
    static PlayReport pr;
    pr = PlayReport{};
    if (room && room_len) copyFixedN(pr.room, room, room_len);
    const std::size_t take = size < kPlayReportMaxBytes ? size : kPlayReportMaxBytes;
    if (payload && take) std::memcpy(pr.payload, payload, take);
    pr.size = take;
    pr.truncated = size > kPlayReportMaxBytes;
    if (!st.outbound_reports.push(pr)) {
        MK8AP_LOG_WARN("[bridge] outbound_reports FULL; dropped play_report room=%s", pr.room);
    }
}

void reportIdTable(const IdTable& tbl) {
    if (!ApState::instance().outbound_tables.push(tbl)) {
        MK8AP_LOG_WARN("[bridge] outbound_tables FULL; dropped %s table", tbl.kind);
    }
}

void enqueueRemoteLog(const char* level, const char* msg) {
    auto& st = ApState::instance();
    if (st.conn.load(std::memory_order_relaxed) == ConnState::Disconnected) return;
    Log lg;
    copyFixed(lg.level, level);
    copyFixed(lg.msg, msg);
    while (g_logs_push_lock.test_and_set(std::memory_order_acquire)) {}
    const bool ok = st.outbound_logs.push(lg);
    g_logs_push_lock.clear(std::memory_order_release);
    if (!ok) st.log_drops.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace mk8ap::ap
