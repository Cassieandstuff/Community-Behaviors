# ---------------------------------------------------------------------------
# Behavior Converter — standalone, MO2-launchable tool (BehaviorConverter.exe).
# Converts a Nemesis/Pandora behavior load order into per-mod Community Behaviors
# .hky bundles via the havok converter core (PatchConverter / ConvertModDelta).
# GLFW + Dear ImGui + OpenGL. An end-user + author tool that shares CB's whole
# engine, so it lives in this repo rather than splitting into cross-repo coupling.
#
# Flat src/: each Foo.cpp beside its Foo.h under src/ (app/, ui/, convert/); src/
# and src/convert are the private include roots (subdir-qualified "app/App.h" and
# bare "NemesisSetDataConvert.h" both resolve). No PCH — each source includes what
# it needs (imgui via the imgui target's PUBLIC includes).
# ---------------------------------------------------------------------------

sct_collect_sources(BCONV_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
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
    "${CMAKE_CURRENT_SOURCE_DIR}/src"           # "app/App.h", "ui/ConverterUI.h", "Converter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/convert"   # "NemesisSetDataConvert.h", "Fnis*.h"
    "${CMAKE_SOURCE_DIR}/src")                  # "core/debug/DebugFlags.h" — the shared plugin debug-flag registry
                                                # (last, so the converter's own headers still win on any name clash;
                                                #  a dir on the include PATH compiles nothing unless #included)

# imgui transitively supplies glfw + glad + opengl32 (PUBLIC). havok-core = the converter core
# (PatchConverter / decompilers / oracle); havok-model + havok-schema = the DEFAULT data-driven
# per-mod delta (ConvertModDelta / SchemaRegistry). sct-utilities = folder picker + zip (+ ole32/shell32).
target_link_libraries(behavior-converter PRIVATE havok-core havok-model havok-schema imgui sct-utilities)

if(MSVC)
    target_compile_options(behavior-converter PRIVATE /W4 /wd4100 /wd4505 /MP)
    # GUI subsystem in an optimized build (no console); console for Debug stderr. main() entry works
    # under /SUBSYSTEM:WINDOWS via the CRT-entry redirect.
    set_target_properties(behavior-converter PROPERTIES WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
    target_link_options(behavior-converter PRIVATE $<$<NOT:$<CONFIG:Debug>>:/ENTRY:mainCRTStartup>)
endif()

# ── Ship templates/ + Havok/ next to the exe ──────────────────────────────────
# The converter defaults templatesDir to <exe>/templates and the schema to <exe>/Havok. templates/ is
# XML-first (the <g>.xml oracle numbering that makes conversion + --regen-master possible) plus two
# fallback vanilla binaries. Havok/ (repo root schema tree) is the data-driven delta's schema source.
set(BCONV_TEMPLATES "${CMAKE_CURRENT_SOURCE_DIR}/templates")
set(BCONV_HAVOK     "${CMAKE_SOURCE_DIR}/Havok")
add_custom_command(TARGET behavior-converter POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${BCONV_TEMPLATES}" "$<TARGET_FILE_DIR:behavior-converter>/templates"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${BCONV_HAVOK}"     "$<TARGET_FILE_DIR:behavior-converter>/Havok"
    COMMENT "Behavior Converter: staging templates/ + Havok/ beside the exe"
    VERBATIM)

# Optional tools-folder deploy (MO2-launchable), same convention as the plugin deploy.
if(DEFINED ENV{SKYRIM_TOOLS_FOLDER})
    set(_tools "$ENV{SKYRIM_TOOLS_FOLDER}/Behavior Converter")
    add_custom_command(TARGET behavior-converter POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_tools}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:behavior-converter>" "${_tools}/"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${BCONV_TEMPLATES}" "${_tools}/templates"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${BCONV_HAVOK}"     "${_tools}/Havok"
        COMMENT "Behavior Converter: deploy -> ${_tools}"
        VERBATIM)
endif()

# ── Base master (Skyrim.hky) regeneration ─────────────────────────────────────
# The converter derives + validates the vanilla base master from $SKYRIM_DATASOURCE and packs it into
# the plugin's .hky stage, so cb_deploy's bundle merge-copy carries a fresh Skyrim.hky into the mod.
# Gated on SKYRIM_DATASOURCE: unset -> skipped, the already-deployed Skyrim.hky stays. See the helper.
# Short-path root for the unpacked master (regen's kept-unpacked tree + cb_deploy's cross-build diff
# unpack both land here). Honors the SCT_BASE_MASTER_DIR env var when set (the one knob), else defaults.
if(DEFINED ENV{SCT_BASE_MASTER_DIR})
    set(_base_master_default "$ENV{SCT_BASE_MASTER_DIR}")
else()
    set(_base_master_default "D:/sct-base-master")
endif()
set(SCT_BASE_MASTER_DIR "${_base_master_default}" CACHE PATH
    "Short-path dir where the unpacked Skyrim.hky base tree is kept (env SCT_BASE_MASTER_DIR)")
file(GLOB_RECURSE _bconv_template_files CONFIGURE_DEPENDS "${BCONV_TEMPLATES}/*")
include(GenerateBaseMaster)
sct_generate_base_master(
    CONVERTER_TARGET behavior-converter
    TEMPLATES        "${BCONV_TEMPLATES}"
    PACKED_OUT       "${CB_HKY_STAGE}/community_behaviors/plugins/Skyrim.hky"
    UNPACKED_DIR     "${SCT_BASE_MASTER_DIR}"
    DEPENDS          ${_bconv_template_files})
