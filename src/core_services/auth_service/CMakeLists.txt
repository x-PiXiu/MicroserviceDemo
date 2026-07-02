# 认证服务CMake构建文件 - 重构版本
# Game Microservices Auth Service CMakeLists.txt (Refactored)
# 版本: 2.0.0  
# 日期: 2025-09-19
# 说明: 支持用户服务客户端架构的构建配置

cmake_minimum_required(VERSION 3.16)

# 项目信息 (更新版本)
project(auth_service
    VERSION 2.0.0
    DESCRIPTION "Game Microservices Authentication Service (Refactored)"
    LANGUAGES CXX
)

# 版本信息变量
set(AUTH_SERVICE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(AUTH_SERVICE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(AUTH_SERVICE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(AUTH_SERVICE_VERSION ${PROJECT_VERSION})
string(TIMESTAMP AUTH_SERVICE_BUILD_DATE "%Y-%m-%d")
string(TIMESTAMP AUTH_SERVICE_BUILD_TIME "%H:%M:%S")
set(AUTH_SERVICE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})

# Git信息 (如果可用)
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE AUTH_SERVICE_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE AUTH_SERVICE_GIT_BRANCH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    set(AUTH_SERVICE_GIT_COMMIT "unknown")
    set(AUTH_SERVICE_GIT_BRANCH "unknown")
endif()

# C++标准设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra -Wpedantic")
    add_definitions(-DAUTH_SERVICE_DEBUG -DAUTH_SERVICE_REFACTORED)
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -DNDEBUG")
    add_definitions(-DAUTH_SERVICE_REFACTORED)
endif()

# 包含目录
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include    # 认证服务本地头文件
)

# 查找依赖包
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)  # ✅ 新增：HTTP客户端依赖

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

# 查找Kafka客户端库 (可选)
find_library(RDKAFKA_LIB
    NAMES rdkafka
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
)

# 查找bcrypt库 (可选)
find_library(BCRYPT_LIB
    NAMES bcrypt
    PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/local/lib/x86_64-linux-gnu
)

# ✅ 重构版源文件列表
set(AUTH_SERVICE_SOURCES
    # ✅ 新架构：用户服务客户端
    src/user_service_client.cpp

    # ✅ 验证码管理器
    src/verification_code_manager.cpp

    # 数据模型 (保留)
    src/user_models.cpp
    
    # 核心组件 (部分保留)
    src/jwt_manager.cpp
    src/password_manager.cpp
    src/session_manager.cpp              # 会话管理保留
    src/database_initializer.cpp         # 用于认证专用数据库
    ${CMAKE_SOURCE_DIR}/src/common/security/secret_manager.cpp
    
    # ✅ 新增：独立定时器管理器
    src/auth_timer_manager.cpp           # 避免EventLoop timerfd问题
    
    # ❌ 移除：直接数据库访问
    # src/user_repository.cpp            # 不再需要，改用用户服务客户端
    
    # ✅ 重构版主服务类
    src/auth_service.cpp                 # 使用重构版本
    
    # 启动入口 (重构版)
    main.cpp
)

# ✅ 重构版头文件列表
set(AUTH_SERVICE_HEADERS
    # ✅ 新架构：用户服务客户端头文件
    include/user_service_client.h

    # ✅ 验证码管理器头文件
    include/verification_code_manager.h

    # 现有头文件 (保留的)
    include/user_models.h
    include/jwt_manager.h
    include/password_manager.h
    include/session_manager.h
    include/database_initializer.h
    
    # ✅ 新增：独立定时器管理器头文件
    include/auth_timer_manager.h         # 避免EventLoop timerfd问题
    
    # ✅ 重构版主头文件
    include/auth_service.h
    
    # ❌ 移除：直接数据库访问头文件
    # include/user_repository.h
)

# ✅ Common模块库 (更新依赖)
set(COMMON_LIBS
    common_config_lib
    database_lib                      # 仍需要数据库连接池
    http_module                       # ✅ 需要HTTP客户端和服务器
    network_lib
    messaging_lib                     # Kafka (可选)
    common_logger_lib
    task_scheduler_lib
    thread_pool_lib
    common_monitoring
    common_service_registry           # 服务注册客户端
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${AUTH_SERVICE_SOURCES} ${AUTH_SERVICE_HEADERS})

# 设置输出目录到 build/bin
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ✅ 重构版链接库
target_link_libraries(${PROJECT_NAME}
    # Common模块
    ${COMMON_LIBS}
    
    # 系统库
    Threads::Threads
    
    # 加密库
    OpenSSL::SSL
    OpenSSL::Crypto
    
    # ✅ 新增：HTTP客户端库
    ${CURL_LIBRARIES}
    
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
    message(FATAL_ERROR "✗ MySQL Connector/C++ is required for auth service")
endif()

if(REDIS_LIB)
    target_link_libraries(${PROJECT_NAME} ${REDIS_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_REDIS)
    message(STATUS "✓ Redis support enabled")
else()
    message(FATAL_ERROR "✗ Redis client library is required for auth service")
endif()

# Kafka支持 (可选)
if(RDKAFKA_LIB)
    target_link_libraries(${PROJECT_NAME} ${RDKAFKA_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_KAFKA)
    message(STATUS "✓ Kafka support enabled")
else()
    message(STATUS "○ Kafka support disabled (optional)")
endif()

# BCrypt支持 (可选)
if(BCRYPT_LIB)
    target_link_libraries(${PROJECT_NAME} ${BCRYPT_LIB})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_BCRYPT)
    message(STATUS "✓ BCrypt support enabled")
else()
    message(STATUS "○ Using OpenSSL for password hashing")
endif()

# JSON库 (必需)
if(nlohmann_json_FOUND)
    target_link_libraries(${PROJECT_NAME} nlohmann_json::nlohmann_json)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_NLOHMANN_JSON)
    message(STATUS "✓ nlohmann/json support enabled")
else()
    message(FATAL_ERROR "✗ nlohmann/json is required")
endif()

# CURL库 (必需)
if(CURL_FOUND)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CURL_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_CURL)
    message(STATUS "✓ CURL support enabled")
else()
    message(FATAL_ERROR "✗ CURL is required for HTTP client functionality")
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

# ✅ 重构版编译定义
target_compile_definitions(${PROJECT_NAME} PRIVATE
    AUTH_SERVICE_VERSION="${PROJECT_VERSION}"
    AUTH_SERVICE_NAME="${PROJECT_NAME}"
    AUTH_SERVICE_ARCHITECTURE="refactored"
    USER_SERVICE_CLIENT_ENABLED=1
    DIRECT_DATABASE_ACCESS_DISABLED=1
)

# 安装规则
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    COMPONENT auth_service
)

# ✅ 安装重构版配置文件
install(FILES
    ${CMAKE_SOURCE_DIR}/config/auth_service_config.yml
    DESTINATION etc/game-microservices
    COMPONENT auth_service
)

# 安装认证专用数据库脚本
install(FILES
    database/auth_sessions_database_design.sql
    DESTINATION share/game-microservices/sql
    COMPONENT auth_service
)

# ✅ 重构版调试信息
message(STATUS "=== Auth Service Build Configuration (Refactored) ===")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "Architecture: User Service Client Based")
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

if(CURL_FOUND)
    message(STATUS "✓ CURL: ${CURL_LIBRARIES}")
else()
    message(FATAL_ERROR "✗ CURL library (REQUIRED for user service client)")
endif()

if(nlohmann_json_FOUND OR JSON_INCLUDE_DIR)
    message(STATUS "✓ nlohmann/json: Available")
else()
    message(FATAL_ERROR "✗ nlohmann/json (REQUIRED)")
endif()

message(STATUS "")

# 可选依赖检查
message(STATUS "=== 可选依赖检查 ===")

if(RDKAFKA_LIB)
    message(STATUS "✓ librdkafka: ${RDKAFKA_LIB}")
else()
    message(STATUS "○ librdkafka: Not found (optional)")
    message(STATUS "  Install: sudo apt-get install librdkafka-dev")
endif()

if(BCRYPT_LIB)
    message(STATUS "✓ BCrypt: ${BCRYPT_LIB}")
else()
    message(STATUS "○ BCrypt: Not found (using OpenSSL)")
    message(STATUS "  Install: sudo apt-get install libbcrypt-dev")
endif()

message(STATUS "")

# ✅ 新架构特性提示
message(STATUS "=== 新架构特性 ===")
message(STATUS "✓ 用户服务客户端集成")
message(STATUS "✓ 认证专用数据库 (auth_sessions_db)")
message(STATUS "✓ 移除直接用户数据访问")
message(STATUS "✓ 支持服务间通信")
message(STATUS "✓ 增强缓存策略")
message(STATUS "✓ 完整JWT验证")

message(STATUS "")

# 构建建议
message(STATUS "=== 构建建议 ===")
message(STATUS "1. 确保用户服务 (port 8082) 已启动")
message(STATUS "2. 确保认证专用数据库已创建")
message(STATUS "3. 使用配置文件: auth_service_config.yml")
message(STATUS "4. 检查外部服务连接配置")

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
add_custom_target(init_auth_db
    COMMAND mysql -u root -p < ${CMAKE_CURRENT_SOURCE_DIR}/database/auth_sessions_database_design.sql
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "初始化认证服务专用数据库"
)

# 添加自定义目标用于配置验证
add_custom_target(validate_config
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME} --validate-config
    DEPENDS ${PROJECT_NAME}
    COMMENT "验证认证服务配置"
)

# 生成版本信息文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.h
    @ONLY
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
