# ── sct-config ────────────────────────────────────────────────────────────────
# CB's config discovery + loading: one deterministically-ordered directory scanner
# + per-format adapters (JSON / YAML / INI) behind a single umbrella header. The
# runtime resolver (SymbolInjector, BundleManifest) reads its declarations through
# this. Consumers: #include <sct-config/SctConfig.h> — it pulls in all three
# parsers (nlohmann/json, ryml, SimpleIni) because each adapter's Doc holds the
# parser type by value.

sct_collect_sources(SCT_CONFIG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

add_library(sct-config STATIC ${SCT_CONFIG_SOURCES})

# include/ → <sct-config/SctConfig.h>; include/external → the RymlInclude.h shim and
# header-only SimpleIni.h, part of the public surface of the adapter headers (and
# used directly by the plugin's Plugin.cpp).
target_include_directories(sct-config
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
           ${CMAKE_CURRENT_SOURCE_DIR}/include/external)

# nlohmann_json + ryml are PUBLIC — they appear in the umbrella's adapter structs.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ryml CONFIG REQUIRED)
target_link_libraries(sct-config PUBLIC nlohmann_json::nlohmann_json ryml::ryml)

set_target_properties(sct-config PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sct-config PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
