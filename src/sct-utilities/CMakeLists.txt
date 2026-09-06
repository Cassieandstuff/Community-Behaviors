# ── sct-utilities ─────────────────────────────────────────────────────────────
# Desktop-app plumbing for the Behavior Converter: native folder/file dialogs,
# Windows known-folder lookups, and a directory->zip writer. One umbrella header
# <sct-utilities/SctUtilities.h>; Zip.cpp is backed by vendored miniz (private).

sct_collect_sources(SCT_UTIL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/cpp/*.cpp")

set(_MINIZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
add_library(sct-utilities STATIC ${SCT_UTIL_SOURCES} "${_MINIZ_DIR}/miniz.c")
if(MSVC)
    set_source_files_properties("${_MINIZ_DIR}/miniz.c" PROPERTIES COMPILE_FLAGS "/w")
endif()

# include/ → <sct-utilities/SctUtilities.h>; miniz.h is a private impl detail of Zip.cpp.
target_include_directories(sct-utilities
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${_MINIZ_DIR}")

# ole32 (IFileDialog/COM) + shell32 (SHGetKnownFolderPath) PUBLIC so a consumer links them transitively.
target_link_libraries(sct-utilities PUBLIC ole32 shell32)

set_target_properties(sct-utilities PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sct-utilities PRIVATE /W4 /wd4100 /MP /EHsc)
endif()
