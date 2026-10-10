# ── config ─────────────────────────────────────────────────────────────────────
# CB's config discovery + loading: one deterministically-ordered directory scanner + per-format adapters
# (JSON / YAML / INI) behind a single umbrella header, <config/SctConfig.h> (include root common/). It
# pulls in all three parsers (nlohmann/json, ryml, SimpleIni) because each adapter's Doc holds the parser
# type by value. external/ (the RymlInclude.h shim + header-only SimpleIni.h) is public for the same reason.
sct_collect_sources(CB_CONFIG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

add_library(config STATIC ${CB_CONFIG_SOURCES})
target_include_directories(config
    PUBLIC "${CMAKE_SOURCE_DIR}/common"
           "${CMAKE_CURRENT_SOURCE_DIR}/external")

find_package(nlohmann_json CONFIG REQUIRED)
find_package(ryml CONFIG REQUIRED)
target_link_libraries(config PUBLIC nlohmann_json::nlohmann_json ryml::ryml)

set_target_properties(config PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(config PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
