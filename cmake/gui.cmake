# FLTK frontend. Included from the top-level CMakeLists.txt when
# ZEALFS_BUILD_GUI is enabled; everything it needs lives here so the core and
# CLI configuration stays readable.

# The FLTK frontend is C++17; every other translation unit stays plain C.
# C++ is only required when the GUI is enabled, so CLI-only builds do not
# need a C++ compiler.
enable_language(CXX)
add_executable(zeal_disk_tool
    src/gui/main.cpp
    src/gui/MainWindow.cpp
    src/gui/SidebarPanel.cpp
    src/gui/DiskMapPanel.cpp
    src/gui/DetailsPanel.cpp
    src/gui/FileBrowserPanel.cpp
    src/gui/FileBrowserDraw.cpp
    src/gui/ModernStyle.cpp
    src/gui/Dialogs.cpp
    src/gui/Utils.cpp
    src/gui/model.c)
target_include_directories(zeal_disk_tool PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated" include)
set_target_properties(zeal_disk_tool PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_link_libraries(zeal_disk_tool PRIVATE zealfs_core)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(zeal_disk_tool PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The frontend uses FLTK 1.4 drawing primitives (fl_rounded_rect() and
# friends), so older 1.3 packages are rejected with a clear message.
find_package(FLTK 1.4 CONFIG QUIET)
if(TARGET fltk::fltk)
    target_link_libraries(zeal_disk_tool PRIVATE fltk::fltk)
    # Fl_SVG_Image lives in the images library; it rasterises the bundled
    # Lucide icon sources at the requested pixel size.
    if(TARGET fltk::images)
        target_link_libraries(zeal_disk_tool PRIVATE fltk::images)
    endif()
else()
    find_program(FLTK_CONFIG_EXECUTABLE NAMES fltk-config REQUIRED)
    execute_process(COMMAND "${FLTK_CONFIG_EXECUTABLE}" --version
        OUTPUT_VARIABLE FLTK_CONFIG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(FLTK_CONFIG_VERSION VERSION_LESS 1.4)
        message(FATAL_ERROR
            "FLTK 1.4 or newer is required (found ${FLTK_CONFIG_VERSION}). "
            "Build FLTK from source or point CMAKE_PREFIX_PATH at a 1.4 installation.")
    endif()
    execute_process(COMMAND "${FLTK_CONFIG_EXECUTABLE}" --use-images --cxxflags
        OUTPUT_VARIABLE FLTK_CXX_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND "${FLTK_CONFIG_EXECUTABLE}" --use-images --ldflags
        OUTPUT_VARIABLE FLTK_LD_FLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
    separate_arguments(FLTK_CXX_FLAGS_LIST NATIVE_COMMAND "${FLTK_CXX_FLAGS}")
    separate_arguments(FLTK_LD_FLAGS_LIST NATIVE_COMMAND "${FLTK_LD_FLAGS}")
    set(FLTK_INCLUDE_FLAGS "")
    set(FLTK_DEFINE_FLAGS "")
    foreach(flag IN LISTS FLTK_CXX_FLAGS_LIST)
        if(flag MATCHES "^-I(.+)$")
            list(APPEND FLTK_INCLUDE_FLAGS "${CMAKE_MATCH_1}")
        else()
            list(APPEND FLTK_DEFINE_FLAGS "${flag}")
        endif()
    endforeach()
    if(FLTK_INCLUDE_FLAGS)
        target_include_directories(zeal_disk_tool SYSTEM PRIVATE ${FLTK_INCLUDE_FLAGS})
    endif()
    target_compile_options(zeal_disk_tool PRIVATE ${FLTK_DEFINE_FLAGS})
    target_link_libraries(zeal_disk_tool PRIVATE ${FLTK_LD_FLAGS_LIST})
endif()
if(UNIX)
    target_link_libraries(zeal_disk_tool PRIVATE m)
endif()
# Include platform-specific options
if(APPLE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/packages/darwin.cmake)
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
    include(${CMAKE_CURRENT_SOURCE_DIR}/packages/appimage.cmake)
    add_custom_target(gui_package DEPENDS linux_appimage)
    # General install target for Linux
    install(TARGETS zeal_disk_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
