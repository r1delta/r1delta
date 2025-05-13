project(launcher)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-D_WIN32_WINNT=0x0A00)

file(GLOB_RECURSE LAUNCHER_SRC 
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" 
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)

add_executable(${PROJECT_NAME} WIN32 ${LAUNCHER_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES 
  OUTPUT_NAME "R1Delta"
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  "$<$<CONFIG:Debug>:DEBUG>"
  "$<$<CONFIG:Debug>:_DEBUG>"
  "$<$<CONFIG:Release>:NDEBUG>"
)

if(MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    imm32
    shlwapi
    "$<$<CONFIG:Debug>:ucrtd>"
    "$<$<CONFIG:Release>:ucrt>"
  )
  
  target_link_options(${PROJECT_NAME} PRIVATE
    /MAP
    /DEBUG
    /STACK:"0x7A1200"
  )
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE
    imm32
    shlwapi
    "$<$<CONFIG:Debug>:ucrtd>"
    "$<$<CONFIG:Release>:ucrt>"
  )
endif()