cmake_minimum_required(VERSION 3.10)

# ==================== Config模块库 ====================

# 明确指定源文件（避免GLOB缓存问题）
set(CONFIG_SOURCES
    config_manager.cpp
)

# 创建Config库
add_library(common_config_lib ${CONFIG_SOURCES})

# 设置C++标准
target_compile_features(common_config_lib PUBLIC cxx_std_17)

# 添加头文件搜索路径
target_include_directories(common_config_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/common
)

# ==================== 依赖库配置 ====================

# 基础系统库
target_link_libraries(common_config_lib PUBLIC
        pthread
)

# 文件系统库支持（C++17 filesystem）
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(common_config_lib PUBLIC stdc++fs)
endif()

# Logger库依赖
if(TARGET common_logger_lib)
    target_link_libraries(common_config_lib PUBLIC common_logger_lib)
    message(STATUS "Config module linked with logger library")
else()
    message(WARNING "Logger library not found for Config module")
endif()

# 注意：Config模块只定义ThreadPoolConfig结构体，不直接依赖线程池实现
# 因此不需要链接thread_pool_lib

# ==================== 第三方库配置 ====================

# 注意：Config模块只定义SSL相关的配置项（字符串），不直接使用OpenSSL
# 实际的SSL功能由使用这些配置的模块（如MySQL、Redis客户端）提供

# yaml-cpp库
find_package(yaml-cpp QUIET)
if(yaml-cpp_FOUND)
    target_link_libraries(common_config_lib PUBLIC yaml-cpp)
    target_compile_definitions(common_config_lib PUBLIC YAML_CPP_FOUND)
    message(STATUS "Config module: Found yaml-cpp")
else()
    # 尝试手动查找yaml-cpp
    find_path(YAML_CPP_INCLUDE_DIR
        NAMES yaml-cpp/yaml.h
        PATHS
            /usr/include
            /usr/local/include
            /opt/yaml-cpp/include
    )

    find_library(YAML_CPP_LIBRARY
        NAMES yaml-cpp libyaml-cpp
        PATHS
            /usr/lib
            /usr/local/lib
            /opt/yaml-cpp/lib
            /usr/lib/x86_64-linux-gnu
    )

    if(YAML_CPP_INCLUDE_DIR AND YAML_CPP_LIBRARY)
        target_include_directories(common_config_lib PUBLIC ${YAML_CPP_INCLUDE_DIR})
        target_link_libraries(common_config_lib PUBLIC ${YAML_CPP_LIBRARY})
        target_compile_definitions(common_config_lib PUBLIC YAML_CPP_FOUND)
        message(STATUS "Config module: Found yaml-cpp manually at ${YAML_CPP_LIBRARY}")
    else()
        message(WARNING "Config module: yaml-cpp not found. YAML support will be limited.")
        message(STATUS "To install yaml-cpp:")
        message(STATUS "  Ubuntu/Debian: sudo apt-get install libyaml-cpp-dev")
        message(STATUS "  CentOS/RHEL: sudo yum install yaml-cpp-devel")
    endif()
endif()

# nlohmann/json库
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(common_config_lib PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(common_config_lib PUBLIC NLOHMANN_JSON_FOUND)
    message(STATUS "Config module: Found nlohmann/json")
else()
    # 尝试手动查找nlohmann/json
    find_path(NLOHMANN_JSON_INCLUDE_DIR
        NAMES nlohmann/json.hpp
        PATHS
            /usr/include
            /usr/local/include
            /opt/nlohmann/include
    )

    if(NLOHMANN_JSON_INCLUDE_DIR)
        target_include_directories(common_config_lib PUBLIC ${NLOHMANN_JSON_INCLUDE_DIR})
        target_compile_definitions(common_config_lib PUBLIC NLOHMANN_JSON_FOUND)
        message(STATUS "Config module: Found nlohmann/json manually at ${NLOHMANN_JSON_INCLUDE_DIR}")
    else()
        message(WARNING "Config module: nlohmann/json not found. JSON support will be limited.")
        message(STATUS "To install nlohmann/json:")
        message(STATUS "  Ubuntu/Debian: sudo apt-get install nlohmann-json3-dev")
        message(STATUS "  CentOS/RHEL: sudo yum install json-devel")
    endif()
endif()

# ==================== 编译选项 ====================

# 添加编译定义
target_compile_definitions(common_config_lib PRIVATE
    CONFIG_MODULE_VERSION="1.0.0"
)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(common_config_lib PRIVATE CONFIG_DEBUG_MODE)
endif()

# 导出库目标
set_target_properties(common_config_lib PROPERTIES
    OUTPUT_NAME "common_config"
    VERSION 1.0.0
    SOVERSION 1
)

# 安装配置（可选）
if(CMAKE_INSTALL_PREFIX)
    install(TARGETS common_config_lib
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )

    install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/common/config
        DESTINATION include/common
        FILES_MATCHING PATTERN "*.h"
    )
endif()

message(STATUS "Config module library configured successfully")