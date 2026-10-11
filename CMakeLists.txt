# ============================================================================
# Community Behaviors.
#
# TOP-LEVEL build: the runtime SKSE plugin (CommonLibSSE-NG) that compiles Havok
# behavior graphs over a mod load order + serves them, the Behavior Converter, and
# the build-time gates.
#
# CONSUMED as a subproject (FetchContent / add_subdirectory): exposes ONLY the
# public API surface — the `cb::CB-API` target (<CB-API.h>) and its engine modules —
# so a tool (e.g. a behavior editor) merges a load order IDENTICALLY to the CB
# compiler, WITHOUT pulling in CommonLibSSE, the SKSE plugin, or the converter.
#
# Env contract (top-level build only):
#   VCPKG_ROOT            — vcpkg clone (toolchain, via the preset)
#   SKYRIM_MODS_FOLDER    — MO2 mods folder (deploy target)
#   CB_REGEN_SOURCE       — root of <Name>.hky regen source folders (arms the derived-bundle regen)
#   SCT_BASE_MASTER_DIR   — short D-drive path for the unpacked-master diff tree
#
# Consumer contract (subproject): the consumer's toolchain must provide `ryml`
# (find_package(ryml CONFIG) — the engine modules' YAML front-end). Link `cb::CB-API`.
# ============================================================================
cmake_minimum_required(VERSION 3.25)
project(CommunityBehaviors VERSION 0.5.2 LANGUAGES CXX C
        DESCRIPTION "Community Behaviors — runtime Havok behavior-graph compiler")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMAKE_CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) so the CB cmake/ modules resolve even when CB is
# add_subdirectory'd into another project (consumed mode — CMAKE_SOURCE_DIR would be the consumer's root).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Consumption mode. Full standalone build ONLY when CB is the top-level project AND not asked for the
# API-only surface. Two ways to get API-only: add_subdirectory'd into another build (FetchContent
# — not top-level), or CB_API_ONLY=ON (vcpkg builds CB as top-level, so the portfile passes this).
option(CB_API_ONLY "Build only the cb-api surface (for vcpkg / library consumers)" OFF)
option(CB_BUILD_CONVERTER "Build the Behavior Converter GUI tool (+ the derived-bundle regen)" ON)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CB_API_ONLY)
    set(CB_STANDALONE ON)
else()
    set(CB_STANDALONE OFF)
endif()

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# ── The shareable engine + resolution surface (built in BOTH modes) ─────────────
# src/engine is CB's shared reflection engine (one `engine` target). cb::CB-API (<CB-API.h>) is the curated
# public contract a consumer links; it PUBLIC-pulls the engine, so linking it is all a consumer needs.
include(SctSources)   # sct_collect_sources() — the underscore-exclusion source glob
add_subdirectory(src/engine)
add_subdirectory(API)              # the single public API facade (<CB-API.h>): resolve + anim + schema

# ── Install / export rules for the cb-api surface ────────────────────────────────
# So a consumer can `find_package(cb-api)` (vcpkg, or a manual cmake --install). Inert unless someone
# actually installs — FetchContent (add_subdirectory) ignores these; vcpkg's vcpkg_cmake_install() runs them.
# Every include dir above is BUILD_INTERFACE/INSTALL_INTERFACE-wrapped so the export accepts them.
include(CMakePackageConfigHelpers)
install(TARGETS cb-api engine
        EXPORT  cb-api-targets
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include)
# Headers: the engine include root, headers only (sources sit beside them and are not installed).
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/engine/" DESTINATION include FILES_MATCHING PATTERN "*.h")
# The CB-API facade header itself (repo-root include/), shipped as <CB-API.h>.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/CB-API.h" DESTINATION include)
# The class schema (schema/) is DATA the resolve/merge loads at runtime (SchemaRegistry::LoadDir) — the
# ground truth for class layout + per-field `merge:` policy + the schema_version stamp. Without it a
# consumer's merge runs with an empty classifier (NOT identical to the compiler), so ship it beside the
# package config and expose its location as CB_API_SCHEMA_DIR (see cb-api-config.cmake.in).
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/schema/" DESTINATION share/cb-api/schema PATTERN templates EXCLUDE)
install(EXPORT cb-api-targets
        NAMESPACE cb::
        FILE      cb-api-targets.cmake
        DESTINATION share/cb-api)
# Relocatable path to the shipped schema tree, resolved by set_and_check(@PACKAGE_...@) in the
# config template regardless of where the package is installed/moved.
set(CB_API_SCHEMA_INSTALL_DIR share/cb-api/schema)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cb-api-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cb-api-config.cmake"
    INSTALL_DESTINATION share/cb-api
    PATH_VARS CB_API_SCHEMA_INSTALL_DIR)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cb-api-config-version.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cb-api-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cb-api-config-version.cmake"
    DESTINATION share/cb-api)

# When consumed as a subproject, the resolution surface is all we expose — stop here so a consumer
# never configures the plugin / converter / tools / CommonLibSSE.
if(NOT CB_STANDALONE)
    message(STATUS "Community Behaviors: consumed as a subproject — exposing cb-api + the engine "
                   "only (plugin, converter, tools, CommonLibSSE skipped). Consumer must provide ryml.")
    return()
endif()

# ════════════════════════════════════════════════════════════════════════════════
# TOP-LEVEL ONLY below here — the full CB build (plugin + converter + gates).
# ════════════════════════════════════════════════════════════════════════════════
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# CommonLibSSE-NG (alandtse fork) build options — set BEFORE it configures. Full NG: one DLL for
# SE + AE + VR, addressed at runtime via REL::VariantID. xbyak enables trampoline hooks; the fork's
# unit tests are off (we don't build them). SKSE_SUPPORT_PATCH_SAFETY stays at its ON default.
set(ENABLE_SKYRIM_SE   ON  CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE   ON  CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR   ON  CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON  CACHE BOOL "" FORCE)
set(BUILD_TESTS        OFF CACHE BOOL "" FORCE)

# ── CommonLibSSE-NG: vendored as a git submodule (lib/commonlibsse-ng, alandtse `ng` branch) ──
# Single source of truth — no env var, no network fetch. `git submodule update --init --recursive`.
message(STATUS "CommonLibSSE-NG: submodule at lib/commonlibsse-ng")
add_subdirectory(lib/commonlibsse-ng "${CMAKE_BINARY_DIR}/CommonLibSSE")

include(CommonLibSSE)   # add_commonlibsse_plugin() — SHARED lib + generated version-data TU
include(Deploy)         # cb_deploy() — MO2 deploy

# common/: what features build against — the feature SDK and the config scanner. Top-level only.
add_subdirectory(common/feature-sdk)
add_subdirectory(common/config)

# ── The plugin ─────────────────────────────────────────────────────────────────
# src/ is the private include root; each Foo.cpp sits beside its Foo.h.
find_package(minhook CONFIG REQUIRED)   # function-ENTRY hooking (CommonLib's trampoline is call-site only)
find_package(ryml CONFIG REQUIRED)      # YAML front-end (imported target visible at this scope for the link)

# The plugin is src/core/ (Plugin.cpp + the pipeline stages discover -> resolve -> serve, plus bootstrap,
# debug, hooks) and the graph features under src/features/, compiled straight into the plugin target.
sct_collect_sources(CB_CORE_SOURCES    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/core/*.cpp")
sct_collect_sources(CB_FEATURE_SOURCES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/features/*.cpp")

# add_commonlibsse_plugin bakes in the CommonLibSSE version-data TU (SKSEPluginVersion) + links CommonLibSSE
# — Plugin.cpp defines only SKSEPluginLoad.
add_commonlibsse_plugin(CommunityBehaviors SOURCES ${CB_CORE_SOURCES} ${CB_FEATURE_SOURCES})
target_compile_features(CommunityBehaviors PRIVATE cxx_std_23)

# src/ is the private include root: headers resolve folder-prefixed (e.g. "core/resolve/Resolver.h",
# <core/hooks/hookslib.h>, "features/ERGate.h"). src/core/pch/ holds the force-include-tier shared headers
# (PCH.h, PluginLogger.h), on the path so their bare quoted includes resolve everywhere.
target_include_directories(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/core/pch")

# Engine symbol registry: src/core/hooks/registry/engine.yaml (the single hand-edited source of every engine
# address CB uses beyond CommonLib) baked into a constexpr header at build time — see
# cmake/EmbedEngineRegistry.cmake and <core/hooks/registry/Registry.h>.
set(_cb_gen_dir "${CMAKE_BINARY_DIR}/generated")
set(_cb_engine_reg "${_cb_gen_dir}/core/hooks/registry/EngineRegistry.gen.h")
add_custom_command(
    OUTPUT  "${_cb_engine_reg}"
    COMMAND "${CMAKE_COMMAND}"
            "-DREGISTRY_YAML=${CMAKE_SOURCE_DIR}/src/core/hooks/registry/engine.yaml"
            "-DOUT_H=${_cb_engine_reg}"
            -P "${CMAKE_SOURCE_DIR}/cmake/EmbedEngineRegistry.cmake"
    DEPENDS "${CMAKE_SOURCE_DIR}/src/core/hooks/registry/engine.yaml" "${CMAKE_SOURCE_DIR}/cmake/EmbedEngineRegistry.cmake"
    COMMENT "Generating the engine symbol registry from src/core/hooks/registry/engine.yaml"
    VERBATIM)
target_sources(CommunityBehaviors PRIVATE "${_cb_engine_reg}")
target_include_directories(CommunityBehaviors PRIVATE "${_cb_gen_dir}")

# engine = the shared reflection engine; feature-sdk = the graph-feature SDK
# (<feature-sdk/FeatureRegistry.h>); config = the config scanner; ryml = YAML front-end;
# minhook = function-ENTRY hooks (the BSResource stream ctor).
target_link_libraries(CommunityBehaviors PRIVATE feature-sdk engine config ryml::ryml minhook::minhook)

# Force-included PCH (RE/Skyrim.h before Windows.h; the whole SKSE API for every TU).
target_precompile_headers(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src/core/pch/PCH.h")

# ── Dear ImGui + D3D11 backend, compiled INTO the plugin (no SKSE Menu Framework dependency) ──
# Drives the cold-compile progress bar: ProgressHud draws ImGui frames after the game's hooked
# present call during the compile (see ProgressHud.cpp). Fetched unconditionally (small; the converter's
# separate glfw/opengl3 `imgui` lib, below, reuses the same populated source). d3d11/dxgi for the
# swapchain + device the game hands us.
include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)
add_library(imgui_dx11 STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp")
target_include_directories(imgui_dx11 PUBLIC "${imgui_SOURCE_DIR}")
target_compile_definitions(imgui_dx11 PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
set_target_properties(imgui_dx11 PROPERTIES CXX_STANDARD 17)
target_link_libraries(CommunityBehaviors PRIVATE imgui_dx11 d3d11 dxgi)

# ── Dev tools ────────────────────────────────────────────────────────────────────
add_subdirectory(tools/hky)      # hky-tool: .hky pack/unpack (also unpacks the deployed master for diffing)
add_subdirectory(tools/cbdiff)   # cbdiff: semantic diff of CB output vs a proven reference (Pandora)

# ── Bundle stage ────────────────────────────────────────────────────────────────
# Every shipped .hky is a DERIVED bundle the converter regenerates from $CB_REGEN_SOURCE (see
# cmake/GenerateBaseMaster.cmake), packed into this tree already shaped as the runtime serve path
# (community_behaviors/plugins/<Bundle>.hky), so the deploy is a straight mirror.
set(CB_HKY_STAGE "${CMAKE_BINARY_DIR}/packed-hky")

# Deploy: DLL + the class schema + the packed bundles (merge, preserving Skyrim.hky). The plugin never
# reads the converter's Nemesis-ingest templates, so schema/templates stays out of the mod.
cb_deploy(CommunityBehaviors "Community Behaviors"
    SCHEMA_DIR     "${CMAKE_SOURCE_DIR}/schema"
    SCHEMA_EXCLUDE templates
    HKY_STAGE      "${CB_HKY_STAGE}")

# ── DEV-ONLY: x64dbg auto-attach SKSE plugin (NEVER shipped) ─────────────────────
# A standalone SKSE plugin that spawns x64dbg attached to the game at startup, so early
# breakpoints (e.g. the LoadClips call site) can be caught without racing the launch.
# Opt-in + EXCLUDE_FROM_ALL + never referenced by cb_deploy, so it cannot reach end users.
# Enable with -DCB_BUILD_AUTOATTACH=ON, then build the CBDebugAutoAttach target explicitly.
option(CB_BUILD_AUTOATTACH "Build the DEV-ONLY x64dbg auto-attach SKSE plugin (never shipped)" OFF)
if(CB_BUILD_AUTOATTACH)
    add_subdirectory(tools/x64dbg-autoattach)
endif()

# ── The Behavior Converter (end-user + author GUI tool) ──────────────────────────
# Shares CB's whole engine; a GLFW + Dear ImGui desktop app — heavier deps (glfw + imgui via
# FetchContent), so gated: -DCB_BUILD_CONVERTER=OFF gives a lean plugin-only configure.
if(CB_BUILD_CONVERTER)
    # ── GLFW (windowing) ──────────────────────────────────────────────────────
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glfw)

    # ── glad (bundled pre-generated OpenGL 4.5 Core loader) ───────────────────
    # Before imgui, so imgui_impl_opengl3.cpp finds <glad/gl.h>.
    add_library(glad_gl_45_core STATIC "${CMAKE_SOURCE_DIR}/cmake/glad/src/gl.c")
    target_include_directories(glad_gl_45_core PUBLIC "${CMAKE_SOURCE_DIR}/cmake/glad/include")

    # ── Dear ImGui (docking branch) ───────────────────────────────────────────
    add_library(imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(imgui PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends")
    target_link_libraries(imgui PUBLIC glfw glad_gl_45_core)
    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    set_target_properties(imgui PROPERTIES CXX_STANDARD 17)

    add_subdirectory(src/converter)
endif()
