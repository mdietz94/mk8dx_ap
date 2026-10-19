cmake_minimum_required(VERSION 3.16)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/sys/cmake/toolchain.cmake")
project(mk8dx_archipelago VERSION 0.1 LANGUAGES ASM C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(config/config.cmake)

# ---------------------------------------------------------------------------
# Version + bridge defines (land verbatim in HELLO + log lines).
#
# Nothing about the player's network is compiled in: the discovery sweep
# seed comes from ap_config.json in the mod's romfs layer, written by the
# setup wizard (RomConfigHook / ApDiscovery). The release ships one prebuilt
# binary; scripts/release_audit.py rejects any baked IPv4 literal.
# ---------------------------------------------------------------------------
set(MK8DX_VERSION          "4.0.0"         CACHE STRING "Target MK8DX version (the only supported one)")
set(MK8AP_MOD_VERSION      "0.1.0"         CACHE STRING "Module version baked into HELLO")
set(BRIDGE_PORT            17778           CACHE STRING "Bridge TCP port")
set(BRIDGE_RETRY_MS        3000            CACHE STRING "Reconnect backoff seed (ms)")
set(BRIDGE_RECV_TIMEOUT_MS 200             CACHE STRING "Per-iteration recv timeout (ms)")
set(DISCOVERY_PORT         17779           CACHE STRING "UDP discovery probe port")

add_compile_definitions(
    MK8DX_VERSION_STRING="${MK8DX_VERSION}"
    MK8AP_MOD_VERSION_STRING="${MK8AP_MOD_VERSION}"
    BRIDGE_PORT_VALUE=${BRIDGE_PORT}
    BRIDGE_RETRY_MS_VALUE=${BRIDGE_RETRY_MS}
    BRIDGE_RECV_TIMEOUT_MS_VALUE=${BRIDGE_RECV_TIMEOUT_MS}
    DISCOVERY_PORT_VALUE=${DISCOVERY_PORT}
)

option(MK8AP_DEBUG_SD_LOG "Enable boot-time SD-card log dump to sd:/mk8dx_ap.txt" OFF)
if(MK8AP_DEBUG_SD_LOG)
    add_compile_definitions(MK8AP_DEBUG_SD_LOG=1)
endif()

file(GLOB_RECURSE SOURCES_CXX ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${SOURCES_CXX})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)

# nn::os / nn::nifm / nn::socket / nn::prepo are undefined at link time;
# nnrtld resolves them at module load against the exports of main / sdk.
# Without sail's fakesymbols.so, tell lld to leave them as dynamic imports.
target_link_options(${PROJECT_NAME} PRIVATE -Wl,--unresolved-symbols=ignore-all)

include(sys/cmake/module.cmake)
include(sys/cmake/deploy.cmake)
include(sys/cmake/sail.cmake)
usesail(${PROJECT_NAME})
