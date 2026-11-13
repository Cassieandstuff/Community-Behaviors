# ---------------------------------------------------------------------------
# Behavior Converter — standalone, MO2-launchable tool (BehaviorConverter.exe).
# Converts a Nemesis/Pandora behavior load order into per-mod Community Behaviors
# .hky bundles via HKY-SDK (hky::decompile::ConvertModDelta).
# GLFW + Dear ImGui + OpenGL. An end-user + author tool that shares CB's whole
# engine, so it lives in this repo rather than splitting into cross-repo coupling.
#
# Each Foo.cpp sits beside its Foo.h (app/, ui/, convert/, platform/); this folder
# and convert/ are the private include roots (subdir-qualified "app/App.h" and
# bare "NemesisSetDataConvert.h" both resolve). The only PCH is the engine prelude (below); each source
# includes what it needs (imgui via the imgui target's PUBLIC includes).
# ---------------------------------------------------------------------------

sct_collect_sources(BCONV_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
if(WIN32)
    list(APPEND BCONV_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/app.manifest")
endif()

add_executable(behavior-converter WIN32 ${BCONV_SOURCES})

set_target_properties(behavior-converter PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
    OUTPUT_NAME           "BehaviorConverter")

target_include_directories(behavior-converter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"               # "app/App.h", "ui/ConverterUI.h", "platform/Platform.h", "Converter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/convert"       # "NemesisSetDataConvert.h", "Fnis*.h"
    "${CMAKE_SOURCE_DIR}/src")                  # "core/debug/DebugFlags.h" — the shared plugin debug-flag registry
                                                # (last, so the converter's own headers still win on any name clash;
                                                #  a dir on the include PATH compiles nothing unless #included)

# imgui transitively supplies glfw + glad + opengl32 (PUBLIC). hky::core = HKY-SDK, the engine (decompile, per-mod
# delta, schema registry); config = the compatibility registry. ole32/shell32 back platform/'s folder picker + known
# folders.
target_link_libraries(behavior-converter PRIVATE hky::core hky::locomotion config imgui ole32 shell32)

if(MSVC)
    target_compile_options(behavior-converter PRIVATE /W4 /wd4100 /wd4505 /MP)
    # GUI subsystem in an optimized build (no console); console for Debug stderr. main() entry works
    # under /SUBSYSTEM:WINDOWS via the CRT-entry redirect.
    set_target_properties(behavior-converter PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    target_link_options(behavior-converter PRIVATE $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
endif()

# ── Ship templates/ next to the exe ──────────────────────────────────────────────
# The class schema is compiled into the exe (HKY-SDK). The converter reads only its Nemesis-ingest templates (the
# <g>.xml tagfiles each Nemesis patch is converted against) from <exe>/templates: copied beside the build-tree exe,
# which the regen below runs.
add_custom_command(TARGET behavior-converter POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:behavior-converter>/templates" "$<TARGET_FILE_DIR:behavior-converter>/schema"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/templates" "$<TARGET_FILE_DIR:behavior-converter>/templates"
    COMMENT "Behavior Converter: staging templates/ beside the exe"
    VERBATIM)

# The "Behavior Converter" tool (MO2-launchable): BUILDER stages exe + pdb + templates/ (owned) and deploys it to
# $SKYRIM_TOOLS_FOLDER/Behavior Converter.
builder_stage_app(behavior-converter APP "Behavior Converter" FILES "${CMAKE_CURRENT_SOURCE_DIR}/templates" AS templates)
builder_package(APP "Behavior Converter" VERSION ${PROJECT_VERSION})

# ── Derived-bundle regeneration (CB_REGEN_SOURCE) ───────────────────────────────
# The converter regenerates every derived bundle (Skyrim.hky, XPMSSE.hky, ...) from its $CB_REGEN_SOURCE
# source folder and packs it into the plugin's .hky stage, so BUILDER's bundle merge-copy carries them
# into the mod. Gated on CB_REGEN_SOURCE: unset -> skipped, the already-deployed bundles stay. See the
# helper. Short-path root for the unpacked trees (each regen's kept-unpacked <Name>.hky/ + the plugin's
# cross-build diff unpack both land here). Honors the SCT_BASE_MASTER_DIR env var when set (the one
# knob), else defaults.
if(DEFINED ENV{SCT_BASE_MASTER_DIR})
    set(_base_master_default "$ENV{SCT_BASE_MASTER_DIR}")
else()
    set(_base_master_default "D:/sct-base-master")
endif()
set(SCT_BASE_MASTER_DIR "${_base_master_default}" CACHE PATH
    "Short-path root where each regenerated bundle's unpacked <Name>.hky/ tree is kept (env SCT_BASE_MASTER_DIR)")
include(GenerateBaseMaster)
cb_regen_bundles(
    CONVERTER_TARGET behavior-converter
    PACKED_DIR       "${CB_HKY_STAGE}/community_behaviors/plugins"
    UNPACKED_DIR     "${SCT_BASE_MASTER_DIR}")

# HKY-SDK's prelude (src/hky/Prelude.h) — the converter includes engine headers that
# reference hky::common / hky::codec unqualified; make them ambient in every TU.
target_precompile_headers(behavior-converter PRIVATE "${hky-sdk_SOURCE_DIR}/src/hky/Prelude.h")
