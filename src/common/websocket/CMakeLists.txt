# src/common/websocket/CMakeLists.txt

# ⭐⭐⭐ 2025-12-11: 移除废弃的websocketpp实现，仅保留native实现
# WebSocket模块源文件（仅公共组件，不包含websocketpp依赖）
set(WEBSOCKET_SOURCES
    websocket_message.cpp
    # ⭐ 已废弃: delivery_task_router.cpp (依赖MerchantConnectionManager)
    # ⭐ 已废弃: merchant_connection_manager.cpp (依赖旧版WebSocketConnection)
    # ⭐ 已废弃: websocket_handler.cpp 和 websocket_connection.cpp (基于websocketpp)
)

# 创建WebSocket库
add_library(common_websocket STATIC ${WEBSOCKET_SOURCES})

# 包含目录
target_include_directories(common_websocket
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 链接依赖
target_link_libraries(common_websocket
    PUBLIC
        common_logger_lib
        database_lib
        network_lib
        http_module
)

# 查找OpenSSL (jwt-cpp依赖)
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")
    target_link_libraries(common_websocket PUBLIC OpenSSL::SSL OpenSSL::Crypto)
else()
    message(WARNING "OpenSSL not found! Please install: sudo apt-get install libssl-dev")
endif()

# 查找nlohmann/json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
        PATHS /usr/include /usr/local/include
    )
    if(NLOHMANN_JSON_INCLUDE_DIR)
        message(STATUS "Found nlohmann/json: ${NLOHMANN_JSON_INCLUDE_DIR}")
        target_include_directories(common_websocket PUBLIC ${NLOHMANN_JSON_INCLUDE_DIR})
    else()
        message(WARNING "nlohmann/json not found! Install: sudo apt-get install nlohmann-json3-dev")
    endif()
endif()

# ⭐⭐⭐ 2025-12-11: 移除websocketpp依赖（已废弃）
# find_path(WEBSOCKETPP_INCLUDE_DIR websocketpp/config/asio.hpp
#     PATHS /usr/include /usr/local/include
# )
# if(WEBSOCKETPP_INCLUDE_DIR)
#     message(STATUS "Found websocketpp: ${WEBSOCKETPP_INCLUDE_DIR}")
#     target_include_directories(common_websocket PUBLIC ${WEBSOCKETPP_INCLUDE_DIR})
# else()
#     message(WARNING "websocketpp not found! Install: sudo apt-get install libwebsocketpp-dev")
# endif()

# 查找jwt-cpp (header-only) ⭐ 修复：添加target_include_directories
find_path(JWT_CPP_INCLUDE_DIR jwt-cpp/jwt.h
    PATHS /usr/local/include /usr/include
)
if(JWT_CPP_INCLUDE_DIR)
    message(STATUS "Found jwt-cpp: ${JWT_CPP_INCLUDE_DIR}")
    target_include_directories(common_websocket PUBLIC ${JWT_CPP_INCLUDE_DIR})
else()
    message(WARNING "jwt-cpp not found! Install from source: https://github.com/Thalhammer/jwt-cpp")
endif()

# ==================== WebSocket Native实现 ⭐⭐⭐ 2025-12-11 ====================
add_subdirectory(native)
