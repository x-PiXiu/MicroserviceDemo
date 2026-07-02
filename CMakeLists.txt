cmake_minimum_required(VERSION 3.10)
project(MicroserviceBase)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

# ==================== 平台检测 ====================
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    message(STATUS "Target platform: Windows")
    set(PLATFORM_WINDOWS TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Target platform: Linux")
    set(PLATFORM_LINUX TRUE)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(STATUS "Target platform: macOS")
    set(PLATFORM_MACOS TRUE)
else()
    message(STATUS "Target platform: ${CMAKE_SYSTEM_NAME}")
    set(PLATFORM_LINUX TRUE)  # 默认假设是类Unix系统
endif()

# ==================== 编译选项配置 ====================
if(PLATFORM_WINDOWS)
    # Windows特定设置
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -DPLATFORM_WINDOWS)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
else()
    # Linux/Unix特定设置
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")
    add_definitions(-DPLATFORM_LINUX)
endif()

# 第三方库
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)
find_package(Protobuf REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(nlohmann_json REQUIRED)

# 新架构所需的额外依赖
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# MySQL Connector/C++ (for user_service and game_data_service)
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

# Redis客户端库 (for caching in user_service and game_data_service)
find_path(REDIS_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    PATHS /usr/include /usr/local/include
)

find_library(REDIS_LIB
    NAMES hiredis
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# 添加子目录
add_subdirectory(src/common)
add_subdirectory(src/core_services)
add_subdirectory(src/game_services)



# ==================== 新架构依赖检查 ====================
message(STATUS "=== 新分层架构依赖检查 ===")

if(MYSQL_CONNECTOR_LIB)
    message(STATUS "✓ MySQL Connector/C++ found: ${MYSQL_CONNECTOR_LIB}")
else()
    message(WARNING "○ MySQL Connector/C++ not found (required for user_service & game_data_service)")
    message(STATUS "  Install: sudo apt-get install libmysqlcppconn-dev")
endif()

if(REDIS_LIB)
    message(STATUS "✓ Redis (hiredis) found: ${REDIS_LIB}")
else()
    message(WARNING "○ Redis client library not found (required for caching)")
    message(STATUS "  Install: sudo apt-get install libhiredis-dev")
endif()

message(STATUS "✓ JSON library: nlohmann/json")
message(STATUS "✓ Configuration: yaml-cpp")
message(STATUS "✓ Encryption: OpenSSL")
message(STATUS "✓ HTTP client: CURL")
message(STATUS "===================================")

# ==================== 库变量定义 ====================
# 为测试提供库变量
set(CONFIG_LIB temp_config_lib)
set(LOGGER_LIB temp_logger_lib)
set(THREAD_POOL_LIB temp_thread_pool_lib)
set(TASK_SCHEDULER_LIB temp_task_scheduler_lib)
set(DATABASE_LIB database_lib)  # 数据库库
set(NETWORK_LIB temp_network_lib)

# 安装目标
install(DIRECTORY ${CMAKE_BINARY_DIR}/bin/
        DESTINATION bin
        USE_SOURCE_PERMISSIONS)