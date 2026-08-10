cmake_minimum_required(VERSION 3.10)
project(tcp_chat_client C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 导出 compile_commands.json，供 VS Code 扩展（clangd/cpptools）自动获取编译参数，
# 避免手写 includePath；见 CLAUDE.md 注意事项
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
    src/file.c
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

add_executable(test_commands
    tests/test_commands.c
    src/commands.c
    src/conn.c
    src/file.c
    src/ui.c
    src/protocol.c
    src/utils.c
)
target_include_directories(test_commands PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_test(NAME test_commands COMMAND test_commands)

add_executable(test_file
    tests/test_file.c
    src/file.c
    src/commands.c
    src/conn.c
    src/ui.c
    src/protocol.c
    src/utils.c
)
target_include_directories(test_file PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
add_test(NAME test_file COMMAND test_file)

# 构建后把 compile_commands.json 复制到项目根，
# VS Code 扩展在项目根自动发现，零配置（不用写 .vscode/settings.json）
add_custom_target(export_cc ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    DEPENDS ${CMAKE_BINARY_DIR}/compile_commands.json
)
