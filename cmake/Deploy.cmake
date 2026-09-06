# cb_deploy(TARGET MOD_NAME) — POST_BUILD copy of the plugin DLL into the MO2 mods folder as a
# "<MOD_NAME>" mod (SKSE convention: <mod>/SKSE/Plugins/<dll>). So a build immediately refreshes the
# in-game plugin. Content deploy (Havok/, hky/, Data/) gets added here as those move in.
function(cb_deploy TARGET MOD_NAME)
    if(NOT DEFINED ENV{SKYRIM_MODS_FOLDER})
        message(STATUS "cb_deploy: SKYRIM_MODS_FOLDER unset — skipping deploy of ${TARGET}")
        return()
    endif()
    set(_dst "$ENV{SKYRIM_MODS_FOLDER}/${MOD_NAME}/SKSE/Plugins")
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_dst}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${TARGET}>" "${_dst}/"
        COMMENT "cb_deploy: ${TARGET}.dll -> ${MOD_NAME}/SKSE/Plugins"
        VERBATIM)
endfunction()
