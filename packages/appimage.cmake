# Deploy targets (AppImage), only for Linux
add_custom_target(linux_appimage
    COMMAND cp -R ${CMAKE_SOURCE_DIR}/packages/appdir ${CMAKE_BINARY_DIR}/
    COMMAND cp $<TARGET_FILE:zeal_disk_tool> ${CMAKE_BINARY_DIR}/packages/appdir/zeal_disk_tool
    # Needs linuxdeploy binary in PATH
    COMMAND linuxdeploy --appdir AppDeploy
                        --executable=packages/appdir/zeal_disk_tool
                        --desktop-file packages/appdir/zeal-disk-tool.desktop
                        --icon-file packages/icons/zeal-disk-tool.png
                        --output appimage
    DEPENDS zeal_disk_tool
    COMMENT "Creating Linux .AppImage bundle"
)