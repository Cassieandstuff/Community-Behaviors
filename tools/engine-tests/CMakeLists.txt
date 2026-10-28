# ── tools/engine-tests ───────────────────────────────────────────────────
# engine-tests — unit tests over the engine lib (never deployed), run against the source schema/ tree.

add_executable(engine-tests EngineTests.cpp RulesTests.cpp)
target_link_libraries(engine-tests PRIVATE engine)

set_target_properties(engine-tests PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(engine-tests PRIVATE /W4 /wd4100 /MP /EHsc)
endif()

add_test(NAME engine-tests COMMAND engine-tests "${CMAKE_SOURCE_DIR}/schema")
