# WebSocket Native Implementation CMakeLists.txt

# 设置源文件
set(WEBSOCKET_NATIVE_SOURCES
    websocket_frame.cpp
    websocket_handshake.cpp
    websocket_native_connection.cpp
    websocket_native_server.cpp
)

# 创建静态库
add_library(websocket_native STATIC ${WEBSOCKET_NATIVE_SOURCES})

# 设置包含目录
target_include_directories(websocket_native
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 链接依赖库
target_link_libraries(websocket_native
    PUBLIC
        common_logger_lib
        network_lib
        http_module
        OpenSSL::SSL
        OpenSSL::Crypto
)

# 编译选项
target_compile_options(websocket_native
    PRIVATE
        -Wall
        -Wextra
        -Werror
)

message(STATUS "✅ WebSocket Native库已配置")
