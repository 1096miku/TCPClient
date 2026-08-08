cmake_minimum_required(VERSION 3.10)
project(tcp_chat_client C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# ---- Client source files ----
set(SOURCES
    src/main.c
    src/app.c
    src/conn.c
    src/commands.c
    src/ui.c
    src/protocol.c
    src/utils.c
)

# ---- Executable ----
add_executable(tcp_client ${SOURCES})
target_include_directories(tcp_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---- Unit tests ----
enable_testing()

add_executable(test_protocol
    tests/test_protocol.c
    src/protocol.c
    src/utils.c
)
target_include_directories(test_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_test(NAME test_protocol COMMAND test_protocol)
