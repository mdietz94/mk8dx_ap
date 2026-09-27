// Module-resident state shared between the socket worker and the game
// threads (frame thread + whichever thread a hook fires on).
//
//   worker -> hooks : restriction snapshot (atomics; last write wins)
//   hooks  -> worker: outbound events (SPSC rings; hooks push, worker pops)
//
// Everything is fixed-size and allocation-free.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ApProtocol.hpp"

namespace mk8ap::ap {

enum class ConnState : std::uint8_t { Disconnected, Connecting, Hello, Ready };

// Single-producer single-consumer ring. Producer-side callers that can run
// on several threads (logging) wrap push() in their own spinlock.
template <typename T, std::size_t N>
class SpscRing {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(const T& v) {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        if (h - t >= N) return false;
        buf_[h & (N - 1)] = v;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    // Copy out the oldest element without removing it.
    bool peek(T& out) const {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (h == t) return false;
        out = buf_[t & (N - 1)];
        return true;
    }
    const T* peekRef() const {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        const std::size_t h = head_.load(std::memory_order_acquire);
        if (h == t) return nullptr;
        return &buf_[t & (N - 1)];
    }
    void popDiscard() {
        tail_.store(tail_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }
    bool pop(T& out) {
        if (!peek(out)) return false;
        popDiscard();
        return true;
    }
    std::size_t pendingApprox() const {
        return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_relaxed);
    }
private:
    T buf_[N]{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

class ApState {
public:
    static ApState& instance();

    // ---- connection --------------------------------------------------------
    std::atomic<ConnState> conn{ConnState::Disconnected};
    std::atomic<bool> bridge_ready{false};   // hello_ack ok received
    char local_slot[kMedCap] = {};
    char seed[kMedCap] = {};

    // ---- restriction snapshot (worker writes, hooks read) ------------------
    // Defaults = "everything allowed" so the game plays vanilla until the PC
    // client has sent a state (enabled=false also means vanilla).
    std::atomic<bool> enabled{false};
    std::atomic<int> state_seq{0};
    std::atomic<std::uint32_t> cups{0xFFFFFFFFu};
    std::atomic<std::uint32_t> drivers[4]{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::atomic<std::uint32_t> bodies[2]{0xFFFFFFFFu, 0xFFFFFFFFu};
    std::atomic<std::uint32_t> tires{0xFFFFFFFFu};
    std::atomic<std::uint32_t> wings{0xFFFFFFFFu};
    std::atomic<std::uint32_t> items{0xFFFFFFFFu};
    std::atomic<int> speed_pct{100};
    std::atomic<int> engine_max{3};
    std::atomic<int> def_driver{0};
    std::atomic<int> def_body{0};
    std::atomic<int> def_tire{0};
    std::atomic<int> def_wing{0};
    std::atomic<bool> cpu_items{false};

    void applyState(const State& s);

    bool restricting() const { return enabled.load(std::memory_order_relaxed); }
    bool isCupAllowed(int cupIdx) const;      // 0..23
    bool isDriverAllowed(int id) const;      // 0..127
    bool isBodyAllowed(int id) const;        // 0..63
    bool isTireAllowed(int id) const;        // 0..31
    bool isWingAllowed(int id) const;        // 0..31
    bool isItemAllowed(int itemSlot) const;  // gear::EItemSlot 0..22

    // ---- outbound rings (hooks push, worker pops) ---------------------------
    SpscRing<RaceResult, 16> outbound_results;
    SpscRing<PlayReport, 4> outbound_reports;
    SpscRing<IdTable, 8> outbound_tables;
    SpscRing<Log, 256> outbound_logs;
    std::atomic<std::uint32_t> log_drops{0};
    std::atomic<int> next_seq{1};

    // Set once the save-load hook has started networking (so a second save
    // load does not spawn a second worker).
    std::atomic<bool> networking_started{false};
    std::atomic<bool> id_tables_sent{false};

    // Monotonic milliseconds (nn system tick / 19200).
    static std::int64_t nowMs();

private:
    ApState() = default;
};

}  // namespace mk8ap::ap
