set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_C_FLAGS "-m32")
set(CMAKE_CXX_FLAGS "-m32")
set(CMAKE_EXE_LINKER_FLAGS "-m32")

# GUI cross-builds need a 32-bit FLTK installation for the target; point
# FLTK_DIR or CMAKE_PREFIX_PATH at it. The core and CLI need no GUI dependency.
