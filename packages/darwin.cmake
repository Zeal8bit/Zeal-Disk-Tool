# Find required macOS tools
find_program(SIPS_EXECUTABLE sips)
find_program(ICONUTIL_EXECUTABLE iconutil)

if(NOT SIPS_EXECUTABLE OR NOT ICONUTIL_EXECUTABLE)
    message(WARNING "sips or iconutil not found. Please run: xcode-select --install")
    message(WARNING "mac_app target will not be available")
else()
    # Configure Info.plist with version information
    configure_file(
        ${CMAKE_SOURCE_DIR}/packages/macos/Info.plist.in
        ${CMAKE_BINARY_DIR}/Info.plist
        @ONLY
    )

    # Create macOS .app bundle
    add_custom_target(mac_app
        # Create iconset directory
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset
        # Generate required icon sizes using sips
        COMMAND ${SIPS_EXECUTABLE} -z 16 16 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_16x16.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 32 32 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_16x16@2x.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 32 32 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_32x32.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 64 64 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_32x32@2x.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 128 128 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_128x128.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 256 256 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_128x128@2x.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 256 256 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_256x256.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 512 512 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_256x256@2x.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 512 512 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_512x512.png > /dev/null 2>&1
        COMMAND ${SIPS_EXECUTABLE} -z 1024 1024 ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.png --out ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset/icon_512x512@2x.png > /dev/null 2>&1
        # Convert iconset to icns
        COMMAND ${ICONUTIL_EXECUTABLE} -c icns ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset -o ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.icns
        # Create .app bundle directories
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/ZealDiskTool.app/Contents/MacOS
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/ZealDiskTool.app/Contents/Resources
        # Copy files into .app bundle
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:zeal_disk_tool> ${CMAKE_SOURCE_DIR}/ZealDiskTool.app/Contents/MacOS/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/Info.plist ${CMAKE_SOURCE_DIR}/ZealDiskTool.app/Contents/
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.icns ${CMAKE_SOURCE_DIR}/ZealDiskTool.app/Contents/Resources/
        # Clean up iconset directory
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.iconset
        DEPENDS zeal_disk_tool
        COMMENT "Creating macOS .app bundle with generated icon (version ${APP_VERSION})"
    )

    # Prompt to install macOS .app bundle to ~/Applications
    add_custom_target(mac_install
        COMMAND bash -c "read -p \"Install ZealDiskTool.app to ~/Applications? (y/n) \" -n 1 -r && echo && if [[ \$REPLY =~ ^[Yy]\$ ]]$<SEMICOLON> then rm -rf ~/Applications/ZealDiskTool.app && cp -R ${CMAKE_SOURCE_DIR}/ZealDiskTool.app ~/Applications/ && echo \"Installed to ~/Applications/ZealDiskTool.app\"$<SEMICOLON> else echo \"Installation cancelled\"$<SEMICOLON> fi"
        DEPENDS mac_app
        COMMENT "Installing macOS .app bundle to ~/Applications"
        VERBATIM
    )
endif()

# Make install depend on mac_app target - this must come BEFORE the directory install
install(CODE "execute_process(COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target mac_app)")

# Configure standard install target to install .app bundle
install(DIRECTORY ${CMAKE_SOURCE_DIR}/ZealDiskTool.app
        DESTINATION "$ENV{HOME}/Applications"
        USE_SOURCE_PERMISSIONS)