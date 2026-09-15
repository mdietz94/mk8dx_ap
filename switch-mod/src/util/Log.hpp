// Lightweight logging.
//
// Each MK8AP_LOG_* writes to hk::svc::OutputDebugString (Ryujinx-visible;
// on real Switch routed into lm — see CAVEAT below).
//
// Optional: configure with -DMK8AP_DEBUG_SD_LOG=ON to additionally dump
// the first ~5 seconds of log output as a one-shot file write to
// sd:/mk8dx_ap.txt at drawMain frame ~300. This is purely a boot-time
// diagnostic for cases where Ryujinx isn't running and lm isn't usable.
// Off by default; see switch-mod/CMakeLists.txt MK8AP_DEBUG_SD_LOG option.
//
// CAVEAT (real Switch): Atmosphere's lm sink does NOT redirect per-title
// debug-log output into a file at sd:/atmosphere/contents/<TID>/. Older
// project docs that pointed users at `mk8ap.log` under that path were
// wrong — there has never been a file at that path. Use MK8AP_DEBUG_SD_LOG
// when you need on-device log capture without Ryujinx.
//
// log() is safe to call from any thread (init, worker, frame, hooks);
// allocator-free, atomic_flag spinlock + memcpy when the ring is enabled.

#pragma once

#include <cstdarg>
#include <cstddef>

namespace mk8ap::util {

enum class LogLevel { Debug, Info, Warn, Error };

void log(LogLevel lvl, const char* fmt, ...);

}  // namespace mk8ap::util

// Host-test builds (test_cappy_messenger.cpp, test_protocol.cpp, etc.) define
// MK8AP_HOST_TEST and link only the pure-logic .cpp files. Stub the macros to
// no-ops so we don't drag Log.cpp + every Switch dep in for tests.
#ifdef MK8AP_HOST_TEST
#  define MK8AP_LOG_DEBUG(...) ((void)0)
#  define MK8AP_LOG_INFO(...)  ((void)0)
#  define MK8AP_LOG_WARN(...)  ((void)0)
#  define MK8AP_LOG_ERROR(...) ((void)0)
#else
#  define MK8AP_LOG_DEBUG(...) ::mk8ap::util::log(::mk8ap::util::LogLevel::Debug, __VA_ARGS__)
#  define MK8AP_LOG_INFO(...)  ::mk8ap::util::log(::mk8ap::util::LogLevel::Info,  __VA_ARGS__)
#  define MK8AP_LOG_WARN(...)  ::mk8ap::util::log(::mk8ap::util::LogLevel::Warn,  __VA_ARGS__)
#  define MK8AP_LOG_ERROR(...) ::mk8ap::util::log(::mk8ap::util::LogLevel::Error, __VA_ARGS__)
#endif

namespace mk8ap::util {
// No-op stub kept for source compat with older call sites.
void markFsReady();

// Compile-time-gated diagnostic: when MK8AP_DEBUG_SD_LOG is defined, drains
// the ring buffer to sd:/mk8dx_ap.txt exactly once per session at drawMain
// frame ~300 (~5s in). When the flag is undefined, this is a no-op.
//
// Call once per frame from DrawMainHook. Cheap atomic-load early returns
// on every call except the single drain. Must run on the frame thread
// when active — the worker thread isn't safe for nn::fs.
void drainPendingToFile();

// Copy the in-memory log ring into `out` (up to `cap` bytes). Writes the
// actual number of bytes copied to `*out_len` if non-null. Safe to call
// from any thread (frame thread reads it for the on-Switch debug overlay).
// Returns the head pointer for convenience.
char* snapshotRecentLogs(char* out, std::size_t cap, std::size_t* out_len);
}  // namespace mk8ap::util
