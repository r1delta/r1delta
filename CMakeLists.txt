cmake_minimum_required(VERSION 3.13)
project(tier0 LANGUAGES C CXX ASM)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(BUILD_TESTING OFF CACHE BOOL "Disable tests in all submodules" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "Disable mimalloc tests" FORCE)
set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann_json tests" FORCE)

if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
  enable_language(ASM_MASM)
  set(ASM_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/netchan_fix.asm")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -D_WINSOCK_DEPRECATED_NO_WARNINGS)

add_subdirectory(vendor/minhook)
add_subdirectory(vendor/mimalloc)
add_subdirectory(vendor/nlohmann)
add_subdirectory(vendor/cpp-httplib)
add_subdirectory(vendor/zstd/build/cmake)
add_subdirectory(vendor/ogg)
add_subdirectory(vendor/vorbis)

file(GLOB_RECURSE OTHER_SRC_FILES "src/*.cpp")

set(DELTASRCS
  ${OTHER_SRC_FILES}
  "${CMAKE_CURRENT_SOURCE_DIR}/src/security_fixes.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/arena.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/tctx.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/tier0/dbg.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/tier0/platformtime.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/tier0/valve_tracelogging.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/tier1/ipv6text.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/tier1/netadr.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/tier1/utlbuffer.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/vsdk/vstdlib/strtools.cpp"
  ${ASM_FILES}
)

list(REMOVE_DUPLICATES DELTASRCS)

list(FILTER DELTASRCS EXCLUDE REGEX "src/launch/main.cpp$")
list(FILTER DELTASRCS EXCLUDE REGEX "src/dedi_launch/main.cpp$")

add_library(${PROJECT_NAME} SHARED ${DELTASRCS})

# Copy mimalloc.dll
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  $<TARGET_FILE:mimalloc>
  $<TARGET_FILE_DIR:${PROJECT_NAME}>
  COMMENT "Copying mimalloc.dll to executable directory")

# Copy mimalloc-redirect.dll
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "$<TARGET_FILE_DIR:mimalloc>/mimalloc-redirect.dll"
  $<TARGET_FILE_DIR:${PROJECT_NAME}>
  COMMENT "Copying mimalloc-redirect.dll to executable directory")

# Copy tier0_orig.dll
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${CMAKE_CURRENT_SOURCE_DIR}/libs/tier0_orig.dll"
  $<TARGET_FILE_DIR:${PROJECT_NAME}>
  COMMENT "Copying tier0_orig.dll to executable directory")

target_include_directories(${PROJECT_NAME} PRIVATE
  src
  vendor/
  vendor/minhook/include
  vendor/mimalloc/include
  vendor/nlohmann/include
  vendor/zstd/lib
  vendor/ogg/include
  vendor/vorbis/include
  vendor/vsdk/public
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  minhook
  mimalloc
  nlohmann_json
  httplib
  libzstd_static
  winhttp
  shell32
  Shlwapi
  ws2_32
  crypt32
  ${CMAKE_SOURCE_DIR}/libs/vstdlib.lib
  ${CMAKE_SOURCE_DIR}/libs/tier0_orig.lib
  vorbis
  vorbisfile
)