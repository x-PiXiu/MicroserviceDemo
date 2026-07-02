cmake_minimum_required(VERSION 3.10)

# 查找librdkafka库（如果系统中已安装）
find_library(RDKAFKA_LIBRARY NAMES rdkafka)
find_library(RDKAFKACPP_LIBRARY NAMES rdkafka++)

# 设置默认库变量
set(KAFKA_LIBRARIES "")

if(RDKAFKA_LIBRARY AND RDKAFKACPP_LIBRARY)
    set(KAFKA_LIBRARIES ${RDKAFKA_LIBRARY} ${RDKAFKACPP_LIBRARY})
    message(STATUS "Found librdkafka libraries: ${KAFKA_LIBRARIES}")
else()
    message(WARNING "librdkafka libraries not found, using mock implementation")
    # 可以添加mock实现或者使用其他消息队列库
endif()

# Kafka消息队列库
add_library(messaging_lib
        kafka_producer.cpp
        kafka_consumer.cpp
)

# 设置C++标准
target_compile_features(messaging_lib PUBLIC cxx_std_17)

# 添加头文件搜索路径
target_include_directories(messaging_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 链接依赖库
target_link_libraries(messaging_lib PUBLIC
        common_logger_lib
        thread_pool_lib
        ${KAFKA_LIBRARIES}
)

# 如果找到了librdkafka，添加编译定义
if(RDKAFKA_LIBRARY AND RDKAFKACPP_LIBRARY)
    target_compile_definitions(messaging_lib PUBLIC KAFKA_ENABLED)
else()
    target_compile_definitions(messaging_lib PUBLIC KAFKA_MOCK)
endif()