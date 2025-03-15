# Welcome :)
#
# Using CMake and this file, you can:
# - Compile the launcher and the dedicated server launcher
# - Generate Visual Studio solution/project files (please, please, PLEASE use the provided ones first)
#
# What you can't do with CMake and this file (yet):
# - Compile tier0 (basically all r1delta code) and vaudio_opus
#
# With that being said, you can get started with these commands (on linux and msys2 [windows]):
# mkdir build && cd build
# cmake -DCMAKE_TOOLCHAIN_FILE=../cmake/mingw64-toolchain.cmake ..
# make
#
# If you're using this on Windows and you want to use Visual Studio you don't need to pass the toolchain parameter.
cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    # Always compile on release by default
    set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(r1delta)

if(MSVC)
    message("-- Using MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DUNICODE /D_UNICODE /W0 /sdl /MP /Gy /permissive-")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Oi")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SUBSYSTEM:WINDOWS /STACK:0x7A1200")
else()
    message("-- NOT using MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -w -mwindows")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g2 -gdwarf-2")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -s")
endif()

add_subdirectory(launch)
add_subdirectory(dedi_launch)
##add_subdirectory(vaudio_opus)
##add_subdirectory(vsdk)

## here goes the tier0 impl
#project(tier0)

#file(GLOB TIER0SRC_FILES "${PROJECT_SOURCE_DIR}/*.cpp ${PROJECT_SOURCE_DIR}/*.cc")
#file(GLOB TIER0H_FILES "${PROJECT_SOURCE_DIR}/*.h ${PROJECT_SOURCE_DIR}/*.hh")
#file(GLOB TIER0RC_FILES "${PROJECT_SOURCE_DIR/*.rc")

#if(CMAKE_BUILD_TYPE STREQUAL "Debug")
#    add_definitions(-DDEBUG)
#endif()
#if(CMAKE_BUILD_TYPE STREQUAL "Release")
#    add_definitions(-DNDEBUG)
#endif()

#add_definitions(-DWIN32
#    -DWINDOWS
#    -DUSRDLL
#    -DDLL1_EXPORTS
#    -DWIN32_LEAN_AND_MEAN
#    -D_CRT_SECURE_NO_WARNINGS
#)

#add_library(tier0 SHARED ${TIER0SRC_FILES} ${TIER0H_FILES} ${TIER0RC_FILES})
#target_link_libraries(tier0 winhttp shell32 vstdlib tier0_orig)