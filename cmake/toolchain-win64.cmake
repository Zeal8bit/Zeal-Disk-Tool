set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Provide raylib location for cross target
if(NOT DEFINED RAYLIB_INCLUDE_DIR)
    set(RAYLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/raylib/win64/include)
endif()
if(NOT DEFINED RAYLIB_LIBRARY_DIR)
    set(RAYLIB_LIBRARY_DIR ${CMAKE_SOURCE_DIR}/raylib/win64/lib)
endif()
