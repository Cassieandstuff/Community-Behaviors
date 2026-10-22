# CopySchemaData.cmake — copy one top-level schema/ entry to DST, leaving C++ sources out. The schema ships as data
# (the runtime loads its *.yaml); a C++ source never ships (the reimplemented runtime lives in havok/, outside schema/).
# Only folders that hold a shipped file are created, so a sources-only folder leaves nothing behind.
#
# Invoked as a script:  cmake -DSRC=<schema/entry> -DDST=<dest dir> -P CopySchemaData.cmake

if(NOT DEFINED SRC OR NOT DEFINED DST)
    message(FATAL_ERROR "CopySchemaData.cmake: SRC and DST are required")
endif()

set(_source_ext "\\.(h|hpp|inl|cpp)$")

if(IS_DIRECTORY "${SRC}")
    get_filename_component(_name "${SRC}" NAME)
    file(GLOB_RECURSE _files RELATIVE "${SRC}" "${SRC}/*")
    foreach(_f IN LISTS _files)
        if(_f MATCHES "${_source_ext}")
            continue()
        endif()
        get_filename_component(_dir "${_f}" DIRECTORY)
        file(COPY "${SRC}/${_f}" DESTINATION "${DST}/${_name}/${_dir}")
    endforeach()
elseif(NOT SRC MATCHES "${_source_ext}")
    file(COPY "${SRC}" DESTINATION "${DST}")
endif()
