cmake_minimum_required(VERSION 3.16)

# 设置五子棋游戏库和可执行文件名称
set(GOMOKU_LIB gomoku_game_lib)
set(GOMOKU_SERVER gomoku_server)

# 明确指定源文件列表（避免包含备份文件）
set(GOMOKU_SOURCES
    # 头文件
        include/gomoku_database.h
        include/gomoku_logic.h
        include/gomoku_player_session.h
        include/gomoku_room.h
        include/gomoku_server.h
        include/gomoku_types.h
        include/gomoku_websocket_handler.h
        include/service_client.h
        include/circuit_breaker.h
        include/event_system.h
        include/game_mode_strategy.h
        include/match_pool.h
        include/match_strategy.h
        include/match_making_manager.h
        include/friend_room_manager.h

    # 源文件
        src/gomoku_database.cpp
        src/gomoku_logic.cpp
        src/gomoku_player_session.cpp
        src/gomoku_room.cpp
        src/gomoku_server.cpp
        src/gomoku_websocket_handler.cpp
        src/service_client.cpp
        src/event_system.cpp
        src/game_mode_strategy.cpp
        src/match_making_manager.cpp
        src/friend_room_manager.cpp

    # [FIX] 已移除冗余文件：
    # - include/auth_service_client.h (认证由API网关统一处理)
    # - include/jwt_validator.h (JWT验证由API网关统一处理)
    # - src/auth_service_client.cpp
    # - src/jwt_validator.cpp
)

# 创建五子棋游戏静态库
add_library(${GOMOKU_LIB} STATIC ${GOMOKU_SOURCES})

# 设置包含目录
target_include_directories(${GOMOKU_LIB} PUBLIC
    ${CMAKE_SOURCE_DIR}/include
        include
    ${CMAKE_SOURCE_DIR}/src/game_services/game_base/include
)

# 添加依赖关系
add_dependencies(${GOMOKU_LIB}
    game_base
    http_module
    network_lib
    common_logger_lib
    thread_pool_lib
    common_config_lib
    database_lib
    common_monitoring
)

# 链接依赖库
target_link_libraries(${GOMOKU_LIB} PUBLIC
    game_base                        # 继承游戏基类
    http_module
    network_lib
    common_logger_lib
    thread_pool_lib
    common_config_lib
    database_lib
    common_monitoring
    common_service_registry          # 服务注册客户端
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    ${CMAKE_THREAD_LIBS_INIT}
    # 🎯 简化架构：移除Kafka依赖，使用简化的跨服务事件发布
)

# 创建五子棋服务器可执行文件
add_executable(${GOMOKU_SERVER} main.cpp)

# 设置可执行文件的包含目录
target_include_directories(${GOMOKU_SERVER} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
        include
    ${CMAKE_SOURCE_DIR}/src/game_services/game_base/include
)

# 链接五子棋游戏库
target_link_libraries(${GOMOKU_SERVER} PRIVATE
    ${GOMOKU_LIB}
)

# 设置编译选项
target_compile_features(${GOMOKU_LIB} PUBLIC cxx_std_17)
target_compile_features(${GOMOKU_SERVER} PRIVATE cxx_std_17)

target_compile_options(${GOMOKU_LIB} PRIVATE 
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)
target_compile_options(${GOMOKU_SERVER} PRIVATE 
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

# 设置输出目录
set_target_properties(${GOMOKU_LIB} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE ON
)

set_target_properties(${GOMOKU_SERVER} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 安装规则
install(TARGETS ${GOMOKU_LIB} ${GOMOKU_SERVER}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include
    DESTINATION include/gomoku
    FILES_MATCHING PATTERN "*.h"
)

# 打印构建信息
message(STATUS "Gomoku Game configured:")
message(STATUS "  - Library name: ${GOMOKU_LIB}")
message(STATUS "  - Server name: ${GOMOKU_SERVER}")
message(STATUS "  - Source files: ${GOMOKU_SOURCES}")
message(STATUS "  - C++ standard: C++17")




