cmake_minimum_required(VERSION 3.10)

# 日志库
add_library(common_logger_lib
        logger.cpp
)

# 添加头文件搜索路径
target_include_directories(common_logger_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 链接线程库
target_link_libraries(common_logger_lib PRIVATE
        pthread
)