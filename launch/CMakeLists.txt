project(launcher)

include_directories(.)
add_definitions(-D_WIN32_WINNT=0x0A00)

file(GLOB LAUNCHERSRC_FILES "${PROJECT_SOURCE_DIR}/*.cpp" "${PROJECT_SOURCE_DIR}/*.cc" "${PROJECT_SOURCE_DIR}/*.rc")
file(GLOB LAUNCHERH_FILES "${PROJECT_SOURCE_DIR}/*.h" "${PROJECT_SOURCE_DIR}/*.hh")

# Visual Studio folders
#source_group("Source Files" FILES ${LAUNCHERSRC_FILES})
#source_group("Header Files" FILES ${LAUNCHERH_FILES})

add_executable(${PROJECT_NAME} ${LAUNCHERSRC_FILES} ${LAUNCHERH_FILES})

target_compile_definitions(${PROJECT_NAME} PUBLIC
    "_CONSOLE"
    "$<$<CONFIG:Debug>:DEBUG>"
    "$<$<CONFIG:Debug>:_DEBUG>"
    "$<$<CONFIG:Release>:NDEBUG>"
)

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "R1Delta")

if(MSVC)
    target_link_libraries(${PROJECT_NAME} imm32 shlwapi ucrt "$<$<CONFIG:Debug>:ucrtd>")
    set_target_properties(${PROJECT_NAME} PROPERTIES
        LINK_FLAGS "/MAP /DEBUG /SUBSYSTEM:WINDOWS /STACK:\"0x7A1200\""
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "../"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "../"
    )
else()
    target_link_libraries(${PROJECT_NAME} imm32 shlwapi msvcrt)
endif()