# cmake -DBUNDLE_DIR=<dir> -P CheckLfsPointers.cmake
# Fails when any file under BUNDLE_DIR is still a Git LFS pointer stub (the clone never ran `git lfs pull`), so a
# feature bundle is never packed with pointer text in place of its animations.
if(NOT BUNDLE_DIR OR NOT IS_DIRECTORY "${BUNDLE_DIR}")
    message(FATAL_ERROR "CheckLfsPointers: BUNDLE_DIR '${BUNDLE_DIR}' is not a directory")
endif()

file(GLOB_RECURSE _files "${BUNDLE_DIR}/*")
set(_stubs 0)
foreach(_f ${_files})
    file(READ "${_f}" _head LIMIT 48)
    if(_head MATCHES "^version https://git-lfs\\.github\\.com/spec/v1")
        math(EXPR _stubs "${_stubs} + 1")
        if(_stubs LESS_EQUAL 3)
            message(STATUS "LFS pointer stub: ${_f}")
        endif()
    endif()
endforeach()

if(_stubs GREATER 0)
    message(FATAL_ERROR "CheckLfsPointers: ${_stubs} file(s) under ${BUNDLE_DIR} are Git LFS pointer stubs, not their "
                        "content. Run `git lfs pull` in the repository, then build again.")
endif()
