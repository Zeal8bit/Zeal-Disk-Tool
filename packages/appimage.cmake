# Optional: install linuxdeploy matching the target architecture to build an AppImage.
add_custom_target(linux_appimage
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/appimage"
    COMMAND linuxdeploy --appdir "${CMAKE_BINARY_DIR}/appimage/AppDir"
        --executable "$<TARGET_FILE:zeal_disk_tool>"
        --desktop-file "${CMAKE_SOURCE_DIR}/packages/appdir/zeal-disk-tool.desktop"
        --icon-file "${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png"
        --output appimage
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS zeal_disk_tool
    COMMENT "Creating a Linux GUI AppImage"
    VERBATIM)
