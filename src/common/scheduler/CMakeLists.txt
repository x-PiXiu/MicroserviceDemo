cmake_minimum_required(VERSION 3.10)

# 任务调度器库
add_library(task_scheduler_lib
        task_scheduler.cpp
)

# 设置C++标准
target_compile_features(task_scheduler_lib PUBLIC cxx_std_17)

# 添加头文件搜索路径
target_include_directories(task_scheduler_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 查找并链接线程库
find_package(Threads REQUIRED)
target_link_libraries(task_scheduler_lib
    PUBLIC
        Threads::Threads
)

# 设置编译选项
target_compile_options(task_scheduler_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)