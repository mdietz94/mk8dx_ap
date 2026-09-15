// nn::socket::Initialize observer.
//
// nn::socket::Initialize may only be called once per process. MK8DX
// initialises it itself at some point during boot (it is an online title),
// but we do not know exactly when relative to our own start. So:
//   * trampoline Initialize: pass the first call through and remember it;
//     swallow any later call (returns success) so neither side double-inits;
//   * ApClient::start asks ensureSocketInitialized(): if the game has not
//     called it yet we call it ourselves with our own pool (6 MB + 128 KB,
//     the sizes smo_archipelago uses) and the trampoline then neuters the
//     game's later call.

#include "Hooks.hpp"

#include <atomic>

#include "hk/hook/Trampoline.h"
#include "hk/types.h"

#include "../util/Log.hpp"

namespace nn::socket {
    unsigned int Initialize(void* pool, unsigned long poolSize,
                            unsigned long allocPoolSize, int concurLimit);
}

namespace mk8ap::hooks {

namespace {

constexpr unsigned long kPoolSize = 0x600000;
constexpr unsigned long kAllocPoolSize = 0x20000;
alignas(0x1000) char g_socket_pool[kPoolSize + kAllocPoolSize];

std::atomic<bool> g_initialized{false};
bool g_hook_installed = false;

HkTrampoline<unsigned int, void*, unsigned long, unsigned long, int> g_initHook =
    hk::hook::trampoline([](void* pool, unsigned long poolSize,
                            unsigned long allocPoolSize, int concurLimit) -> unsigned int {
        bool expected = false;
        if (!g_initialized.compare_exchange_strong(expected, true)) {
            MK8AP_LOG_INFO("[socket] Initialize called again (pool=%p size=%lu); "
                           "already initialised, returning success", pool, poolSize);
            return 0;
        }
        const unsigned int rc = g_initHook.orig(pool, poolSize, allocPoolSize, concurLimit);
        MK8AP_LOG_INFO("[socket] Initialize(pool=%p size=%lu alloc=%lu concur=%d) rc=0x%x",
                       pool, poolSize, allocPoolSize, concurLimit, rc);
        if (rc != 0) g_initialized.store(false);
        return rc;
    });

}  // namespace

void installSocketInitHook() {
    const auto rc = g_initHook.installAtSym<"_ZN2nn6socket10InitializeEPvmmi">();
    g_hook_installed = rc.succeeded();
    MK8AP_LOG_INFO("[socket] Initialize trampoline: %s",
                   g_hook_installed ? "installed" : "FAILED (symbol not exported?)");
}

bool socketInitialized() { return g_initialized.load(std::memory_order_acquire); }

bool ensureSocketInitialized() {
    if (socketInitialized()) return true;
    MK8AP_LOG_INFO("[socket] game has not initialised nn::socket yet; initialising "
                   "with our own pool (%lu+%lu bytes)", kPoolSize, kAllocPoolSize);
    // Goes through the trampoline when installed (sets the flag); calls the
    // SDK directly otherwise.
    const unsigned int rc = nn::socket::Initialize(g_socket_pool, kPoolSize, kAllocPoolSize, 0xE);
    if (!g_hook_installed && rc == 0) g_initialized.store(true);
    if (rc != 0) MK8AP_LOG_ERROR("[socket] Initialize failed rc=0x%x", rc);
    return socketInitialized();
}

}  // namespace mk8ap::hooks
