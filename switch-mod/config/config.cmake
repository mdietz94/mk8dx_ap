set(LINKFLAGS -nodefaultlibs)
set(LLDFLAGS --no-demangle --gc-sections)

set(OPTIMIZE_OPTIONS_DEBUG -O2 -gdwarf-4)
# Conservative codegen (same rationale as smo_archipelago / smbw_ap): -O3 +
# LLVM 19 can emit instruction sequences that Ryujinx's ARMeilleure JIT
# mistranslates. Stay at -O2.
set(OPTIMIZE_OPTIONS_RELEASE -O2 -fno-strict-aliasing)
set(WARN_OPTIONS -Werror=return-type -Wno-invalid-offsetof)

set(INCLUDES include)

set(ASM_OPTIONS "")
set(C_OPTIONS -ffunction-sections -fdata-sections)
set(CXX_OPTIONS "")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(IS_32_BIT FALSE)
set(TARGET_IS_STATIC FALSE)
set(MODULE_NAME mk8dx_archipelago)
# Mario Kart 8 Deluxe. 3.0.4+ is a native 64-bit build (3.0.3 and below are
# ARM32 and cannot load an AArch64 subsdk). Target: 4.0.0 only, main build
# id 2C336A9BCF79C3040CE506CDD391B578; the module stays inactive elsewhere.
set(TITLE_ID 0x0100152000022000)
# subsdk9 is the Atmosphere exefs slot the module lands in.
set(MODULE_BINARY subsdk9)
set(SDK_PAST_1900 FALSE)
# No sail: MK8DX ships no symbols. Game functions are located at runtime by
# byte-pattern scan (src/sym/PatternResolver) and SDK functions (nn::socket,
# nn::nifm, nn::prepo, nn::os) are resolved by nnrtld / hk::ro::lookupSymbol.
set(USE_SAIL FALSE)

set(TRAMPOLINE_POOL_SIZE 0x40)
set(BAKE_SYMBOLS FALSE)

# HeapSourceDynamic routes operator new / malloc / free to the game's own
# allocator. Without it, libc++ growth paths NULL-deref on worker threads.
set(HAKKUN_ADDONS HeapSourceDynamic)
