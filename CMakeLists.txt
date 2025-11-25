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

if(PLATFORM STREQUAL "linux")
    target_compile_options(zeal_disk_tool PRIVATE "-Wno-format-truncation")
elseif(PLATFORM STREQUAL "win")
    # Windows icon .res
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/zeal-disk-tool.res
        COMMAND ${CMAKE_RC_COMPILER}
                ${CMAKE_SOURCE_DIR}/appdir/zeal-disk-tool.rc
                -O coff -o ${CMAKE_BINARY_DIR}/zeal-disk-tool.res
        DEPENDS appdir/zeal-disk-tool.rc appdir/zeal-disk-tool.ico)
    # For Windows, we need to link a bit more libraries
    target_link_libraries(zeal_disk_tool PRIVATE winmm gdi32 ole32 comctl32)
    # Make the Windows target static, as such, no need for any .dll file
    target_link_options(zeal_disk_tool PRIVATE -static -mwindows)
endif()

# Deploy targets (AppImage), only for Linux
add_custom_target(deploy
    COMMAND cp -R ${CMAKE_SOURCE_DIR}/appdir ${CMAKE_BINARY_DIR}/
    COMMAND cp $<TARGET_FILE:zeal_disk_tool> ${CMAKE_BINARY_DIR}/appdir/zeal_disk_tool
    # Needs linuxdeploy binary in PATH
    COMMAND linuxdeploy --appdir AppDeploy
                        --executable=appdir/zeal_disk_tool
                        --desktop-file appdir/zeal-disk-tool.desktop
                        --icon-file appdir/zeal-disk-tool.png
                        --output appimage
    DEPENDS zeal_disk_tool
)
