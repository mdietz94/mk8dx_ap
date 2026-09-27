// Game-thread -> worker marshalling. Hooks call these; they push onto the
// ApState rings which the socket worker drains. All allocation-free.

#pragma once

#include <cstddef>
#include <cstdint>

#include "ApProtocol.hpp"

namespace mk8ap::ap {

// RaceFinishHook. `seq` is stamped here.
void reportRaceResult(int course_id, int rule, int engine, bool mirror,
                      int rank, int kart_index);

// PlayReportHook: forward a serialized nn::prepo report (copied; caller's
// buffer may go away). Truncates at kPlayReportMaxBytes.
void reportPlayReport(const char* room, std::size_t room_len,
                      const void* payload, std::size_t size);

// IdTableDump: one table per kind.
void reportIdTable(const IdTable& tbl);

// util::log forwarder. Safe from any thread.
void enqueueRemoteLog(const char* level, const char* msg);

}  // namespace mk8ap::ap
