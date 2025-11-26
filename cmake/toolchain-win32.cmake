set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)

# Provide raylib location for cross target
set(RAYLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/raylib/win32/include)
set(RAYLIB_LIBRARY_DIR ${CMAKE_SOURCE_DIR}/raylib/win32/lib)
