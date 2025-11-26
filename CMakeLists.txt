cmake_minimum_required(VERSION 3.16)
project(zeal_disk_tool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/GenerateVersion.cmake)
generate_version_header(${CMAKE_CURRENT_SOURCE_DIR}/include/app_version.h)

# List the source files that are common to all OS
set(SRCS
    src/main.c
    src/disk.c
    src/ui/popup.c
    src/ui/combo_disk.c
    src/ui/message_box.c
    src/ui/menubar.c
    src/ui/statusbar.c
    src/ui/partition_viewer.c
    src/zealfs/zealfs_v2.c
    src/ui/tinyfiledialogs.c
    src/raylib-nuklear.c)

message(STATUS "Building for platform: ${CMAKE_SYSTEM_NAME}")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(PLATFORM mac)
    list(APPEND SRCS "src/disk_mac.c")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(PLATFORM win)
    list(APPEND SRCS "src/disk_win.c")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM linux)
    list(APPEND SRCS "src/disk_linux.c")
endif()

# Check for Raylib path, either from the system path or from the toolchain file
if(NOT DEFINED RAYLIB_INCLUDE_DIR)
    find_path(RAYLIB_INCLUDE_DIR raylib.h)
endif()

if(NOT DEFINED RAYLIB_LIBRARY_DIR)
    find_library(RAYLIB_LIBRARY raylib)
endif()


# Create the executable and give it all the sources gathered
add_executable(zeal_disk_tool "${SRCS}")

target_include_directories(zeal_disk_tool PRIVATE "include" ${RAYLIB_INCLUDE_DIR})
target_link_directories(zeal_disk_tool PRIVATE ${RAYLIB_LIBRARY_DIR})

# Libraries to link to DiskTool regardless of the OS we are building for
target_link_libraries(zeal_disk_tool PRIVATE raylib m)

# Include platform-specific options
if(PLATFORM STREQUAL "linux")
    target_compile_options(zeal_disk_tool PRIVATE "-Wno-format-truncation")
    include(packages/appimage.cmake)
    # General install target for Linux
    add_custom_target(package
        DEPENDS linux_appimage
        COMMENT "Building Linux package"
    )
elseif(PLATFORM STREQUAL "mac")
    include(packages/darwin.cmake)
    # General install target for macOS
    add_custom_target(package
        DEPENDS mac_app
        COMMENT "Building macOS package"
    )
elseif(PLATFORM STREQUAL "win")
    # Windows icon .res
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/zeal-disk-tool.res
        COMMAND ${CMAKE_RC_COMPILER}
                ${CMAKE_SOURCE_DIR}/packages/icons/zeal-disk-tool.rc
                -O coff -o ${CMAKE_BINARY_DIR}/zeal-disk-tool.res
        DEPENDS packages/icons/zeal-disk-tool.rc packages/icons/zeal-disk-tool.ico)
    # For Windows, we need to link a bit more libraries
    target_link_libraries(zeal_disk_tool PRIVATE winmm gdi32 ole32 comctl32)
    # Make the Windows target static, as such, no need for any .dll file
    target_link_options(zeal_disk_tool PRIVATE -static -mwindows)
endif()

# Clean deployment artifacts - this will run automatically before clean
set_directory_properties(PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES
    "${CMAKE_SOURCE_DIR}/ZealDiskTool.app;${CMAKE_SOURCE_DIR}/packages/macos/zeal-disk-tool.icns;${CMAKE_BINARY_DIR}/packages;${CMAKE_SOURCE_DIR}/zeal_disk_tool;${CMAKE_SOURCE_DIR}/zeal_disk_tool.exe"
)

# Additional manual clean for wildcard files
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND bash -c "rm -f ${CMAKE_SOURCE_DIR}/*.AppImage"
    COMMENT "Cleaning all build and deployment artifacts"
)

