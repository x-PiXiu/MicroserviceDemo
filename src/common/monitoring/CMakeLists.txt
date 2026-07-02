cmake_minimum_required(VERSION 3.10)

# 设置项目名称
set(TARGET_NAME common_monitoring)

# 明确指定源文件（避免GLOB缓存问题）
set(SOURCES
    performance_monitor.cpp
    ${CMAKE_SOURCE_DIR}/include/common/monitoring/performance_monitor.h
)

# 创建静态库
add_library(${TARGET_NAME} STATIC ${SOURCES})

# 设置包含目录
target_include_directories(${TARGET_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann/include
)

# 链接依赖库（仅保留实际需要的依赖）
target_link_libraries(${TARGET_NAME}
    common_config_lib
    common_logger_lib
    thread_pool_lib
)

# 设置编译选项
target_compile_features(${TARGET_NAME} PRIVATE cxx_std_17)
target_compile_options(${TARGET_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# 设置目标属性
set_target_properties(${TARGET_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# 安装规则
install(TARGETS ${TARGET_NAME}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

# 安装头文件
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/common/monitoring/
    DESTINATION include/common/monitoring
    FILES_MATCHING PATTERN "*.h"
)
