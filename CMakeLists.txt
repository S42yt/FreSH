cmake_minimum_required(VERSION 3.16)
project(FreSH C)
enable_language(RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(makeicon ${CMAKE_SOURCE_DIR}/tools/makeicon.c)
add_executable(bin2c ${CMAKE_SOURCE_DIR}/tools/bin2c.c)
if(NOT MSVC)
    target_link_libraries(makeicon m)
endif()

add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/fresh.ico ${CMAKE_SOURCE_DIR}/installation/fresh.ico
    COMMAND makeicon ${CMAKE_SOURCE_DIR}/src/fresh.ico
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/fresh.ico
            ${CMAKE_SOURCE_DIR}/installation/fresh.ico
    DEPENDS makeicon
)

file(GLOB FRESH_SOURCES ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(FreSH ${FRESH_SOURCES} ${CMAKE_SOURCE_DIR}/src/fresh.rc
                     ${CMAKE_SOURCE_DIR}/src/fresh.ico)
target_compile_definitions(FreSH PRIVATE _WIN32_WINNT=0x0601)
target_link_libraries(FreSH advapi32)

add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/installation/payload.h
    COMMAND bin2c $<TARGET_FILE:FreSH> ${CMAKE_SOURCE_DIR}/installation/payload.h FRESH_PAYLOAD
    DEPENDS FreSH bin2c
)

file(GLOB INSTALLER_SOURCES ${CMAKE_SOURCE_DIR}/installation/*.c)
add_executable(FreSH-Setup ${INSTALLER_SOURCES} ${CMAKE_SOURCE_DIR}/installation/setup.rc
                           ${CMAKE_SOURCE_DIR}/installation/payload.h
                           ${CMAKE_SOURCE_DIR}/installation/fresh.ico)
target_compile_definitions(FreSH-Setup PRIVATE _WIN32_WINNT=0x0601)
target_link_libraries(FreSH-Setup ole32 uuid shell32 advapi32 user32)
