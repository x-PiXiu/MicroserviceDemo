# HTTP模块CMakeLists.txt
# 简化的HTTP模块构建配置

cmake_minimum_required(VERSION 3.10)

# 使用GLOB收集源文件，避免路径问题
file(GLOB HTTP_SOURCES "*.cpp")

# 创建库
add_library(http_module STATIC ${HTTP_SOURCES})

# 设置C++标准
set_target_properties(http_module PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
)

# 包含目录
target_include_directories(http_module PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 查找 CURL 库
find_package(CURL QUIET)
if(CURL_FOUND)
    message(STATUS "CURL found: ${CURL_LIBRARIES}")
    target_link_libraries(http_module PUBLIC ${CURL_LIBRARIES})
    target_include_directories(http_module PUBLIC ${CURL_INCLUDE_DIRS})
else()
    # 尝试使用 pkg-config 查找 libcurl
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBCURL libcurl)
        if(LIBCURL_FOUND)
            message(STATUS "libcurl found via pkg-config: ${LIBCURL_LIBRARIES}")
            target_link_libraries(http_module PUBLIC ${LIBCURL_LIBRARIES})
            target_include_directories(http_module PUBLIC ${LIBCURL_INCLUDE_DIRS})
        else()
            message(WARNING "libcurl not found - HTTP client functionality may be limited")
            # 尝试直接链接 curl
            target_link_libraries(http_module PUBLIC curl)
        endif()
    else()
        message(WARNING "Neither CURL nor PkgConfig found - trying direct curl link")
        target_link_libraries(http_module PUBLIC curl)
    endif()
endif()

# 链接依赖库
target_link_libraries(http_module PUBLIC
    pthread
)

# 如果网络库存在，则链接
if(TARGET network_lib)
    target_link_libraries(http_module PUBLIC network_lib)
endif()

# 如果日志库存在，则链接
if(TARGET common_logger_lib)
    target_link_libraries(http_module PUBLIC common_logger_lib)
endif()

# 如果工具库存在，则链接（提供 IPBlacklistManager）
if(TARGET utils_lib)
    target_link_libraries(http_module PUBLIC utils_lib)
endif()

# 如果认证库存在，则链接（提供 JwtValidator）
if(TARGET common_auth)
    target_link_libraries(http_module PUBLIC common_auth)
endif()

message(STATUS "HTTP module created successfully with sources: ${HTTP_SOURCES}")
