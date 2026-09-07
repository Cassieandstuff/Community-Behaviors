# ============================================================================
# Community Behaviors.
#
# TOP-LEVEL build: the runtime SKSE plugin (CommonLibSSE-NG) that compiles Havok
# behavior graphs over a mod load order + serves them, the Behavior Converter, and
# the build-time gates.
#
# CONSUMED as a subproject (FetchContent / add_subdirectory): exposes ONLY the
# shareable load-order resolution surface — the `cb-resolve` target and its engine
# modules — so a tool (e.g. a behavior editor) links `cb-resolve` and resolves a
# load order IDENTICALLY to the CB compiler, WITHOUT pulling in CommonLibSSE, the
# SKSE plugin, or the converter. See src/cb-resolve.
#
# Env contract (top-level build only):
#   VCPKG_ROOT            — vcpkg clone (toolchain, via the preset)
#   COMMONLIB_SSE_FOLDER  — a local CommonLibSSE-NG clone (add_subdirectory'd, offline)
#   SKYRIM_MODS_FOLDER    — MO2 mods folder (deploy target)
#   SKYRIM_DATASOURCE     — unpacked vanilla data (arms the Skyrim.hky regen)
#   SCT_BASE_MASTER_DIR   — short D-drive path for the unpacked-master diff tree
#
# Consumer contract (subproject): the consumer's toolchain must provide `ryml`
# (find_package(ryml CONFIG) — the engine modules' YAML front-end). Link `cb-resolve`.
# ============================================================================
cmake_minimum_required(VERSION 3.25)
project(CommunityBehaviors VERSION 0.3.3 LANGUAGES CXX C
        DESCRIPTION "Community Behaviors — runtime Havok behavior-graph compiler")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMAKE_CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) so the CB cmake/ modules resolve even when CB is
# add_subdirectory'd into another project (consumed mode — CMAKE_SOURCE_DIR would be the consumer's root).
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Consumption mode. Full standalone build ONLY when CB is the top-level project AND not asked for the
# resolve-only surface. Two ways to get resolve-only: add_subdirectory'd into another build (FetchContent
# — not top-level), or CB_RESOLVE_ONLY=ON (vcpkg builds CB as top-level, so the portfile passes this).
option(CB_RESOLVE_ONLY "Build only the cb-resolve surface (for vcpkg / library consumers)" OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CB_RESOLVE_ONLY)
    set(CB_STANDALONE ON)
else()
    set(CB_STANDALONE OFF)
endif()

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# ── The shareable engine + resolution surface (built in BOTH modes) ─────────────
# The data-driven modules ARE CB's source. cb-resolve is the curated, havok-core-free public
# contract (<cb-resolve/CbResolve.h>) a consumer links; it PUBLIC-pulls havok-model + havok-schema
# (→ havok-io + havok-framing transitively), so linking cb-resolve is all a consumer needs.
include(SctSources)   # sct_collect_sources() — the underscore-exclusion source glob
add_subdirectory(src/havok-framing)
add_subdirectory(src/havok-schema)
add_subdirectory(src/havok-io)
add_subdirectory(src/havok-model)
add_subdirectory(src/havok-anim)   # schema-driven animation pipeline (depends on havok-model)
add_subdirectory(src/cb-resolve)
add_subdirectory(API)              # the single public API facade (<CB-API.h>): resolve + anim + schema

# ── Install / export rules for the cb-resolve surface ────────────────────────────
# So a consumer can `find_package(cb-resolve)` (vcpkg, or a manual cmake --install). Inert unless someone
# actually installs — FetchContent (add_subdirectory) ignores these; vcpkg's vcpkg_cmake_install() runs them.
# Every include dir above is BUILD_INTERFACE/INSTALL_INTERFACE-wrapped so the export accepts them.
include(CMakePackageConfigHelpers)
install(TARGETS cb-api cb-resolve havok-framing havok-schema havok-io havok-model havok-anim
        EXPORT  cb-resolve-targets
        ARCHIVE DESTINATION lib
        INCLUDES DESTINATION include)
# Headers: each module's full include/ tree (havok-model's public headers pull havok-framing's, etc.).
# Headers + sources are co-located per module now (no include/ vs cpp/ split), so the export ships
# only the headers (FILES_MATCHING *.h) from each module root — the co-located .cpp are not installed.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/havok-framing/" DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/havok-schema/"  DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/havok-io/"      DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/havok-model/"   DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/havok-anim/"    DESTINATION include FILES_MATCHING PATTERN "*.h")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/cb-resolve/"    DESTINATION include FILES_MATCHING PATTERN "*.h")
# The CB-API facade header itself (repo-root include/), shipped as <CB-API.h>.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/include/CB-API.h" DESTINATION include)
# The Havok/ class-schema tree is DATA cb-resolve loads at runtime (SchemaRegistry::LoadDir) — the
# ground truth for class layout + per-field `merge:` policy + the schema_version stamp. Without it a
# consumer's merge runs with an empty classifier (NOT identical to the compiler), so ship it beside the
# package config and expose its location as CB_RESOLVE_SCHEMA_DIR (see cb-resolve-config.cmake.in).
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Havok/" DESTINATION share/cb-resolve/Havok)
install(EXPORT cb-resolve-targets
        NAMESPACE cb::
        FILE      cb-resolve-targets.cmake
        DESTINATION share/cb-resolve)
# Relocatable path to the shipped Havok/ schema tree, resolved by set_and_check(@PACKAGE_...@) in the
# config template regardless of where the package is installed/moved.
set(CB_RESOLVE_SCHEMA_INSTALL_DIR share/cb-resolve/Havok)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cb-resolve-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cb-resolve-config.cmake"
    INSTALL_DESTINATION share/cb-resolve
    PATH_VARS CB_RESOLVE_SCHEMA_INSTALL_DIR)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cb-resolve-config-version.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cb-resolve-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cb-resolve-config-version.cmake"
    DESTINATION share/cb-resolve)

# When consumed as a subproject, the resolution surface is all we expose — stop here so a consumer
# never configures the plugin / converter / CommonLibSSE / havok-core.
if(NOT CB_STANDALONE)
    message(STATUS "Community Behaviors: consumed as a subproject — exposing cb-resolve + engine modules "
                   "only (plugin, converter, havok-core, CommonLibSSE skipped). Consumer must provide ryml.")
    return()
endif()

# ════════════════════════════════════════════════════════════════════════════════
# TOP-LEVEL ONLY below here — the full CB build (plugin + converter + gates).
# ════════════════════════════════════════════════════════════════════════════════
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# CommonLibSSE (powerof3 fork) build options — set BEFORE it configures. AE (1.6.x) exposes the
# modern SKSE plugin-version format (SKSE::PluginVersionData); xbyak enables trampoline hooks. SSE 1.170 = AE.
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

# havok-pipeline (feature SDK), sct-config (config scanner), havok-core (quarantined backbone + CLI) —
# needed only by the plugin/converter/gates, so top-level only.
add_subdirectory(src/havok-pipeline)
add_subdirectory(src/sct-config)
set(HAVOK_CORE_BUILD_TOOLS ON CACHE BOOL "havok-core-cli for the build-time gates + master regen" FORCE)
add_subdirectory("Retirement Home/havok-core" "${CMAKE_BINARY_DIR}/havok-core")

# ── The plugin ─────────────────────────────────────────────────────────────────
# Sources are FLAT: each Foo.cpp beside its Foo.h directly under src/ (src/ IS the private include root).
# The plugin's OWN sources are the flat .cpp at the src/ ROOT (non-recursive glob, so the sibling engine
# modules under src/havok-*/ + src/sct-config/ are NOT swept in) PLUS the graph features under features/.
find_package(minhook CONFIG REQUIRED)   # function-ENTRY hooking (CommonLib's trampoline is call-site only)
find_package(ryml CONFIG REQUIRED)      # YAML front-end (imported target visible at this scope for the link)

# The plugin's entry point (Plugin.cpp) sits at src/ root; the compiler pipeline is organized by STAGE
# under src/core/ (discover -> resolve -> serve, plus bootstrap + debug), and the graph features live
# under src/features/. Collect the entry point, every pipeline stage, and the features — while the
# non-recursive root glob still keeps the sibling engine modules (src/havok-*/, sct-config, ...) out.
file(GLOB CB_ROOT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
sct_collect_sources(CB_CORE_SOURCES    "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/core/*.cpp")
sct_collect_sources(CB_FEATURE_SOURCES "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/features/*.cpp")

# add_commonlibsse_plugin bakes in the CommonLibSSE version-data TU (SKSEPluginVersion) + links CommonLibSSE
# — Plugin.cpp defines only SKSEPluginLoad.
add_commonlibsse_plugin(CommunityBehaviors SOURCES ${CB_ROOT_SOURCES} ${CB_CORE_SOURCES} ${CB_FEATURE_SOURCES})
target_compile_features(CommunityBehaviors PRIVATE cxx_std_23)

# src/ is the private include root: pipeline headers resolve stage-prefixed (e.g.
# "core/resolve/Resolver.h", "features/ERGate.h"), Hooks/ (header-only) from here too. src/pch/
# holds the force-include-tier shared headers (PCH.h, PluginLogger.h) so their bare quoted includes
# ("PCH.h" / "PluginLogger.h") still resolve everywhere.
target_include_directories(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/pch")

# havok-core = the offline/legacy compiler backbone; havok-pipeline = the feature-framework SDK
# (PUBLIC-exposes "FeatureRegistry.h"); sct-config = the config scanner; ryml = YAML front-end;
# minhook = function-ENTRY hooks (the BSResource stream ctor).
target_link_libraries(CommunityBehaviors PRIVATE havok-core havok-pipeline sct-config ryml::ryml minhook::minhook)

# Force-included PCH (RE/Skyrim.h before Windows.h; the whole SKSE API for every TU).
target_precompile_headers(CommunityBehaviors PRIVATE "${CMAKE_SOURCE_DIR}/src/pch/PCH.h")

# ── Dear ImGui + D3D11 backend, compiled INTO the plugin (no SKSE Menu Framework dependency) ──
# Drives the cold-compile progress bar: PresentPump renders its own ImGui frames onto the game's
# swapchain during the compile (see PresentPump.cpp). Fetched unconditionally (small; the converter's
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

# ── Pack the .hky bundle YAML trees at build time ─────────────────────────────
# Each hky/<Bundle>.hky/ is the uncompressed, git-tracked YAML tree; the build packs every such folder
# to a single-file .hky under CB_HKY_STAGE, already shaped as the runtime serve path
# (community_behaviors/plugins/<Bundle>.hky) so the deploy is a straight mirror. Adding a bundle = drop a
# hky/<Name>.hky/ folder (CONFIGURE_DEPENDS). Skyrim.hky (the vanilla master) is NOT packed here — it
# regenerates from the vanilla datasource via the converter; only the small mutable bundles pack.
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

# ── The Behavior Converter (end-user + author GUI tool) ──────────────────────────
# Shares CB's whole engine; a GLFW + Dear ImGui desktop app — heavier deps (glfw + imgui via
# FetchContent), so gated: -DCB_BUILD_CONVERTER=OFF gives a lean plugin-only configure.
option(CB_BUILD_CONVERTER "Build the Behavior Converter GUI tool" ON)
if(CB_BUILD_CONVERTER)
    include(FetchContent)

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
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        docking
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC
        "${imgui_SOURCE_DIR}/imgui.cpp"
        "${imgui_SOURCE_DIR}/imgui_draw.cpp"
        "${imgui_SOURCE_DIR}/imgui_tables.cpp"
        "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
        "${imgui_SOURCE_DIR}/imgui_demo.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(imgui PUBLIC
        "${imgui_SOURCE_DIR}"
        "${imgui_SOURCE_DIR}/backends")
    target_link_libraries(imgui PUBLIC glfw glad_gl_45_core)
    target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
    set_target_properties(imgui PROPERTIES CXX_STANDARD 17)

    add_subdirectory(src/sct-utilities)     # folder picker + zip (converter-only)
    add_subdirectory("APP/Behavior Converter")
endif()
