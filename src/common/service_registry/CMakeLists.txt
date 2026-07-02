# Service Registry Client Library
cmake_minimum_required(VERSION 3.14)

set(SERVICE_REGISTRY_SOURCES
    service_registry_client.cpp
)

add_library(common_service_registry STATIC ${SERVICE_REGISTRY_SOURCES})

target_include_directories(common_service_registry PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(common_service_registry PUBLIC
    http_module
    common_logger_lib
    nlohmann_json::nlohmann_json
)

target_compile_features(common_service_registry PUBLIC cxx_std_17)

# Set output directories
set_target_properties(common_service_registry PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
