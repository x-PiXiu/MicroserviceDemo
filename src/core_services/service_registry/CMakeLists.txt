# Service Registry CMake构建文件
# 独立的服务注册与发现中心
# 版本: 1.0.0
# 日期: 2025-02-19
# 说明: 基于Redis的轻量级服务注册与发现服务

cmake_minimum_required(VERSION 3.16)

# 项目信息
project(service_registry
    VERSION 1.0.0
    DESCRIPTION "Game Microservices Service Registry"
    LANGUAGES CXX
)

# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra -Wpedantic")
    add_definitions(-DSERVICE_REGISTRY_DEBUG)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
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

# 查找Redis客户端库 (必需)
find_path(REDIS_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    PATHS /usr/include /usr/local/include
)

find_library(REDIS_LIB
    NAMES hiredis
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# 查找yaml-cpp (可选)
find_library(YAML_CPP_LIB
    NAMES yaml-cpp
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# 服务源文件列表
set(SERVICE_REGISTRY_SOURCES
    # 数据模型
    src/service_info.cpp

    # 存储层
    src/redis_storage.cpp
    src/index_manager.cpp

    # 核心业务
    src/registry_manager.cpp
    src/health_monitor.cpp
    src/query_engine.cpp
    src/event_publisher.cpp

    # HTTP层
    src/registry_handler.cpp

    # 主服务
    src/service_registry.cpp

    # 启动入口
        main.cpp
)

# 服务头文件列表
set(SERVICE_REGISTRY_HEADERS
    include/service_info.h
    include/registry_config.h
    include/redis_storage.h
    include/index_manager.h
    include/registry_manager.h
    include/health_monitor.h
    include/query_engine.h
    include/event_publisher.h
    include/registry_handler.h
    include/service_registry.h
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
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SERVICE_REGISTRY_SOURCES} ${SERVICE_REGISTRY_HEADERS})

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
if(REDIS_LIB)
    target_link_libraries(${PROJECT_NAME} ${REDIS_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_REDIS)
    message(STATUS "✓ Redis support enabled")
else()
    message(FATAL_ERROR "✗ Redis client library is required for service_registry")
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
    SERVICE_REGISTRY_VERSION="${PROJECT_VERSION}"
    SERVICE_REGISTRY_NAME="${PROJECT_NAME}"
    SERVICE_REGISTRY_PORT=8081
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT service_registry
)

# 安装配置文件
install(FILES
    config/service_registry.yml
    DESTINATION etc/game-microservices
    COMPONENT service_registry
)

# 调试信息
message(STATUS "=== Service Registry Build Configuration ===")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Port: 8081")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "")

# 核心依赖检查
message(STATUS "=== 核心依赖检查 ===")

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

# 服务特性
message(STATUS "=== Service Registry 特性 ===")
message(STATUS "✓ 服务注册/注销")
message(STATUS "✓ 心跳机制 (Redis TTL)")
message(STATUS "✓ 健康监控")
message(STATUS "✓ 多维度查询")
message(STATUS "✓ 服务推荐")
message(STATUS "✓ 事件发布 (Redis Pub/Sub)")
message(STATUS "✓ HTTP API")

message(STATUS "")

message(STATUS "================================================")

# 平台特定设置
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} pthread)
elseif(APPLE)
    find_library(CORE_FOUNDATION CoreFoundation)
    if(CORE_FOUNDATION)
        target_link_libraries(${PROJECT_NAME} ${CORE_FOUNDATION})
    endif()
endif()

# 性能优化设置
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

    target_compile_options(${PROJECT_NAME} PRIVATE
        -march=native
        -mtune=native
        -flto
    )

    message(STATUS "✓ 性能优化已启用 (Release build)")
endif()
