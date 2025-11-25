set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_C_FLAGS "-m32")
set(CMAKE_CXX_FLAGS "-m32")
set(CMAKE_EXE_LINKER_FLAGS "-m32")

# Provide raylib location for cross target
set(RAYLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/raylib/win32/include)
set(RAYLIB_LIBRARY_DIR ${CMAKE_SOURCE_DIR}/raylib/win32/lib)
