function(generate_version_header out)
    execute_process(
        COMMAND git describe --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE _git_result
        OUTPUT_VARIABLE GIT_VERSION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(NOT _git_result EQUAL 0)
        set(GIT_VERSION "local")
    endif()

    # Export APP_VERSION to parent scope for use in package targets (Info.plist, etc.)
    # Use the same version string as the header file
    set(APP_VERSION "${GIT_VERSION}" PARENT_SCOPE)

    file(WRITE ${out} "#define VERSION \"${GIT_VERSION}\"\n")
    add_custom_target(version_header DEPENDS ${out})
endfunction()