# ---------------------------------------------------------------------------
# havok-core — first-party C++ Havok toolchain
#
# A clean-room C++23 rewrite of HKBuild + HKX2E (the MIT HKX2Library fork).
# Compiles a blueprint's behavior model to a Havok packfile (.hkx). Shared by
# the editor (export) and Engine Relay (runtime). Pure C++23: NO Qt, OpenGL,
# .NET, or CommonLibSSE/RE:: dependencies.
#
# Tiers (by include direction only): sct -> model -> classes -> core.
# ---------------------------------------------------------------------------

sct_collect_sources(HAVOK_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

# The YAML loader (Tier B, src/model/yaml/) is the ONLY TU that depends on ryml
# (rapidyaml). Keep the rest of the library dependency-free: pull the yaml TU out
# of the default source set and add it back only when ryml is available (the VS /
# vcpkg build). This preserves the standalone, zero-dependency build of the core.
sct_collect_sources(HAVOK_CORE_YAML_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model/yaml/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/anim/yaml/*.cpp"
)
if(HAVOK_CORE_YAML_SOURCES)
    list(REMOVE_ITEM HAVOK_CORE_SOURCES ${HAVOK_CORE_YAML_SOURCES})
endif()

add_library(havok-core STATIC ${HAVOK_CORE_SOURCES})

set_target_properties(havok-core PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF
)

target_include_directories(havok-core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# The packfile framing (IHavokObject, BinaryReader/WriterEx, PackFile(De)serializer,
# HavokRegistry infra) now lives in havok-framing — havok-core's sources still include it at
# the same havok/core/* + havok/classes/IHavokObject.h paths, provided by this leaf.
# The name-keyed doc + merge + animdata derivation (BehaviorData/defs, BashMerge, the YAML
# loaders, AnimDataDeriver/AnimationData) moved to havok-model (Stage 3r.1); havok-core's
# builders, sct/ and anim/ consume them from there at the same havok/model|anim|sct/* paths.
# The animation compile/decompile pipeline (AnimationDef, spline codec, Assemble/Compile/
# DecompileAnimation, AnimationYamlLoader) moved to havok-anim (schema-native); havok-core's
# CharacterDecompiler + the CLI anim verbs consume it from there at havok/anim/* paths.
target_link_libraries(havok-core PUBLIC havok-framing havok-model havok-anim)

if(MSVC)
    target_compile_options(havok-core PRIVATE /W4 /wd4100 /MP)
endif()

# ── Optional YAML loader (Tier B, ryml-gated) ──────────────────────────────
# Wired exactly like Engine Relay (find_package(ryml CONFIG REQUIRED) +
# ryml::ryml). If ryml isn't on the prefix path the loader is simply omitted; the
# builder + Def model still compile and work (they don't need YAML — the editor
# can populate BehaviorData directly).
find_package(ryml CONFIG QUIET)
if(ryml_FOUND AND HAVOK_CORE_YAML_SOURCES)
    # The remaining YAML tier is the ANIMATION loaders (src/anim/yaml/). The behavior/character
    # loaders + the .hky archive reader (and its vendored miniz) moved to havok-model (3r.1).
    # ryml-gated so the core stays dependency-free when ryml is absent.
    target_sources(havok-core PRIVATE ${HAVOK_CORE_YAML_SOURCES})
    target_link_libraries(havok-core PUBLIC ryml::ryml)
    message(STATUS "havok-core: animation YAML loaders enabled (ryml)")
else()
    message(STATUS "havok-core: YAML loaders disabled (ryml not found) — core is dependency-free")
endif()

# ── Unit tests (M0 exit: binary-IO round-trip) ─────────────────────────────
option(HAVOK_CORE_BUILD_TESTS "Build havok-core unit tests" ON)
if(HAVOK_CORE_BUILD_TESTS)
    sct_collect_sources(HAVOK_CORE_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )
    add_executable(havok-core-tests ${HAVOK_CORE_TEST_SOURCES})
    set_target_properties(havok-core-tests PROPERTIES
        CXX_STANDARD          23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )
    target_include_directories(havok-core-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(havok-core-tests PRIVATE havok-core)
    if(MSVC)
        target_compile_options(havok-core-tests PRIVATE /W4 /wd4100 /MP)
    endif()
endif()

# ── Tools (opt-in) ──────────────────────────────────────────────────────────
# tools/ is deliberately outside the library globs above. The unit tests build
# their own packfiles, so they cannot show that havok-core reads a file Bethesda
# actually shipped; skeleton_import_smoke does, against a real skeleton.hkx.
option(HAVOK_CORE_BUILD_TOOLS "Build havok-core command-line tools" OFF)
if(HAVOK_CORE_BUILD_TOOLS)
    add_executable(skeleton-import-smoke
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/skeleton_import_smoke.cpp")
    set_target_properties(skeleton-import-smoke PROPERTIES
        CXX_STANDARD          23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS        OFF
    )
    target_link_libraries(skeleton-import-smoke PRIVATE havok-core)
    if(MSVC)
        target_compile_options(skeleton-import-smoke PRIVATE /W4 /wd4100 /MP)
    endif()

    # build_behavior: authored YAML tree -> .hkx bytes (the full compile pipeline).
    # Needs the ryml-gated YAML loader, so only add it when ryml was found.
    if(ryml_FOUND AND HAVOK_CORE_YAML_SOURCES)
        add_executable(build_behavior
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_behavior.cpp")
        set_target_properties(build_behavior PROPERTIES
            CXX_STANDARD          23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS        OFF
        )
        target_link_libraries(build_behavior PRIVATE havok-core)
        if(MSVC)
            target_compile_options(build_behavior PRIVATE /W4 /wd4100 /MP)
        endif()

        # havok-core-cli: the compile/decompile YAML<->HKX gate (supersedes
        # build_behavior; the plugin build calls this).
        add_executable(havok-core-cli
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/havok_core_cli.cpp")
        set_target_properties(havok-core-cli PROPERTIES
            CXX_STANDARD          23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS        OFF
            OUTPUT_NAME           "havok-core-cli"
        )
        target_link_libraries(havok-core-cli PRIVATE havok-core havok-schema havok-io havok-model)
        if(MSVC)
            target_compile_options(havok-core-cli PRIVATE /W4 /wd4100 /MP)
        endif()
    endif()
endif()
