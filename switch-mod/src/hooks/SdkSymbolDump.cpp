// Boot-time dump of selected nnSdk exports.
//
// MK8DX ships NintendoSDK 4.4.0 and its cmif proxy template names differ
// from the (SDK 15+) names PlayReportHook was written against, so the
// IPrepoService::SaveReport[WithUser] lookups come back NOT FOUND. This walks
// the SDK module's .dynsym and logs every symbol whose name contains one of
// a few substrings so the exact 4.4.0 mangled names can be copied from the
// boot log into PlayReportHook.cpp. Read-only; a few hundred string compares.

#include "Hooks.hpp"

#include <cstring>

#include "hk/ro/RoModule.h"
#include "hk/ro/RoUtil.h"
#include "rtld/RoModule.h"

#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

constexpr const char* kNeedles[] = {
    "IPrepoService",
    "SaveReport",
};
constexpr int kMaxLogged = 40;
constexpr unsigned kMaxSyms = 400000;

bool interesting(const char* name) {
    for (const char* n : kNeedles) {
        if (std::strstr(name, n)) return true;
    }
    return false;
}

void dumpModule(hk::ro::RoModule* mod, const char* label) {
    if (!mod) { MK8AP_LOG_WARN("[symdump] %s: module null", label); return; }
    auto* nnmod = mod->getNnModule();
    if (!nnmod || !nnmod->m_pDynSym || !nnmod->m_pStrTab) {
        MK8AP_LOG_WARN("[symdump] %s: no dynsym/strtab", label);
        return;
    }
    const Elf_Sym* syms = nnmod->m_pDynSym;
    const char* strtab = nnmod->m_pStrTab;
    const size_t strsz = nnmod->m_StrSz;
    const auto range = mod->range();

    // Symbol count: ELF hash gives nchain directly; for GNU hash fall back to
    // walking until the entries stop looking valid (bounded by the module).
    unsigned count = 0;
    if (!nnmod->m_IsGnuHash && nnmod->m_Symbols) {
        count = static_cast<unsigned>(nnmod->m_Symbols);
    } else {
        for (unsigned i = 0; i < kMaxSyms; ++i) {
            const ::ptr p = reinterpret_cast<::ptr>(&syms[i]);
            if (p + sizeof(Elf_Sym) > range.end()) break;
            if (syms[i].st_name >= strsz) break;
            count = i + 1;
        }
    }

    int logged = 0;
    unsigned matched = 0;
    for (unsigned i = 0; i < count; ++i) {
        const Elf_Sym& s = syms[i];
        if (s.st_name == 0 || s.st_name >= strsz) continue;
        const char* name = strtab + s.st_name;
        if (!interesting(name)) continue;
        ++matched;
        if (logged < kMaxLogged) {
            ++logged;
            MK8AP_LOG_INFO("[symdump] %s +0x%llx %s", label,
                           static_cast<unsigned long long>(s.st_value), name);
        }
    }
    MK8AP_LOG_INFO("[symdump] %s: %u dynsym entries scanned, %u matched (%d logged)",
                   label, count, matched, logged);
}

}  // namespace

void dumpSdkSymbolsOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    dumpModule(hk::ro::getSdkModule(), "sdk");
}

}  // namespace mk8ap::hooks
