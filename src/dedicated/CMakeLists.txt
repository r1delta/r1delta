cmake_minimum_required(VERSION 3.13)
project(dedicated)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_definitions(-D_WIN32_WINNT=0x0A00)

set(DEDILAUNCHER_SRC 
  "${CMAKE_CURRENT_SOURCE_DIR}/main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/dedi_launch.rc"
)

add_executable(${PROJECT_NAME} ${DEDILAUNCHER_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES 
  OUTPUT_NAME "R1Delta_DS"
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  "_CONSOLE"
  "$<$<CONFIG:Debug>:DEBUG>"
  "$<$<CONFIG:Debug>:_DEBUG>"
  "$<$<CONFIG:Release>:NDEBUG>"
)

if(MSVC)
  target_link_libraries(${PROJECT_NAME} PRIVATE
    shlwapi
    "$<$<CONFIG:Debug>:ucrtd>"
    "$<$<CONFIG:Release>:ucrt>"
  )
  
  target_link_options(${PROJECT_NAME} PRIVATE
    /MAP
    /DEBUG
    /SUBSYSTEM:CONSOLE
    /STACK:"0x7A1200"
  )
else()
  target_link_libraries(${PROJECT_NAME} PRIVATE
    shlwapi
    "$<$<CONFIG:Debug>:ucrtd>"
    "$<$<CONFIG:Release>:ucrt>"
  )
endif()