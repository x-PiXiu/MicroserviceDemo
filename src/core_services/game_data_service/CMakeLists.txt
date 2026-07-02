# 游戏数据服务CMake构建文件 - 符合推荐分层架构
# Game Data Service CMakeLists.txt
# 版本: 1.0.0
# 日期: 2025-09-19
# 说明: 管理game_service_db的跨游戏数据服务构建配置

cmake_minimum_required(VERSION 3.16)

# 项目信息
project(game_data_service
    VERSION 1.0.0
    DESCRIPTION "Game Microservices Game Data Service"
    LANGUAGES CXX
)

# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra -Wpedantic")
    add_definitions(-DGAME_DATA_SERVICE_DEBUG)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include    # 游戏数据服务本地头文件
    ${CMAKE_SOURCE_DIR}/src/common/game_plugin/include  # 游戏插件接口
)

# 查找依赖包
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# 查找nlohmann/json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(JSON_INCLUDE_DIR
        NAMES nlohmann/json.hpp
        PATHS /usr/include /usr/local/include
    )
    if(NOT JSON_INCLUDE_DIR)
        message(FATAL_ERROR "nlohmann/json not found. Please install nlohmann-json3-dev")
    endif()
endif()

# 查找MySQL Connector/C++
find_path(MYSQL_CONNECTOR_INCLUDE_DIR
    NAMES mysql_connection.h mysql/mysql.h
    PATHS
        /usr/include/mysql-cppconn-8
        /usr/local/include/mysql-cppconn-8
        /usr/include/mysql
        /usr/include/mysql++
        /usr/local/include/mysql
)

find_library(MYSQL_CONNECTOR_LIB
    NAMES
        mysqlcppconn8
        mysqlcppconn
        mysqlclient
        mysql
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib64
)

# 查找Redis客户端库
find_path(REDIS_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    PATHS /usr/include /usr/local/include
)

find_library(REDIS_LIB
    NAMES hiredis
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# 查找yaml-cpp
find_library(YAML_CPP_LIB
    NAMES yaml-cpp
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# 游戏数据服务源文件列表
set(GAME_DATA_SERVICE_SOURCES
    # 数据模型
    src/game_models.cpp

    # 数据访问层
    src/game_repository.cpp

    # 服务层
    src/game_service.cpp

    # 独立定时器管理器（避免EventLoop timerfd问题）
    src/game_timer_manager.cpp

    # ELO评分系统
    src/elo_rating_calculator.cpp

    # 奖励配置系统
    src/reward_config.cpp

    # 货币管理系统
    src/currency_manager.cpp

    # 游戏结束处理器
    src/game_end_processor.cpp

    # 成就管理器
    src/achievement_manager.cpp

    # 排行榜管理器
    src/leaderboard_manager.cpp

    # 排行榜定时任务调度器
    src/leaderboard_scheduler.cpp

    # 游戏回放管理器
    src/replay_manager.cpp

    # 排行榜实时推送服务 (P1)
    src/leaderboard_push_service.cpp

    # 成就条件检查引擎 (P1)
    src/achievement_condition_engine.cpp

    # 分布式排行榜服务 (P2)
    src/distributed_leaderboard.cpp

    # 启动入口
    main.cpp
)

# 游戏数据服务头文件列表
set(GAME_DATA_SERVICE_HEADERS
    include/game_models.h
    include/game_repository.h
    include/game_service.h
    include/game_timer_manager.h
    include/elo_rating_calculator.h
    include/reward_config.h
    include/currency_manager.h
    include/game_end_processor.h
    include/achievement_manager.h
    include/leaderboard_manager.h
    include/leaderboard_scheduler.h
    include/replay_manager.h
    include/leaderboard_push_service.h
    include/achievement_condition_engine.h
    include/distributed_leaderboard.h
)

# Common模块库
set(COMMON_LIBS
    common_config_lib
    database_lib
    http_module
    network_lib
    common_logger_lib
    task_scheduler_lib
    thread_pool_lib
    common_monitoring
    common_service_registry
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${GAME_DATA_SERVICE_SOURCES} ${GAME_DATA_SERVICE_HEADERS})

# 设置输出目录到 build/bin
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# 链接库
target_link_libraries(${PROJECT_NAME}
    # Common模块
    ${COMMON_LIBS}
    
    # 系统库
    Threads::Threads
    
    # 加密库
    OpenSSL::SSL
    OpenSSL::Crypto
    
    # 其他库
    dl
    rt
)

# 条件链接可选依赖
if(MYSQL_CONNECTOR_LIB)
    target_link_libraries(${PROJECT_NAME} ${MYSQL_CONNECTOR_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_MYSQL)
    message(STATUS "✓ MySQL support enabled")
else()
    message(FATAL_ERROR "✗ MySQL Connector/C++ is required for game data service")
endif()

if(REDIS_LIB)
    target_link_libraries(${PROJECT_NAME} ${REDIS_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_REDIS)
    message(STATUS "✓ Redis support enabled")
else()
    message(FATAL_ERROR "✗ Redis client library is required for game data service")
endif()

if(YAML_CPP_LIB)
    target_link_libraries(${PROJECT_NAME} ${YAML_CPP_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_YAML_CPP)
    message(STATUS "✓ yaml-cpp support enabled")
else()
    message(STATUS "○ yaml-cpp not found, using JSON config only")
endif()

# JSON库 (必需)
if(nlohmann_json_FOUND)
    target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_NLOHMANN_JSON)
    message(STATUS "✓ nlohmann/json support enabled")
else()
    message(FATAL_ERROR "✗ nlohmann/json is required")
endif()

# 包含目录
if(MYSQL_CONNECTOR_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${MYSQL_CONNECTOR_INCLUDE_DIR})
endif()

if(REDIS_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${REDIS_INCLUDE_DIR})
endif()

if(JSON_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${JSON_INCLUDE_DIR})
endif()

if(OPENSSL_INCLUDE_DIR)
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

# 编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    GAME_DATA_SERVICE_VERSION="${PROJECT_VERSION}"
    GAME_DATA_SERVICE_NAME="${PROJECT_NAME}"
    GAME_DATA_SERVICE_DATABASE="game_service_db"
    GAME_DATA_SERVICE_PORT=8083
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT game_data_service
)

# 安装配置文件
install(FILES
    config/game_data_service.yml
    DESTINATION etc/game-microservices
    COMPONENT game_data_service
)

# 安装数据库脚本
install(FILES
    database/game_data_service_database_design.sql
    DESTINATION share/game-microservices/sql
    COMPONENT game_data_service
)

# 调试信息
message(STATUS "=== Game Data Service Build Configuration ===")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Database: game_service_db")
message(STATUS "Port: 8083")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")

# 核心依赖检查
message(STATUS "=== 核心依赖检查 ===")

if(MYSQL_CONNECTOR_LIB)
    message(STATUS "✓ MySQL Connector/C++: ${MYSQL_CONNECTOR_LIB}")
else()
    message(FATAL_ERROR "✗ MySQL Connector/C++ (REQUIRED)")
endif()

if(REDIS_LIB)
    message(STATUS "✓ Redis (hiredis): ${REDIS_LIB}")
else()
    message(FATAL_ERROR "✗ Redis client library (REQUIRED)")
endif()

if(nlohmann_json_FOUND OR JSON_INCLUDE_DIR)
    message(STATUS "✓ nlohmann/json: Available")
else()
    message(FATAL_ERROR "✗ nlohmann/json (REQUIRED)")
endif()

message(STATUS "")

# 可选依赖检查
message(STATUS "=== 可选依赖检查 ===")

if(YAML_CPP_LIB)
    message(STATUS "✓ yaml-cpp: ${YAML_CPP_LIB}")
else()
    message(STATUS "○ yaml-cpp: Not found (optional)")
    message(STATUS "  Install: sudo apt-get install libyaml-cpp-dev")
endif()

message(STATUS "")

# 架构特性提示
message(STATUS "=== 游戏数据服务特性 ===")
message(STATUS "✓ 跨游戏数据管理 (game_service_db)")
message(STATUS "✓ 用户游戏档案 (user_game_profiles表)")
message(STATUS "✓ 成就系统 (achievement_definitions, user_achievements表)")
message(STATUS "✓ 库存系统 (item_definitions, user_inventory表)")
message(STATUS "✓ 货币系统 (currency_definitions, user_currency表)")
message(STATUS "✓ 游戏结果记录 (game_results表)")
message(STATUS "✓ 排行榜系统 (leaderboards表)")
message(STATUS "✓ Redis缓存支持")
message(STATUS "✓ API网关注册")
message(STATUS "✓ ELO评分计算系统 (elo_rating_calculator)")
message(STATUS "✓ 奖励配置系统 (reward_config)")
message(STATUS "✓ 货币管理器 (currency_manager)")
message(STATUS "✓ 游戏结束处理器 (game_end_processor)")
message(STATUS "✓ 成就管理器 (achievement_manager)")
message(STATUS "✓ 排行榜管理器 (leaderboard_manager)")

message(STATUS "")

# 构建建议
message(STATUS "=== 构建建议 ===")
message(STATUS "1. 确保MySQL数据库已启动")
message(STATUS "2. 创建game_service_db数据库")
message(STATUS "3. 确保Redis服务已启动")
message(STATUS "4. 检查config/game_data_service.yml配置")

message(STATUS "================================================")

# 平台特定设置
if(UNIX AND NOT APPLE)
    # Linux特定设置
    target_link_libraries(${PROJECT_NAME} pthread)
elseif(APPLE)
    # macOS特定设置
    find_library(CORE_FOUNDATION CoreFoundation)
    if(CORE_FOUNDATION)
        target_link_libraries(${PROJECT_NAME} ${CORE_FOUNDATION})
    endif()
endif()

# 性能优化设置
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # 启用链接时优化
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    
    # 添加性能优化标志
    target_compile_options(${PROJECT_NAME} PRIVATE
        -march=native
        -mtune=native
        -flto
    )
    
    message(STATUS "✓ 性能优化已启用 (Release build)")
endif()

# 添加自定义目标用于数据库初始化
add_custom_target(init_game_data_db
    COMMAND mysql -u root -p < ${CMAKE_CURRENT_SOURCE_DIR}/database/game_data_service_database_design.sql
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "初始化游戏数据服务数据库"
)

# 添加自定义目标用于配置验证
add_custom_target(validate_game_data_config
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME} --validate-config
    DEPENDS ${PROJECT_NAME}
    COMMENT "验证游戏数据服务配置"
)




