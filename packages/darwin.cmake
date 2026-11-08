find_program(SIPS_EXECUTABLE sips REQUIRED)
find_program(ICONUTIL_EXECUTABLE iconutil REQUIRED)
find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
set(MAC_BUNDLE "${CMAKE_BINARY_DIR}/ZealDiskTool.app")
set(MAC_ICONSET "${CMAKE_BINARY_DIR}/zeal-disk-tool.iconset")
configure_file(packages/macos/Info.plist.in "${CMAKE_BINARY_DIR}/Info.plist" @ONLY)
set(ICON_COMMANDS)
foreach(SIZE 16 32 128 256 512)
    math(EXPR DOUBLE_SIZE "${SIZE} * 2")
    list(APPEND ICON_COMMANDS COMMAND "${SIPS_EXECUTABLE}" -z ${SIZE} ${SIZE}
        "${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png" --out "${MAC_ICONSET}/icon_${SIZE}x${SIZE}.png"
        COMMAND "${SIPS_EXECUTABLE}" -z ${DOUBLE_SIZE} ${DOUBLE_SIZE}
        "${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png" --out "${MAC_ICONSET}/icon_${SIZE}x${SIZE}@2x.png")
endforeach()
get_filename_component(RAYLIB_DEPENDENCY_DIR "${RAYLIB_LIBRARY}" DIRECTORY)
add_custom_target(mac_app
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MAC_BUNDLE}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MAC_ICONSET}"
    ${ICON_COMMANDS}
    COMMAND "${ICONUTIL_EXECUTABLE}" -c icns "${MAC_ICONSET}" -o "${CMAKE_BINARY_DIR}/zeal-disk-tool.icns"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MAC_BUNDLE}/Contents/MacOS" "${MAC_BUNDLE}/Contents/Resources"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:zeal_disk_tool>" "${MAC_BUNDLE}/Contents/MacOS/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/Info.plist" "${MAC_BUNDLE}/Contents/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/zeal-disk-tool.icns" "${MAC_BUNDLE}/Contents/Resources/"
    COMMAND ${CMAKE_COMMAND} "-DBUNDLE=${MAC_BUNDLE}" "-DLIBRARY_DIR=${RAYLIB_DEPENDENCY_DIR}"
        -P "${CMAKE_SOURCE_DIR}/packages/macos/fixup.cmake"
    COMMAND "${CODESIGN_EXECUTABLE}" --force --deep --sign - "${MAC_BUNDLE}"
    DEPENDS zeal_disk_tool
    COMMENT "Packaging macOS GUI and its dynamic libraries"
    VERBATIM)
install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" --build \"${CMAKE_BINARY_DIR}\" --target mac_app RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR \"macOS packaging failed\")
    endif()")
install(DIRECTORY "${MAC_BUNDLE}" DESTINATION Applications USE_SOURCE_PERMISSIONS)
