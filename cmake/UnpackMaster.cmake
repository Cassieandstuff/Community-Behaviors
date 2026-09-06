# UnpackMaster.cmake — run in -P script mode by cb_deploy's POST_BUILD.
#
# Unpacks the deployed Skyrim.hky into a short D-drive root as its uncompressed YAML tree, so the
# "diff across builds" strategy always has a current unpacked master to compare (packed .hky files are
# NOT bit-reproducible — miniz/deflate differs across toolchain versions — so only the unpacked trees
# diff meaningfully). Keeps the PREVIOUS tree at <OUT>/previous and the current at <OUT>/current, so a
# two-point diff (previous vs current) needs no manual bookkeeping.
#
# Args: -DCLI=<havok-core-cli> -DMASTER=<deployed Skyrim.hky> -DOUT=<root dir>
# Tolerant: if MASTER doesn't exist yet (fresh checkout, no deploy), it skips without failing the build.

if(NOT EXISTS "${MASTER}")
    message(STATUS "unpack-master: no deployed master at '${MASTER}' — skipping (nothing to unpack yet).")
    return()
endif()

# Rotate current -> previous so the last build's tree survives for the diff.
if(EXISTS "${OUT}/current")
    file(REMOVE_RECURSE "${OUT}/previous")
    file(RENAME "${OUT}/current" "${OUT}/previous")
endif()

file(MAKE_DIRECTORY "${OUT}/current")
execute_process(
    COMMAND "${CLI}" hky-unpack "${MASTER}" -o "${OUT}/current"
    RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(WARNING "unpack-master: hky-unpack failed (rc=${_rc}) for '${MASTER}'.")
else()
    message(STATUS "unpack-master: '${MASTER}' -> ${OUT}/current (diff vs ${OUT}/previous).")
endif()
