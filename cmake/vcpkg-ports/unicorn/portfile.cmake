vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO unicorn-engine/unicorn
    REF "${VERSION}"
    SHA512 c9ae4230a20b77e0187cde33dbf4827b3504b6c24debd61fc79ec9c13fa2051335c834c101433cebbbc8e3baadae56212b79c5922bf37ea1f777d66d8e67b495
    HEAD_REF master
    PATCHES
        fix-build.patch
        fix-msvc-shared.patch
)

if(VCPKG_TARGET_IS_ANDROID)
    vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt"
        "-lpthread"
        " "
    )
endif()

if(VCPKG_TARGET_IS_WINDOWS)
    vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt"
        "-lpthread -lm"
        " "
    )
endif()

# CB: the v1-style static archive gets a "unicorn.o" alias by symlink, which needs Developer Mode or admin on
# Windows; a copy serves the same purpose.
vcpkg_replace_string("${SOURCE_PATH}/cmake/bundle_static.cmake" "-E create_symlink" "-E copy")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DUNICORN_BUILD_TESTS=OFF
        # CB: only the x86 target cbmatch emulates.
        "-DUNICORN_ARCH=x86"
)

vcpkg_cmake_install()
vcpkg_fixup_pkgconfig()
vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
