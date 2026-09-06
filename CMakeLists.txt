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

include(CommonLibSSE)   # add_commonlibsse_plugin() — SHARED lib + generated version-data TU
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
add_subdirectory(src/sct-config)        # shared config scanner (JSON/YAML/INI) the resolver reads
set(HAVOK_CORE_BUILD_TOOLS ON CACHE BOOL "havok-core-cli for the build-time gates + master regen" FORCE)
add_subdirectory("Retirement Home/havok-core" "${CMAKE_BINARY_DIR}/havok-core")

# ── The plugin ─────────────────────────────────────────────────────────────────
# The runtime behavior-graph compiler. Sources are FLAT: each Foo.cpp sits beside its Foo.h directly
# under src/ (no cpp/hpp split — src/ IS the private include root, so same-name quoted includes and
# the flat headers like "ERGate.h" resolve). The plugin's OWN sources are the flat .cpp at the src/
# ROOT (non-recursive glob, so the sibling engine modules under src/havok-*/ + src/sct-config/ — their
# own targets — are NOT swept in) PLUS the compile-time graph features under features/.
find_package(minhook CONFIG REQUIRED)   # function-ENTRY hooking (CommonLib's trampoline is call-site only)
find_package(ryml CONFIG REQUIRED)      # YAML front-end (imported target visible at this scope for the link below)

file(GLOB CB_ROOT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
sct_collect_sources(CB_FEATURE_SOURCES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/features/*.cpp")

# add_commonlibsse_plugin bakes in the CommonLibSSE version-data TU (SKSEPluginVersion) and links
# CommonLibSSE — Plugin.cpp defines only SKSEPluginLoad. (Works now that SKYRIM_SUPPORT_AE is ON;
# the earlier raw-export hack in main.cpp is gone.)
add_commonlibsse_plugin(CommunityBehaviors SOURCES ${CB_ROOT_SOURCES} ${CB_FEATURE_SOURCES})
target_compile_features(CommunityBehaviors PRIVATE cxx_std_23)

# src/ is the flat private include root: the plugin's own headers, Hooks/ (header-only), PluginLogger.h,
# and PCH.h ("PCH.h" quoted include in Plugin.cpp) all resolve from here.
target_include_directories(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src")

# havok-core = the offline/legacy compiler backbone; havok-pipeline = the feature-framework SDK
# (PUBLIC-exposes "FeatureRegistry.h" for features/<Owner>/ + Resolver.cpp); sct-config = the config
# scanner; ryml = YAML front-end; minhook = function-ENTRY hooks (the BSResource stream ctor).
target_link_libraries(CommunityBehaviors PRIVATE havok-core havok-pipeline sct-config ryml::ryml minhook::minhook)

# Force-included PCH (RE/Skyrim.h before Windows.h; the whole SKSE API for every TU).
target_precompile_headers(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src/PCH.h")

# ── Pack the .hky bundle YAML trees at build time ─────────────────────────────
# Each hky/<Bundle>.hky/ is the uncompressed, git-tracked YAML tree; the build packs every such folder
# to a single-file .hky under CB_HKY_STAGE, already shaped as the runtime serve path
# (community_behaviors/plugins/<Bundle>.hky) so the deploy is a straight mirror. Adding a bundle = drop a
# hky/<Name>.hky/ folder (CONFIGURE_DEPENDS). Skyrim.hky (the vanilla master) is NOT packed here — it
# regenerates from the vanilla datasource via the converter (wave 6); only the small mutable bundles pack.
set(CB_HKY_STAGE "${CMAKE_BINARY_DIR}/packed-hky")
file(GLOB CB_HKY_BUNDLES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/hky/*.hky")
set(_cb_hky_outputs "")
foreach(_hky_src ${CB_HKY_BUNDLES})
    if(NOT IS_DIRECTORY "${_hky_src}")
        continue()   # a stray packed .hky FILE in the source tree, not a bundle folder
    endif()
    get_filename_component(_hky_name "${_hky_src}" NAME)   # "<Bundle>.hky"
    set(_hky_out "${CB_HKY_STAGE}/community_behaviors/plugins/${_hky_name}")
    file(GLOB_RECURSE _hky_files CONFIGURE_DEPENDS "${_hky_src}/*")
    add_custom_command(
        OUTPUT  "${_hky_out}"
        COMMAND "$<TARGET_FILE:havok-core-cli>" hky-pack "${_hky_src}" -o "${_hky_out}"
        DEPENDS havok-core-cli ${_hky_files}
        COMMENT "Community Behaviors: packing ${_hky_name} from source YAML tree"
        VERBATIM)
    list(APPEND _cb_hky_outputs "${_hky_out}")
endforeach()
if(_cb_hky_outputs)
    add_custom_target(pack-hky-bundles DEPENDS ${_cb_hky_outputs})
    add_dependencies(CommunityBehaviors pack-hky-bundles)
endif()

# Deploy: DLL + the class schema tree (Havok/, clean) + the packed bundles (merge, preserving Skyrim.hky).
cb_deploy(CommunityBehaviors "Community Behaviors"
    SCHEMA_DIR "${CMAKE_SOURCE_DIR}/Havok"
    HKY_STAGE  "${CB_HKY_STAGE}")
