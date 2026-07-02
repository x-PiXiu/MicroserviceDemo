cmake_minimum_required(VERSION 3.10)

# 创建utils静态库
add_library(utils_lib
    md5.cpp
    crypto.cpp
    ip_utils.cpp  # ⭐ IP工具类
    ip_blacklist_manager.cpp  # ⭐⭐⭐ 新增：黑名单管理器
    sms_service.cpp
    email_service.cpp  # ⭐⭐⭐ 新增：邮件发送服务
    product_description_helper.cpp  # ⭐⭐⭐ 新增：商品描述助手
)

# 设置包含目录
target_include_directories(utils_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# 链接OpenSSL库
find_package(OpenSSL REQUIRED)
target_link_libraries(utils_lib
    PUBLIC OpenSSL::SSL
    PUBLIC OpenSSL::Crypto
)

# 设置编译标准
target_compile_features(utils_lib PUBLIC cxx_std_17)
