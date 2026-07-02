cmake_minimum_required(VERSION 3.10)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

# 收集源文件
set(NETWORK_SOURCES
        inet_address.cpp
        epoll.cpp
        socket.cpp
        channel.cpp
        event_loop.cpp
        HierarchicalTimingWheel.cpp
)

# 网络模块库
add_library(network_lib STATIC ${NETWORK_SOURCES})

# 添加依赖关系
add_dependencies(network_lib common_logger_lib)

# 添加头文件搜索路径
target_include_directories(network_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# 链接系统库
target_link_libraries(network_lib PUBLIC
    pthread
    common_logger_lib
)


# 安装规则
install(TARGETS network_lib
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

# 安装头文件
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/common/network/
    DESTINATION include/common/network
    FILES_MATCHING PATTERN "*.h"
)