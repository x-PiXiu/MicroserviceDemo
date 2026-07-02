cmake_minimum_required(VERSION 3.16)

# 设置基类库名称
set(GAME_BASE_LIB game_base)

# 收集基类源文件
file(GLOB_RECURSE BASE_SOURCES
    "src/*.cpp"
    "include/*.h"
)

# 创建静态库
add_library(${GAME_BASE_LIB} STATIC ${BASE_SOURCES})

# 设置包含目录 - 确保正确的include路径顺序
target_include_directories(${GAME_BASE_LIB} PUBLIC
    ${CMAKE_SOURCE_DIR}/include
        include
)

# 添加依赖关系确保构建顺序
add_dependencies(${GAME_BASE_LIB}
    http_module
    network_lib
    common_logger_lib
    thread_pool_lib
    common_config_lib
    database_lib
    common_monitoring
)

# 链接依赖库
target_link_libraries(${GAME_BASE_LIB} PUBLIC
    http_module
    network_lib
    common_logger_lib
    thread_pool_lib
    common_config_lib
    database_lib
    common_monitoring                # 🔧 性能监控库（用于AlertManager）
    nlohmann_json::nlohmann_json
    OpenSSL::SSL                     # 🔐 OpenSSL SSL库（WebSocket SSL支持）
    OpenSSL::Crypto                  # 🔐 OpenSSL Crypto库（加密功能）
)

# 设置编译选项
target_compile_features(${GAME_BASE_LIB} PUBLIC cxx_std_17)
target_compile_options(${GAME_BASE_LIB} PRIVATE 
    -Wall 
    -Wextra 
    -Wpedantic
    -Wno-unused-parameter
)

# 设置库属性
set_target_properties(${GAME_BASE_LIB} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    POSITION_INDEPENDENT_CODE ON
)

# 添加预编译头文件支持（可选）
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.16")
    target_precompile_headers(${GAME_BASE_LIB} PRIVATE
        <memory>
        <string>
        <vector>
        <unordered_map>
        <unordered_set>
        <functional>
        <chrono>
        <atomic>
        <shared_mutex>
        <thread>
        <filesystem>
        <regex>
        <nlohmann/json.hpp>
    )
endif()

# 安装规则
install(TARGETS ${GAME_BASE_LIB}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include
    DESTINATION include/game_base
    FILES_MATCHING PATTERN "*.h"
)

# 添加测试（如果启用测试）
if(BUILD_TESTING)
    # 检查tests目录是否存在
    if(EXISTS tests)
        add_subdirectory(tests)
        message(STATUS "Game Base tests will be built")
    else()
        message(STATUS "Game Base tests directory not found, skipping tests")
    endif()
endif()

# 打印构建信息
message(STATUS "Game Base Library configured:")
message(STATUS "  - Library name: ${GAME_BASE_LIB}")
message(STATUS "  - Source files: ${BASE_SOURCES}")
message(STATUS "  - C++ standard: C++17")
message(STATUS "  - Position independent code: ON")
