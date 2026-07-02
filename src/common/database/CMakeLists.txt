cmake_minimum_required(VERSION 3.10)

# 数据库连接池库
add_library(database_lib
        mysql_pool.cpp
        redis_pool.cpp
)

# 设置C++标准
target_compile_features(database_lib PUBLIC cxx_std_17)

# 添加头文件搜索路径
target_include_directories(database_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# 查找MySQL Connector/C++库
find_package(PkgConfig QUIET)

# 首先尝试查找MySQL Connector/C++
find_path(MYSQL_CPPCONN_INCLUDE_DIR
    NAMES mysql_driver.h cppconn/driver.h
    PATHS
        /usr/include
        /usr/local/include
        /usr/include/mysql-cppconn-8
        /usr/include/mysql-cppconn
        /usr/include/cppconn
        /opt/mysql/include
    PATH_SUFFIXES mysql cppconn mysql-cppconn
)

find_library(MYSQL_CPPCONN_LIBRARY
    NAMES mysqlcppconn mysqlcppconn8 libmysqlcppconn
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/mysql
        /usr/local/lib/mysql
        /opt/mysql/lib
        /usr/lib/x86_64-linux-gnu
    PATH_SUFFIXES mysql
)

# 如果找到MySQL Connector/C++，设置相关变量
if(MYSQL_CPPCONN_INCLUDE_DIR AND MYSQL_CPPCONN_LIBRARY)
    set(MYSQL_FOUND TRUE)
    set(MYSQL_INCLUDE_DIRS ${MYSQL_CPPCONN_INCLUDE_DIR})
    set(MYSQL_LIBRARIES ${MYSQL_CPPCONN_LIBRARY})
    message(STATUS "Found MySQL Connector/C++: ${MYSQL_CPPCONN_LIBRARY}")
else()
    # 如果没找到Connector/C++，尝试查找基础MySQL客户端库
    if(PkgConfig_FOUND)
        pkg_check_modules(MYSQL QUIET mysqlclient)
    endif()

    if(NOT MYSQL_FOUND)
        find_path(MYSQL_INCLUDE_DIR
            NAMES mysql/mysql.h mysql.h
            PATHS
                /usr/include
                /usr/local/include
                /usr/include/mysql
                /usr/local/include/mysql
                /opt/mysql/include
            PATH_SUFFIXES mysql
        )

        find_library(MYSQL_LIBRARY
            NAMES mysqlclient mysql libmysql
            PATHS
                /usr/lib
                /usr/local/lib
                /usr/lib/mysql
                /usr/local/lib/mysql
                /opt/mysql/lib
                /usr/lib/x86_64-linux-gnu
            PATH_SUFFIXES mysql
        )

        if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
            set(MYSQL_FOUND TRUE)
            set(MYSQL_INCLUDE_DIRS ${MYSQL_INCLUDE_DIR})
            set(MYSQL_LIBRARIES ${MYSQL_LIBRARY})
            message(STATUS "Found MySQL client library: ${MYSQL_LIBRARY}")
        endif()
    endif()
endif()

# 查找Redis库 (hiredis)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(HIREDIS QUIET hiredis)
endif()

if(NOT HIREDIS_FOUND)
    # 手动查找hiredis
    find_path(HIREDIS_INCLUDE_DIR
        NAMES hiredis/hiredis.h hiredis.h
        PATHS
            /usr/include
            /usr/local/include
            /usr/include/hiredis
            /usr/local/include/hiredis
            /opt/hiredis/include
        PATH_SUFFIXES hiredis
    )

    find_library(HIREDIS_LIBRARY
        NAMES hiredis libhiredis
        PATHS
            /usr/lib
            /usr/local/lib
            /usr/lib/hiredis
            /usr/local/lib/hiredis
            /opt/hiredis/lib
            /usr/lib/x86_64-linux-gnu
        PATH_SUFFIXES hiredis
    )

    if(HIREDIS_INCLUDE_DIR AND HIREDIS_LIBRARY)
        set(HIREDIS_FOUND TRUE)
        set(HIREDIS_INCLUDE_DIRS ${HIREDIS_INCLUDE_DIR})
        set(HIREDIS_LIBRARIES ${HIREDIS_LIBRARY})
    endif()
endif()

# 查找并链接线程库
find_package(Threads REQUIRED)

# 链接库
target_link_libraries(database_lib
    PUBLIC
        Threads::Threads
)

if(MYSQL_FOUND)
    target_include_directories(database_lib PRIVATE ${MYSQL_INCLUDE_DIRS})
    target_link_libraries(database_lib PRIVATE ${MYSQL_LIBRARIES})
    target_compile_definitions(database_lib PRIVATE MYSQL_FOUND)
    message(STATUS "Found MySQL: ${MYSQL_LIBRARIES}")
else()
    message(WARNING "MySQL Connector/C++ not found. Using stub implementation.")
    message(STATUS "To install MySQL Connector/C++ development libraries:")
    message(STATUS "  Ubuntu/Debian: sudo apt-get install libmysqlcppconn-dev")
    message(STATUS "  CentOS/RHEL: sudo yum install mysql-connector-c++-devel")
    message(STATUS "  Or install from source: https://dev.mysql.com/downloads/connector/cpp/")
    target_compile_definitions(database_lib PRIVATE MYSQL_STUB)
endif()

if(HIREDIS_FOUND)
    target_include_directories(database_lib PRIVATE ${HIREDIS_INCLUDE_DIRS})
    target_link_libraries(database_lib PRIVATE ${HIREDIS_LIBRARIES})
    target_compile_definitions(database_lib PRIVATE HIREDIS_FOUND)
    message(STATUS "Found hiredis: ${HIREDIS_LIBRARIES}")
else()
    message(WARNING "hiredis not found. Using stub implementation.")
    message(STATUS "To install hiredis development libraries:")
    message(STATUS "  Ubuntu/Debian: sudo apt-get install libhiredis-dev")
    message(STATUS "  CentOS/RHEL: sudo yum install hiredis-devel")
    target_compile_definitions(database_lib PRIVATE HIREDIS_STUB)
endif()

# 设置编译选项
target_compile_options(database_lib PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)