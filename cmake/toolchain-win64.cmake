set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# GUI cross-builds need a 64-bit MinGW FLTK installation for the target.
# Point FLTK_DIR or CMAKE_PREFIX_PATH at it; the core and CLI need no GUI dependency.
