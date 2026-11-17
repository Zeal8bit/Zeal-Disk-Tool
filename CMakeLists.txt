cmake_minimum_required(VERSION 3.20)
project(zeal_disk_tool LANGUAGES C)
# Prefer an existing Zeal Native installation's prefix for native builds.
# Explicit prefixes always take precedence; cross builds use target defaults.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(NOT CMAKE_CROSSCOMPILING)
        find_program(ZEAL_NATIVE_EXECUTABLE NAMES zeal-native)
        if(ZEAL_NATIVE_EXECUTABLE)
            get_filename_component(ZEAL_NATIVE_BINDIR "${ZEAL_NATIVE_EXECUTABLE}" DIRECTORY)
            get_filename_component(ZEAL_NATIVE_BINDIR_NAME "${ZEAL_NATIVE_BINDIR}" NAME)
            if(ZEAL_NATIVE_BINDIR_NAME STREQUAL "bin")
                get_filename_component(ZEAL_NATIVE_PREFIX "${ZEAL_NATIVE_BINDIR}" DIRECTORY)
                set(CMAKE_INSTALL_PREFIX "${ZEAL_NATIVE_PREFIX}" CACHE PATH "Installation prefix" FORCE)
            endif()
        endif()
    endif()
    if(NOT ZEAL_NATIVE_PREFIX AND APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        set(CMAKE_INSTALL_PREFIX "/opt/homebrew" CACHE PATH "Installation prefix" FORCE)
    endif()
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(ZEALFS_BUILD_GUI "Build the FLTK frontend" ON)
option(ZEALFS_BUILD_CLI "Build the zdt console frontend" ON)
option(ZEALFS_BUILD_TESTS "Build image-based core tests" ON)
include(GNUInstallDirs)
include(cmake/GenerateVersion.cmake)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
generate_version_header("${CMAKE_CURRENT_BINARY_DIR}/generated/app_version.h")

if(APPLE)
    set(PLATFORM mac)
    set(NATIVE_SOURCES src/zealfs/disk_mac.c src/zealfs/native_posix.c src/zealfs/host_posix.c)
elseif(WIN32)
    set(PLATFORM win)
    set(NATIVE_SOURCES src/zealfs/disk_win.c src/zealfs/host_win.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM linux)
    set(NATIVE_SOURCES src/zealfs/disk_linux.c src/zealfs/native_posix.c src/zealfs/host_posix.c)
else()
    message(FATAL_ERROR "Supported platforms: macOS, Linux, Windows")
endif()
# List the source files that are common to all OS
add_library(zealfs_core STATIC src/zealfs/export.c src/zealfs/transfer.c src/zealfs/core.c src/zealfs/host.c src/zealfs/zealfs_v2.c ${NATIVE_SOURCES})
target_include_directories(zealfs_core PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if(NOT WIN32)
    target_compile_definitions(zealfs_core PUBLIC _FILE_OFFSET_BITS=64)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zealfs_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS zealfs_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/disk_tool.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(DIRECTORY docs md_imgs DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(ZEALFS_BUILD_GUI)
    # The FLTK frontend is C++17; every other translation unit stays plain C.
    # C++ is only required when the GUI is enabled, so CLI-only builds do not
    # need a C++ compiler.
    enable_language(CXX)
    add_executable(zeal_disk_tool
        src/gui/main.cpp
        src/gui/MainWindow.cpp
        src/gui/BrowserWindow.cpp
        src/gui/PartitionBar.cpp
        src/gui/Dialogs.cpp
        src/gui/Gui.cpp
        src/gui/model.c)
    target_include_directories(zeal_disk_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" include)
    set_target_properties(zeal_disk_tool PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(zeal_disk_tool PRIVATE zealfs_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(zeal_disk_tool PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Prefer FLTK's modern CMake targets (FLTK 1.4). System packages of FLTK
    # 1.3 ship a config module without imported targets, so fall back to the
    # fltk-config helper, which also carries the platform link libraries.
    find_package(FLTK CONFIG QUIET)
    if(TARGET fltk::fltk)
        target_link_libraries(zeal_disk_tool PRIVATE fltk::fltk)
    elseif(TARGET fltk)
        target_link_libraries(zeal_disk_tool PRIVATE fltk)
    else()
        find_program(FLTK_CONFIG_EXECUTABLE NAMES fltk-config REQUIRED)
        execute_process(COMMAND "${FLTK_CONFIG_EXECUTABLE}" --cxxflags
            OUTPUT_VARIABLE FLTK_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        execute_process(COMMAND "${FLTK_CONFIG_EXECUTABLE}" --ldflags
            OUTPUT_VARIABLE FLTK_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
        separate_arguments(FLTK_CXX_FLAGS_LIST NATIVE_COMMAND "${FLTK_CXX_FLAGS}")
        separate_arguments(FLTK_LD_FLAGS_LIST NATIVE_COMMAND "${FLTK_LD_FLAGS}")
        target_compile_options(zeal_disk_tool PRIVATE ${FLTK_CXX_FLAGS_LIST})
        target_link_libraries(zeal_disk_tool PRIVATE ${FLTK_LD_FLAGS_LIST})
    endif()
    if(UNIX)
        target_link_libraries(zeal_disk_tool PRIVATE m)
    endif()
    # Include platform-specific options
    if(APPLE)
        include(packages/darwin.cmake)
        add_custom_target(gui_package DEPENDS mac_app)
    elseif(WIN32)
        # Windows icon .res
        enable_language(RC)
        target_sources(zeal_disk_tool PRIVATE packages/appdir/zeal-disk-tool.rc)
        target_include_directories(zeal_disk_tool PRIVATE packages/icons packages/appdir)
        # FLTK itself may be static in cross builds; keep the common Win32 deps.
        target_link_libraries(zeal_disk_tool PRIVATE comdlg32 comctl32 gdi32 ole32 shell32 uuid winmm)
        set_target_properties(zeal_disk_tool PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MINGW)
            # Make the Windows target static, as such, no need for any .dll file
            target_link_options(zeal_disk_tool PRIVATE -static)
        endif()
        install(TARGETS zeal_disk_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    else()
        include(packages/appimage.cmake)
        add_custom_target(gui_package DEPENDS linux_appimage)
        # General install target for Linux
        install(TARGETS zeal_disk_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()
endif()
if(ZEALFS_BUILD_CLI)
    add_executable(zdt
        src/cli/main.c
        src/cli/args.c
        src/cli/transfer.c
        src/cli/commands.c
        src/cli/help.c
        src/cli/output.c
        src/cli/session.c
        src/cli/terminal.c
        src/cli/shell.c)
    target_include_directories(zdt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(zdt PRIVATE zealfs_core)
    if(WIN32)
        target_link_libraries(zdt PRIVATE shell32)
    endif()
    if(MINGW)
        target_link_options(zdt PRIVATE -static)
    endif()
    install(TARGETS zdt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT CLI)
endif()
if(ZEALFS_BUILD_TESTS)
    enable_testing()
    add_executable(zealfs_core_test tests/core_test.c)
    target_link_libraries(zealfs_core_test PRIVATE zealfs_core)
    add_test(NAME core COMMAND zealfs_core_test)
    add_executable(zealfs_device_test tests/device_test.c)
    target_include_directories(zealfs_device_test PRIVATE src/zealfs)
    target_link_libraries(zealfs_device_test PRIVATE zealfs_core)
    add_test(NAME device_failure COMMAND zealfs_device_test)
    add_executable(zealfs_merge_device_test tests/merge_device_test.c)
    target_include_directories(zealfs_merge_device_test PRIVATE src/zealfs)
    target_link_libraries(zealfs_merge_device_test PRIVATE zealfs_core)
    add_test(NAME merge_device COMMAND zealfs_merge_device_test)
    if(ZEALFS_BUILD_CLI)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            add_test(NAME bulk_cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/bulk_cli_test.py $<TARGET_FILE:zdt>)
            add_test(NAME export_cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/export_cli_test.py $<TARGET_FILE:zdt>)
            add_test(NAME merge_cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/merge_cli_test.py $<TARGET_FILE:zdt>)
            if(UNIX)
                add_test(NAME shell_history COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/shell_history_test.py $<TARGET_FILE:zdt>)
            endif()
            add_test(NAME cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_test.py $<TARGET_FILE:zdt>)
        endif()
    endif()
endif()

# Portable binary archives; packaging stays in the build tree.
set(CPACK_PACKAGE_NAME "zdt")
set(CPACK_PACKAGE_VERSION "${APP_NUMERIC_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "zdt-${APP_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
if(WIN32)
    set(CPACK_GENERATOR ZIP)
else()
    set(CPACK_GENERATOR TGZ)
endif()
include(CPack)
