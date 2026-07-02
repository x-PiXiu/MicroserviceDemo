cmake_minimum_required(VERSION 3.10)
project(common_auth VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖包
find_package(OpenSSL REQUIRED)

# 查找 nlohmann_json
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(JSON_INCLUDE_DIR
        NAMES nlohmann/json.hpp
        PATHS /usr/include /usr/local/include
    )
endif()

# 源文件
set(AUTH_SOURCES
    jwt_validator.cpp
)

# 头文件目录
set(AUTH_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/include/common/auth
    ${CMAKE_SOURCE_DIR}/include/common/utils
    ${CMAKE_SOURCE_DIR}/include/common/config
    ${CMAKE_SOURCE_DIR}/include/common/logger
)

# 创建静态库
add_library(common_auth STATIC ${AUTH_SOURCES})

# 设置包含目录
target_include_directories(common_auth
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${AUTH_INCLUDE_DIRS}
)

# 添加条件包含目录
if(JSON_INCLUDE_DIR)
    target_include_directories(common_auth PRIVATE ${JSON_INCLUDE_DIR})
endif()

# 链接依赖库
target_link_libraries(common_auth
    PUBLIC
        utils_lib           # 使用 Crypto 类进行签名验证
        common_config_lib   # 配置管理
        common_logger_lib   # 日志
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

# 条件链接 nlohmann_json
if(nlohmann_json_FOUND)
    target_link_libraries(common_auth PRIVATE nlohmann_json::nlohmann_json)
endif()

# 编译选项
target_compile_options(common_auth PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

# 安装头文件
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/common/auth
    DESTINATION include/common
    FILES_MATCHING PATTERN "*.h"
)

# 安装库文件
install(TARGETS common_auth
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

message(STATUS "common_auth module configured successfully")
