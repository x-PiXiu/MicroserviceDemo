cmake_minimum_required(VERSION 3.10)

# 添加子目录 - 按依赖关系顺序构建
add_subdirectory(logger)        # 基础日志模块，其他模块都依赖它
add_subdirectory(utils)         # 工具模块（MD5、加密等）
add_subdirectory(config)        # 配置模块
add_subdirectory(thread_pool)   # 线程池模块
add_subdirectory(network)       # 网络模块，HTTP模块依赖它
add_subdirectory(auth)          # 认证模块（JWT验证器）- 必须在 http 之前
add_subdirectory(http)          # HTTP模块，依赖网络、日志和认证模块
add_subdirectory(database)      # 数据库模块
add_subdirectory(scheduler)     # 调度器模块
add_subdirectory(kafka)         # Kafka模块
add_subdirectory(monitoring)    # 性能监控模块
add_subdirectory(websocket)     # ⭐ WebSocket模块
add_subdirectory(service_registry)  # 服务注册客户端模块
#add_subdirectory(protocol)
add_subdirectory(timer)
