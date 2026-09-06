# ============================================================================
# Community Behaviors — standalone build environment.
#
# A runtime SKSE plugin (CommonLibSSE-NG) that compiles Havok behavior graphs over
# a mod load order and serves them to the live game. Purpose-built, single-project
# build — no monorepo machinery.
#
# Env contract (same as the monorepo it grew out of):
#   VCPKG_ROOT            — vcpkg clone (toolchain, via the preset)
#   COMMONLIB_SSE_FOLDER  — a local CommonLibSSE-NG clone (add_subdirectory'd, offline)
#   SKYRIM_MODS_FOLDER    — MO2 mods folder (deploy target)
# ============================================================================
cmake_minimum_required(VERSION 3.25)
project(CommunityBehaviors VERSION 0.3.1 LANGUAGES CXX C
        DESCRIPTION "Community Behaviors — runtime Havok behavior-graph compiler")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# CommonLibSSE (powerof3 fork) build options — set BEFORE it configures. AE (1.6.x) exposes the
# modern SKSE plugin-version format (SKSE::PluginVersionData); xbyak enables trampoline hooks (CB uses
# them). Target is SSE 1.170 = AE.
set(SKYRIM_SUPPORT_AE  ON CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)

# ── CommonLibSSE: a local clone if COMMONLIB_SSE_FOLDER points at one, else the pinned fork ──
if(DEFINED ENV{COMMONLIB_SSE_FOLDER} AND IS_DIRECTORY "$ENV{COMMONLIB_SSE_FOLDER}")
    message(STATUS "CommonLibSSE: local clone at $ENV{COMMONLIB_SSE_FOLDER}")
    add_subdirectory("$ENV{COMMONLIB_SSE_FOLDER}" "${CMAKE_BINARY_DIR}/CommonLibSSE")
else()
    message(STATUS "CommonLibSSE: FetchContent powerof3 fork (pinned)")
    include(FetchContent)
    FetchContent_Declare(CommonLibSSE
        GIT_REPOSITORY https://github.com/powerof3/CommonLibSSE.git
        GIT_TAG        aad6fb7dc9cd39754a28742b3b709ae29dd20843
        SOURCE_DIR     "${CMAKE_BINARY_DIR}/_deps/commonlibsse-src")
    FetchContent_MakeAvailable(CommonLibSSE)
endif()

include(Deploy)         # cb_deploy() — MO2 deploy

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# ── Havok compile/convert stack ────────────────────────────────────────────────
# The data-driven modules ARE CB's source — they live flat under src/ alongside the plugin. The legacy
# typed backbone (havok-core) is QUARANTINED under "Retirement Home/" (see its README). havok-core links
# the 5 modules, so the whole stack builds as one wave. HAVOK_CORE_BUILD_TOOLS=ON gives havok-core-cli
# (build-time gates + master regen).
include(SctSources)   # sct_collect_sources() — the underscore-exclusion source glob
add_subdirectory(src/havok-framing)
add_subdirectory(src/havok-schema)
add_subdirectory(src/havok-io)
add_subdirectory(src/havok-model)
add_subdirectory(src/havok-pipeline)
set(HAVOK_CORE_BUILD_TOOLS ON CACHE BOOL "havok-core-cli for the build-time gates + master regen" FORCE)
add_subdirectory("Retirement Home/havok-core" "${CMAKE_BINARY_DIR}/havok-core")

# ── The plugin ─────────────────────────────────────────────────────────────────
# A plain SHARED lib linking CommonLibSSE::CommonLibSSE. The SKSE plugin version data + entry point
# are defined directly in src/main.cpp (raw exports) rather than via the helper's generated version
# TU — the generated file's include ordering doesn't cooperate with a force-included PCH under MSVC.
add_library(CommunityBehaviors SHARED src/main.cpp)
target_link_libraries(CommunityBehaviors PUBLIC CommonLibSSE::CommonLibSSE)
target_compile_features(CommunityBehaviors PRIVATE cxx_std_23)

# Force-included PCH (RE/Skyrim.h before Windows.h; the whole SKSE API for every TU).
target_precompile_headers(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src/PCH.h")

cb_deploy(CommunityBehaviors "Community Behaviors")
