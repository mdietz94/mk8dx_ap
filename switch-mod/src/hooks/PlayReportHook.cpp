// nn::prepo PlayReport capture: the primary race-result channel.
//
// At the end of every race MK8DX files a PlayReport whose MessagePack body
// carries Course / Rank / Points / Rule / Engine / Mode / FinishReason /
// Driver / Body / Tire / Wing (the same record Ryujinx prints as
// "PlayReport log:"). We capture it below the PlayReport class, at the IPC
// proxy (CmifProxyImpl<IPrepoService>::_nn_sf_sync_SaveReport[WithUser]),
// because hooking PlayReport::Save/Add triggers a delayed SDK validator
// abort on another thread (smbw_ap, 2026-05: documented in that repo's
// handoff). The ctor + SetEventId are safe and only used for logging.
//
// The payload is forwarded verbatim; the PC decodes it.

#include "Hooks.hpp"

#include "hk/hook/Trampoline.h"
#include "hk/types.h"

#include "../ap/ApFrameBridge.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

// 16-byte {ptr, size} structs passed by const&: AAPCS hands us a pointer.
struct InArrayChar { const char* ptr; ::size size; };
struct InBuffer    { const void* ptr; ::size size; };

HkTrampoline<unsigned, void*, const char*> g_setEventId = hk::hook::trampoline(
    [](void* self, const char* event) -> unsigned {
        MK8AP_LOG_INFO("[prepo] set_event this=%p event=%s", self, event ? event : "(null)");
        return g_setEventId.orig(self, event);
    });

HkTrampoline<unsigned, void*, const InArrayChar*, const InBuffer*, unsigned long>
    g_saveReport = hk::hook::trampoline(
        [](void* self, const InArrayChar* room, const InBuffer* payload,
           unsigned long flags) -> unsigned {
            const ::size room_len = room ? room->size : 0;
            const ::size pay_size = payload ? payload->size : 0;
            MK8AP_LOG_INFO("[prepo] ipc.save room=%.*s size=%zu flags=0x%lx",
                           static_cast<int>(room_len), room && room->ptr ? room->ptr : "",
                           static_cast<std::size_t>(pay_size), flags);
            ap::reportPlayReport(room ? room->ptr : nullptr, room_len,
                                 payload ? payload->ptr : nullptr, pay_size);
            const unsigned ret = g_saveReport.orig(self, room, payload, flags);
            startNetworkingOnce("nn::prepo SaveReport");
            return ret;
        });

HkTrampoline<unsigned, void*, const void*, const InArrayChar*, const InBuffer*, unsigned long>
    g_saveReportWithUser = hk::hook::trampoline(
        [](void* self, const void* uid, const InArrayChar* room,
           const InBuffer* payload, unsigned long flags) -> unsigned {
            const ::size room_len = room ? room->size : 0;
            const ::size pay_size = payload ? payload->size : 0;
            MK8AP_LOG_INFO("[prepo] ipc.save_uid room=%.*s size=%zu flags=0x%lx",
                           static_cast<int>(room_len), room && room->ptr ? room->ptr : "",
                           static_cast<std::size_t>(pay_size), flags);
            ap::reportPlayReport(room ? room->ptr : nullptr, room_len,
                                 payload ? payload->ptr : nullptr, pay_size);
            const unsigned ret = g_saveReportWithUser.orig(self, uid, room, payload, flags);
            startNetworkingOnce("nn::prepo SaveReportWithUser");
            return ret;
        });

void report(const char* what, hk::Result rc) {
    MK8AP_LOG_INFO("[prepo] %s: %s", what, rc.succeeded() ? "installed" : "NOT FOUND (skipped)");
}

}  // namespace

void installPlayReportHooks() {
    report("PlayReport::SetEventId",
           g_setEventId.installAtSym<"_ZN2nn5prepo10PlayReport10SetEventIdEPKc">());
    report("IPrepoService::SaveReport",
           g_saveReport.installAtSym<
               "_ZN2nn2sf4cmif6client6detail13CmifProxyImplINS_5prepo6detail3ipc13IPrepoServiceENS2_19CmifDomainProxyKindINS0_4hipc6client38Hipc2ClientSessionManagedProxyKindBaseINSB_18Hipc2ProxyKindBaseILNSA_6detail11MessageTypeE6EEEEEEENS0_30MemoryResourceAllocationPolicyES8_NS3_19ProcessModifierImplINS2_21DefaultProxyFilterTagEEEE22_nn_sf_sync_SaveReportERKNS0_7InArrayIcEERKNS0_8InBufferEm">());
    report("IPrepoService::SaveReportWithUser",
           g_saveReportWithUser.installAtSym<
               "_ZN2nn2sf4cmif6client6detail13CmifProxyImplINS_5prepo6detail3ipc13IPrepoServiceENS2_19CmifDomainProxyKindINS0_4hipc6client38Hipc2ClientSessionManagedProxyKindBaseINSB_18Hipc2ProxyKindBaseILNSA_6detail11MessageTypeE6EEEEEEENS0_30MemoryResourceAllocationPolicyES8_NS3_19ProcessModifierImplINS2_21DefaultProxyFilterTagEEEE30_nn_sf_sync_SaveReportWithUserERKNS_7account3UidERKNS0_7InArrayIcEERKNS0_8InBufferEm">());
}

}  // namespace mk8ap::hooks
