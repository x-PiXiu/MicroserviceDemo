# Timer管理模块CMakeLists.txt

set(TIMER_SOURCES
    timer_manager.cpp
)

set(TIMER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/common/timer/timer_manager.h
)

# 创建timer静态库
add_library(timer STATIC ${TIMER_SOURCES})

# 添加依赖关系
add_dependencies(timer network_lib)

# 设置目标属性
set_target_properties(timer PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

# 设置头文件搜索路径
target_include_directories(timer PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party/nlohmann/include
)

# 链接依赖库
target_link_libraries(timer 
    network_lib
    pthread
)

# 安装头文件
install(FILES ${TIMER_HEADERS} 
        DESTINATION include/common/timer)

# 安装库文件
install(TARGETS timer
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)