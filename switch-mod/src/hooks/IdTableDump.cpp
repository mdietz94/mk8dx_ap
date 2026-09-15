// Enumerate mush::EDriverID / EBodyID / ETireID / EWingID ::text_(int) and
// ship {id -> internal name} tables to the PC once per session, so the AP
// side never has to hard-code enum orders that the public headers leave
// empty.
//
// text_() is called for ids 0..limit-1 with a conservative limit per kind
// (roster size + small headroom). Each returned pointer is validated before
// use: must point into the main module and be a short printable ASCII
// string; the first invalid entry ends the table.

#include "Hooks.hpp"

#include <cstring>

#include "hk/ro/RoUtil.h"

#include "../ap/ApFrameBridge.hpp"
#include "../ap/ApProtocol.hpp"
#include "../ap/ApState.hpp"
#include "../game/GameApi.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

bool looksLikeName(const char* s) {
    if (!s) return false;
    auto* main = hk::ro::getMainModule();
    if (main) {
        const auto range = main->range();
        const auto p = reinterpret_cast<std::uintptr_t>(s);
        if (p < range.start() || p >= range.end()) return false;
    }
    std::size_t n = 0;
    for (; n < 32; ++n) {
        const char c = s[n];
        if (c == '\0') break;
        if (c < 0x20 || c > 0x7E) return false;
    }
    return n > 0 && n < 32;
}

void dump(const char* kind, const char* (*text)(int), int limit) {
    if (!text) { MK8AP_LOG_WARN("[ids] %s: text_ unresolved; skipped", kind); return; }
    ap::IdTable tbl{};
    ap::copyFixed(tbl.kind, kind);
    for (int id = 0; id < limit && tbl.count < ap::IdTable::kMaxEntries; ++id) {
        const char* s = text(id);
        if (!looksLikeName(s)) {
            MK8AP_LOG_INFO("[ids] %s: stopped at id %d (%s)", kind, id, s ? "not a name" : "null");
            break;
        }
        tbl.entries[tbl.count].id = id;
        ap::copyFixed(tbl.entries[tbl.count].text, s);
        ++tbl.count;
    }
    MK8AP_LOG_INFO("[ids] %s: %zu entries", kind, tbl.count);
    if (tbl.count) ap::reportIdTable(tbl);
}

}  // namespace

void dumpIdTablesOnce() {
    auto& st = ap::ApState::instance();
    bool expected = false;
    if (!st.id_tables_sent.compare_exchange_strong(expected, true)) return;
    dump("driver", &game::driverText, 60);
    dump("body", &game::bodyText, 48);
    dump("tire", &game::tireText, 28);
    dump("wing", &game::wingText, 20);
}

}  // namespace mk8ap::hooks
