// nn::fs::MountRom observer: reads `<mount>:/ap_config.json` once, on the
// game thread, the moment the game has mounted its RomFS.
//
// Port of dread_ap's exlaunch fork (source/program/romfs.cpp RomMounted +
// OpenAndReadFile, discovery.cpp CacheBridgeHostFromRomfs). The setup wizard
// writes {"bridge_host":"<PC LAN IP>"} into the mod's romfs layer next to the
// exefs (Atmosphere LayeredFS / Ryujinx mods add the file to the game's
// RomFS); ApDiscovery sweeps that address's /24. Nothing is compiled in, so
// one prebuilt subsdk9 serves every network.
//
// fs discipline (the lesson Dread paid for with a save-breaking abort,
// Result 2002-6455 FileNotClosed): the file is read EXACTLY ONCE, here, on
// the thread that just mounted the RomFS, and every handle is closed on
// every path. The discovery worker only ever reads the cached string; it
// must never call nn::fs, because the game unmounts on its own schedule and
// an accessor open on another thread at that instant aborts the process.
// The nn::fs read functions are resolved by name (lookupSymbol), never as
// plain externs: an unresolved import would kill module init silently.

#include "Hooks.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"
#include "hk/types.h"

#include "../ap/ApConfig.hpp"
#include "../util/Json.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

// Layouts as in the SDK (and dread_ap's nn headers): FileHandle is one
// pointer-sized value passed by value; DirectoryEntryType a 32-bit enum.
struct FileHandle { void* handle; };
enum DirectoryEntryType : int { kEntryDirectory = 0, kEntryFile = 1 };
constexpr int kOpenModeRead = 1;
constexpr long kMaxConfigBytes = 4096;

using GetEntryTypeFn = u32 (*)(DirectoryEntryType*, const char*);
using OpenFileFn     = u32 (*)(FileHandle*, const char*, int);
using GetFileSizeFn  = u32 (*)(long*, FileHandle);
using ReadFileFn     = u32 (*)(FileHandle, long, void*, unsigned long);
using CloseFileFn    = void (*)(FileHandle);

struct FsFns {
    GetEntryTypeFn get_entry_type = nullptr;
    OpenFileFn     open_file      = nullptr;
    GetFileSizeFn  get_file_size  = nullptr;
    ReadFileFn     read_file      = nullptr;
    CloseFileFn    close_file     = nullptr;
};

bool resolveFs(FsFns& f) {
    f.get_entry_type = reinterpret_cast<GetEntryTypeFn>(
        hk::ro::lookupSymbol("_ZN2nn2fs12GetEntryTypeEPNS0_18DirectoryEntryTypeEPKc"));
    f.open_file = reinterpret_cast<OpenFileFn>(
        hk::ro::lookupSymbol("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci"));
    f.get_file_size = reinterpret_cast<GetFileSizeFn>(
        hk::ro::lookupSymbol("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE"));
    f.read_file = reinterpret_cast<ReadFileFn>(
        hk::ro::lookupSymbol("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm"));
    f.close_file = reinterpret_cast<CloseFileFn>(
        hk::ro::lookupSymbol("_ZN2nn2fs9CloseFileENS0_10FileHandleE"));
    if (f.get_entry_type && f.open_file && f.get_file_size && f.read_file && f.close_file)
        return true;
    MK8AP_LOG_WARN("[romcfg] nn::fs symbols missing (entry=%d open=%d size=%d read=%d close=%d)",
                   f.get_entry_type != nullptr, f.open_file != nullptr,
                   f.get_file_size != nullptr, f.read_file != nullptr, f.close_file != nullptr);
    return false;
}

// Reads `path` into buf (NUL-terminated). Every Result is checked and the
// handle is closed on every path that opened it.
bool readSmallFile(const FsFns& f, const char* path, char* buf, long cap, long& out_len) {
    DirectoryEntryType type = static_cast<DirectoryEntryType>(-1);
    if (f.get_entry_type(&type, path) != 0 || type != kEntryFile) return false;
    FileHandle h{};
    if (f.open_file(&h, path, kOpenModeRead) != 0) return false;
    long size = 0;
    if (f.get_file_size(&size, h) != 0 || size <= 0 || size >= cap) {
        f.close_file(h);
        return false;
    }
    if (f.read_file(h, 0, buf, static_cast<unsigned long>(size)) != 0) {
        f.close_file(h);
        return false;
    }
    f.close_file(h);
    buf[size] = '\0';
    out_len = size;
    return true;
}

void cacheBridgeHost(const char* mount) {
    if (ap::configuredBridgeHost(nullptr, 0)) return;  // first success wins
    if (!mount || !mount[0] || std::strlen(mount) > 32) return;
    char path[64];
    std::snprintf(path, sizeof(path), "%s:/ap_config.json", mount);

    FsFns f;
    if (!resolveFs(f)) return;
    char buf[kMaxConfigBytes];
    long len = 0;
    if (!readSmallFile(f, path, buf, sizeof(buf), len)) {
        MK8AP_LOG_WARN("[romcfg] %s missing/unreadable; LAN sweep disabled "
                       "(loopback-only discovery). Run /setup in the MK8DX Client.", path);
        return;
    }

    util::json::Reader r(buf, static_cast<std::size_t>(len));
    if (!r.enterObject()) {
        MK8AP_LOG_WARN("[romcfg] %s is not a JSON object", path);
        return;
    }
    std::string_view key;
    while (r.nextField(key)) {
        if (key == "bridge_host") {
            std::string_view host;
            if (r.nextString(host) && ap::setConfiguredBridgeHost(host)) {
                MK8AP_LOG_INFO("[romcfg] %s: bridge_host=%.*s", path,
                               static_cast<int>(host.size()), host.data());
                return;
            }
            break;
        }
        if (!r.skipValue()) break;
    }
    MK8AP_LOG_WARN("[romcfg] %s has no usable bridge_host", path);
}

HkTrampoline<u32, const char*, void*, unsigned long> g_mountRom = hk::hook::trampoline(
    [](const char* name, void* cache, unsigned long cache_size) -> u32 {
        const u32 rc = g_mountRom.orig(name, cache, cache_size);
        MK8AP_LOG_INFO("[romcfg] nn::fs::MountRom(%s) rc=0x%x", name ? name : "(null)", rc);
        if (rc == 0) cacheBridgeHost(name);
        return rc;
    });

}  // namespace

void installRomConfigHook() {
    const auto rc = g_mountRom.installAtSym<"_ZN2nn2fs8MountRomEPKcPvm">();
    MK8AP_LOG_INFO("[romcfg] nn::fs::MountRom: %s",
                   rc.succeeded() ? "installed" : "NOT FOUND (no ap_config.json; loopback only)");
}

}  // namespace mk8ap::hooks
