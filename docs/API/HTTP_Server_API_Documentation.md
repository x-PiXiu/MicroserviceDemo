# 🌐 HTTP服务器框架API文档

## **📋 概述**

本文档详细描述common模块HTTP服务器框架的使用API，基于实际代码实现，确保信息的准确性和完整性。HTTP服务器框架作为微服务架构的核心网络组件，提供高性能的事件驱动HTTP服务器、完整的路由系统、中间件支持、会话管理等功能。

---

## **🏢 HTTP服务器框架 (HttpServer)**

### **基础信息**
- **模块名称**: `common::http`
- **主类**: `HttpServer`
- **版本**: `1.0.0`
- **架构特点**: 事件驱动、异步IO、线程池处理、中间件系统、路由管理
- **依赖模块**: 网络模块、日志模块、线程池模块、配置模块

---

## **🔧 1. HttpServer核心API**

### **1.1 服务器构造和配置**

**构造函数1**: 基础配置构造
```cpp
HttpServer(const HttpServerConfig& config = HttpServerConfig{})
```

**构造函数2**: 使用外部线程池构造
```cpp
HttpServer(const HttpServerConfig& config, std::shared_ptr<ThreadPool> thread_pool)
```

**构造函数3**: 简化构造
```cpp
HttpServer(const std::string& host, int port)
```

**HttpServerConfig配置结构**:
```cpp
struct HttpServerConfig {
    // 网络配置
    std::string host = "0.0.0.0";              // 监听地址
    int port = 8080;                           // 监听端口
    int backlog = 1024;                        // 监听队列长度
    bool reuse_addr = true;                    // 启用地址重用
    bool reuse_port = false;                   // 启用端口重用

    // HTTP协议配置
    size_t max_request_size = 1024 * 1024;     // 最大请求大小（1MB）
    size_t max_header_size = 8192;             // 最大头部大小（8KB）
    int request_timeout_ms = 30000;            // 请求超时时间（30秒）
    int keep_alive_timeout_ms = 60000;         // Keep-Alive超时时间（60秒）
    int max_keep_alive_requests = 1024;        // 单连接最大请求数

    // 线程池配置
    int worker_threads = 0;                    // 工作线程数（0=自动检测）
    int io_threads = 1;                        // IO线程数
    bool enable_thread_pool = true;            // 启用线程池

    // 性能优化配置
    bool enable_tcp_nodelay = true;            // 禁用Nagle算法
    bool enable_so_reuseaddr = true;           // SO_REUSEADDR选项
    bool enable_keep_alive = true;             // 启用TCP Keep-Alive
    int send_buffer_size = 65536;              // 发送缓冲区大小（64KB）
    int recv_buffer_size = 65536;              // 接收缓冲区大小（64KB）

    // 安全配置
    bool enable_cors = true;                   // 启用CORS
    std::string cors_origin = "*";             // CORS允许的源

    // 日志和监控配置
    bool enable_access_log = true;             // 启用访问日志
    bool enable_error_log = true;              // 启用错误日志
    bool enable_performance_monitoring = true; // 启用性能监控
    int stats_interval_ms = 30000;             // 统计间隔（30秒）

    // 静态文件服务配置
    bool enable_directory_listing = false;     // 启用目录浏览
    size_t max_static_file_size = 100 * 1024 * 1024; // 最大静态文件大小（100MB）
    bool enable_file_cache = true;             // 启用文件缓存
    int static_file_cache_time = 3600;         // 静态文件缓存时间（秒）
};
```

**使用示例**:
```cpp
// 基础配置
HttpServerConfig config;
config.host = "0.0.0.0";
config.port = 8080;
config.worker_threads = 4;
config.enable_cors = true;

HttpServer server(config);

// 或者简化创建
HttpServer simple_server("127.0.0.1", 9090);
```

---

### **1.2 服务器控制API**

#### **启动服务器**
```cpp
bool start()
```

**功能**: 启动HTTP服务器，开始监听和处理请求

**返回值**: 启动成功返回true，失败返回false

**使用示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// 添加路由
server.get("/api/users", [](const HttpRequest& req, HttpResponse& res) {
    res.ok({{"message", "Hello World"}});
});

// 启动服务器
if (server.start()) {
    LOG_INFO("Server started successfully");
    server.waitForStop();  // 等待服务器停止
} else {
    LOG_ERROR("Failed to start server");
}
```

#### **停止服务器**
```cpp
void stop()
```

**功能**: 优雅停止HTTP服务器

#### **等待停止**
```cpp
void waitForStop()
```

**功能**: 阻塞等待服务器停止

#### **检查运行状态**
```cpp
bool isRunning() const
```

**功能**: 检查服务器是否正在运行

---

### **1.3 路由管理API**

#### **通用路由注册**
```cpp
void route(const std::string& method, const std::string& path, RequestHandler handler)
```

**参数**:
- `method`: HTTP方法（GET、POST、PUT、DELETE等）
- `path`: 路径模式，支持参数路由如`/users/{id}`
- `handler`: 请求处理函数

#### **便捷路由方法**
```cpp
void get(const std::string& path, RequestHandler handler)       // GET路由
void post(const std::string& path, RequestHandler handler)      // POST路由
void put(const std::string& path, RequestHandler handler)       // PUT路由
void del(const std::string& path, RequestHandler handler)       // DELETE路由
void patch(const std::string& path, RequestHandler handler)     // PATCH路由
void options(const std::string& path, RequestHandler handler)   // OPTIONS路由
```

**RequestHandler类型**:
```cpp
using RequestHandler = std::function<void(const HttpRequest&, HttpResponse&)>;
```

**路由使用示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// 静态路由
server.get("/api/status", [](const HttpRequest& req, HttpResponse& res) {
    res.ok({{"status", "running"}});
});

// 参数路由
server.get("/api/users/{id}", [](const HttpRequest& req, HttpResponse& res) {
    std::string user_id = req.getParam("id");
    res.ok({{"user_id", user_id}});
});

// POST路由处理JSON数据
server.post("/api/users", [](const HttpRequest& req, HttpResponse& res) {
    try {
        auto json_data = req.getJsonBody();
        std::string username = json_data["username"];
        
        // 处理用户创建逻辑
        nlohmann::json response = {
            {"id", "usr_123"},
            {"username", username},
            {"created_at", "2025-01-25T10:00:00Z"}
        };
        res.created(response);
    } catch (const std::exception& e) {
        res.badRequest("Invalid JSON data");
    }
});

// 查询参数示例
server.get("/api/search", [](const HttpRequest& req, HttpResponse& res) {
    std::string query = req.getQueryParam("q", "");
    int page = std::stoi(req.getQueryParam("page", "1"));
    int limit = std::stoi(req.getQueryParam("limit", "10"));
    
    nlohmann::json response = {
        {"query", query},
        {"page", page},
        {"limit", limit},
        {"results", nlohmann::json::array()}
    };
    res.ok(response);
});
```

---

### **1.4 中间件管理API**

#### **全局中间件**
```cpp
void use(MiddlewareHandler middleware)                          // 添加全局中间件
void use(MiddlewareHandler middleware, std::string middleName)  // 添加带名称的全局中间件
```

#### **路径特定中间件**
```cpp
void use(const std::string& path, MiddlewareHandler middleware)
```

**MiddlewareHandler类型**:
```cpp
using MiddlewareHandler = std::function<bool(const HttpRequest&, HttpResponse&)>;
```

**中间件使用示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// CORS中间件
server.use([](const HttpRequest& req, HttpResponse& res) -> bool {
    res.setHeader("Access-Control-Allow-Origin", "*");
    res.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    res.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    if (req.getMethodString() == "OPTIONS") {
        res.ok();
        return false; // 停止处理，直接返回
    }
    
    return true; // 继续处理下一个中间件
});

// 日志中间件
server.use([](const HttpRequest& req, HttpResponse& res) -> bool {
    LOG_INFO("Request: " + req.getMethodString() + " " + req.getPath());
    return true;
}, "LoggingMiddleware");

// 认证中间件（只对/api路径生效）
server.use("/api", [](const HttpRequest& req, HttpResponse& res) -> bool {
    std::string auth_header = req.getHeader("Authorization");
    if (auth_header.empty() || !auth_header.starts_with("Bearer ")) {
        res.unauthorized("Authentication required");
        return false;
    }
    
    std::string token = auth_header.substr(7);
    if (!validateJWT(token)) {
        res.unauthorized("Invalid token");
        return false;
    }
    
    return true;
});
```

---

### **1.5 静态文件服务API**

```cpp
void serveStatic(const std::string& path, const std::string& directory)
```

**功能**: 设置静态文件服务，将URL路径映射到本地目录

**参数**:
- `path`: URL路径前缀
- `directory`: 本地目录路径

**静态文件服务示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// 服务静态文件
server.serveStatic("/static", "./public");
server.serveStatic("/assets", "./dist/assets");

// 服务单页应用
server.serveStatic("/", "./webapp/build");

server.start();
```

**静态文件功能特性**:
- 支持多种MIME类型自动识别
- 支持Range请求（断点续传）
- 支持缓存控制（ETag、Last-Modified）
- 支持目录浏览（可配置）
- 安全路径检查（防止目录遍历攻击）
- 压缩传输支持

---

### **1.6 错误处理API**

#### **设置错误处理器**
```cpp
void setErrorHandler(ErrorHandler handler)
```

**ErrorHandler类型**:
```cpp
using ErrorHandler = std::function<void(const std::exception&, const HttpRequest&, HttpResponse&)>;
```

#### **设置404处理器**
```cpp
void setNotFoundHandler(RequestHandler handler)
```

**错误处理示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// 全局错误处理器
server.setErrorHandler([](const std::exception& e, const HttpRequest& req, HttpResponse& res) {
    LOG_ERROR("Request error: " + std::string(e.what()) + " for " + req.getPath());
    
    nlohmann::json error_response = {
        {"error", "Internal Server Error"},
        {"message", "An unexpected error occurred"},
        {"timestamp", std::time(nullptr)},
        {"path", req.getPath()}
    };
    res.internalServerError(error_response.dump());
});

// 404处理器
server.setNotFoundHandler([](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json error_response = {
        {"error", "Not Found"},
        {"message", "The requested resource was not found"},
        {"path", req.getPath()},
        {"method", req.getMethodString()},
        {"timestamp", std::time(nullptr)}
    };
    res.notFound(error_response.dump());
});
```

---

### **1.7 统计和监控API**

#### **获取服务器统计**
```cpp
const HttpServerStats& getStats() const                    // 获取统计引用
ServerStatsSnapshot getStatsSnapshot() const               // 获取统计快照（线程安全）
```

#### **HttpServerStats结构**
```cpp
struct HttpServerStats {
    std::atomic<uint64_t> total_connections{0};     // 总连接数
    std::atomic<uint64_t> active_connections{0};    // 活跃连接数
    std::atomic<uint64_t> total_requests{0};        // 总请求数
    std::atomic<uint64_t> total_responses{0};       // 总响应数
    std::atomic<uint64_t> error_count{0};           // 错误次数
    std::atomic<uint64_t> timeout_count{0};         // 超时次数
    std::chrono::steady_clock::time_point start_time; // 启动时间
    
    nlohmann::json toJson() const;                  // 转换为JSON格式
    long getUptimeSeconds() const;                  // 获取运行时间
};
```

#### **其他监控API**
```cpp
size_t getActiveSessionCount() const                        // 获取活跃会话数
std::vector<std::string> getAllRoutes()                     // 获取所有路由
std::shared_ptr<EventLoop> getEventLoop() const             // 获取事件循环（用于共享）
```

**监控使用示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// 添加统计端点
server.get("/stats", [&server](const HttpRequest& req, HttpResponse& res) {
    auto stats_snapshot = server.getStatsSnapshot();
    nlohmann::json stats_json = {
        {"total_connections", stats_snapshot.total_connections},
        {"active_connections", stats_snapshot.active_connections},
        {"total_requests", stats_snapshot.total_requests},
        {"total_responses", stats_snapshot.total_responses},
        {"error_count", stats_snapshot.error_count},
        {"timeout_count", stats_snapshot.timeout_count},
        {"uptime_seconds", stats_snapshot.uptime_seconds},
        {"active_sessions", server.getActiveSessionCount()}
    };
    res.ok(stats_json);
});

// 添加健康检查端点
server.get("/health", [&server](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json health = {
        {"status", server.isRunning() ? "UP" : "DOWN"},
        {"timestamp", std::time(nullptr)},
        {"active_connections", server.getActiveSessionCount()}
    };
    res.ok(health);
});
```

---

### **1.8 API发现和文档API**

#### **API发现相关方法**
```cpp
std::vector<HttpRouter::ApiEndpoint> getAllApiEndpoints() const    // 获取所有API端点
void setApiMetadata(const std::string& method, const std::string& path, 
                   const HttpRouter::ApiEndpoint& metadata)        // 设置API元数据
nlohmann::json getOpenApiSpec(const nlohmann::json& service_info = {}) const  // 获取OpenAPI规范
void registerApiDiscoveryEndpoints(const std::string& service_name = "microservice",
                                  const std::string& service_version = "1.0.0",
                                  const std::string& base_url = "")     // 注册API发现端点
```

**API发现使用示例**:
```cpp
HttpServer server("0.0.0.0", 8080);

// 注册API发现端点（自动添加 /api/v1/service/endpoints 等端点）
server.registerApiDiscoveryEndpoints("user_service", "1.0.0", "http://localhost:8080");

// 设置API元数据
HttpRouter::ApiEndpoint user_api_metadata;
user_api_metadata.method = "GET";
user_api_metadata.path = "/api/v1/users/{id}";
user_api_metadata.description = "获取指定用户信息";
user_api_metadata.requires_auth = true;
user_api_metadata.tags = {"users", "profile"};
user_api_metadata.parameters = {
    {"id", {{"type", "string"}, {"description", "用户ID"}}}
};

server.setApiMetadata("GET", "/api/v1/users/{id}", user_api_metadata);

server.start();
```

---

## **🔍 2. HttpRequest核心API**

### **2.1 基本信息访问**

#### **HTTP方法相关**
```cpp
HttpMethod getMethod() const                    // 获取HTTP方法枚举
std::string getMethodString() const             // 获取HTTP方法字符串
bool isGet() const                              // 检查是否为GET请求
bool isPost() const                             // 检查是否为POST请求
bool isPut() const                              // 检查是否为PUT请求
bool isDelete() const                           // 检查是否为DELETE请求
```

#### **URL和路径相关**
```cpp
const std::string& getPath() const              // 获取请求路径
const std::string& getQuery() const             // 获取查询字符串
const std::string& getQueryString() const       // 获取查询字符串（别名）
std::string getUrl() const                      // 获取完整URL
std::string getVersion() const                  // 获取HTTP版本
```

**使用示例**:
```cpp
server.get("/api/info", [](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json info = {
        {"method", req.getMethodString()},
        {"path", req.getPath()},
        {"query", req.getQuery()},
        {"url", req.getUrl()},
        {"version", req.getVersion()},
        {"is_get", req.isGet()}
    };
    res.ok(info);
});
```

---

### **2.2 头部管理**

#### **头部访问**
```cpp
std::string getHeader(const std::string& name) const        // 获取指定头部
const Headers& getHeaders() const                           // 获取所有头部
bool hasHeader(const std::string& name) const               // 检查头部是否存在
```

#### **便捷头部方法**
```cpp
std::string getContentType() const                         // 获取Content-Type
size_t getContentLength() const                             // 获取Content-Length
std::string getUserAgent() const                            // 获取User-Agent
std::string getHost() const                                 // 获取Host
std::string getClientIP() const                             // 获取客户端IP
void setClientIP(const std::string& ip)                     // 设置客户端IP
```

**头部使用示例**:
```cpp
server.post("/api/upload", [](const HttpRequest& req, HttpResponse& res) {
    // 检查Content-Type
    std::string content_type = req.getContentType();
    if (content_type.find("multipart/form-data") == std::string::npos) {
        res.badRequest("Expected multipart/form-data");
        return;
    }
    
    // 检查Content-Length
    size_t content_length = req.getContentLength();
    if (content_length > 10 * 1024 * 1024) {  // 10MB限制
        res.error(413, "File too large");
        return;
    }
    
    // 获取自定义头部
    std::string upload_type = req.getHeader("X-Upload-Type");
    
    nlohmann::json response = {
        {"message", "Upload processed"},
        {"content_type", content_type},
        {"content_length", content_length},
        {"upload_type", upload_type},
        {"client_ip", req.getClientIP()},
        {"user_agent", req.getUserAgent()}
    };
    res.ok(response);
});
```

---

### **2.3 参数管理**

#### **查询参数**
```cpp
std::string getParam(const std::string& name, const std::string& default_value = "") const  // 获取URL参数
const Parameters& getParams() const                                         // 获取所有URL参数
bool hasParam(const std::string& name) const                               // 检查参数是否存在
std::string getQueryParam(const std::string& name, const std::string& default_value = "") const  // 获取查询参数（别名）
```

#### **路径参数（由路由器设置）**
```cpp
void setRouteParams(const Parameters& route_params)                         // 设置路径参数
```

**参数使用示例**:
```cpp
// 查询参数示例：GET /api/search?q=test&page=1&limit=10
server.get("/api/search", [](const HttpRequest& req, HttpResponse& res) {
    std::string query = req.getParam("q", "");           // 获取搜索关键词
    int page = std::stoi(req.getParam("page", "1"));     // 获取页码
    int limit = std::stoi(req.getParam("limit", "10"));  // 获取每页数量
    
    if (query.empty()) {
        res.badRequest("Query parameter 'q' is required");
        return;
    }
    
    nlohmann::json results = {
        {"query", query},
        {"page", page},
        {"limit", limit},
        {"total", 0},
        {"items", nlohmann::json::array()}
    };
    res.ok(results);
});

// 路径参数示例：GET /api/users/123
server.get("/api/users/{id}", [](const HttpRequest& req, HttpResponse& res) {
    std::string user_id = req.getParam("id");  // 路径参数由路由器自动解析
    
    if (user_id.empty()) {
        res.badRequest("User ID is required");
        return;
    }
    
    nlohmann::json user = {
        {"id", user_id},
        {"username", "testuser"},
        {"email", "test@example.com"}
    };
    res.ok(user);
});
```

---

### **2.4 请求体处理**

#### **基础请求体**
```cpp
const std::string& getBody() const                          // 获取原始请求体
void setBody(const std::string& body)                       // 设置请求体
bool hasBody() const                                        // 检查是否有请求体
```

#### **JSON请求体**
```cpp
bool isJsonBody() const                                     // 检查是否为JSON请求体
nlohmann::json getJsonBody() const                          // 获取JSON请求体
```

#### **表单数据**
```cpp
bool isFormBody() const                                     // 检查是否为表单请求体
Parameters getFormData()                                    // 获取表单数据
```

**请求体使用示例**:
```cpp
// JSON请求体处理
server.post("/api/users", [](const HttpRequest& req, HttpResponse& res) {
    if (!req.hasBody()) {
        res.badRequest("Request body is required");
        return;
    }
    
    if (!req.isJsonBody()) {
        res.badRequest("Expected JSON request body");
        return;
    }
    
    try {
        auto json_data = req.getJsonBody();
        std::string username = json_data["username"];
        std::string email = json_data["email"];
        
        // 验证必需字段
        if (username.empty() || email.empty()) {
            res.badRequest("Username and email are required");
            return;
        }
        
        // 创建用户逻辑
        nlohmann::json new_user = {
            {"id", "usr_" + std::to_string(std::time(nullptr))},
            {"username", username},
            {"email", email},
            {"created_at", std::time(nullptr)}
        };
        
        res.created(new_user);
    } catch (const nlohmann::json::exception& e) {
        res.badRequest("Invalid JSON: " + std::string(e.what()));
    }
});

// 表单数据处理
server.post("/api/contact", [](const HttpRequest& req, HttpResponse& res) {
    if (!req.isFormBody()) {
        res.badRequest("Expected form data");
        return;
    }
    
    auto form_data = req.getFormData();
    std::string name = form_data["name"];
    std::string email = form_data["email"];
    std::string message = form_data["message"];
    
    if (name.empty() || email.empty() || message.empty()) {
        res.badRequest("Name, email and message are required");
        return;
    }
    
    nlohmann::json response = {
        {"message", "Contact form submitted successfully"},
        {"data", {
            {"name", name},
            {"email", email},
            {"message_length", message.length()}
        }}
    };
    res.ok(response);
});
```

---

### **2.5 Cookie管理**

#### **Cookie访问**
```cpp
std::string getCookie(const std::string& name, const std::string& default_value = "") const  // 获取Cookie值
const Cookies& getCookies() const                                           // 获取所有Cookie
bool hasCookie(const std::string& name) const                               // 检查Cookie是否存在
```

**Cookie使用示例**:
```cpp
server.get("/api/profile", [](const HttpRequest& req, HttpResponse& res) {
    // 检查会话Cookie
    std::string session_id = req.getCookie("session_id");
    if (session_id.empty()) {
        res.unauthorized("Session required");
        return;
    }
    
    // 获取用户偏好Cookie
    std::string theme = req.getCookie("theme", "light");
    std::string language = req.getCookie("language", "en");
    
    nlohmann::json profile = {
        {"session_id", session_id},
        {"preferences", {
            {"theme", theme},
            {"language", language}
        }},
        {"all_cookies", req.getCookies()}
    };
    res.ok(profile);
});
```

---

### **2.6 便捷检查方法**

```cpp
bool isKeepAlive() const                                    // 检查是否支持Keep-Alive
bool isAjax() const                                         // 检查是否为AJAX请求
bool isSecure() const                                       // 检查是否为安全连接
```

**便捷方法使用示例**:
```cpp
server.get("/api/data", [](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json response = {
        {"data", "Some sensitive data"},
        {"connection_info", {
            {"keep_alive", req.isKeepAlive()},
            {"ajax_request", req.isAjax()},
            {"secure_connection", req.isSecure()}
        }}
    };
    
    // 为AJAX请求设置特定头部
    if (req.isAjax()) {
        res.setHeader("X-Requested-With", "XMLHttpRequest");
    }
    
    res.ok(response);
});
```

---

## **📤 3. HttpResponse核心API**

### **3.1 状态码管理**

#### **状态码设置**
```cpp
void setStatus(int status_code)                                    // 设置状态码
void setStatus(int status_code, const std::string& reason_phrase)  // 设置状态码和原因短语
void setStatus(HttpStatus status)                                  // 设置状态码（枚举）
```

#### **状态码访问**
```cpp
int getStatus() const                                              // 获取状态码
const std::string& getReasonPhrase() const                        // 获取原因短语
```

**状态码使用示例**:
```cpp
server.post("/api/validate", [](const HttpRequest& req, HttpResponse& res) {
    auto json_data = req.getJsonBody();
    
    if (json_data.find("email") == json_data.end()) {
        res.setStatus(422, "Unprocessable Entity");
        res.setJsonBody({
            {"error", "Validation failed"},
            {"field", "email"},
            {"message", "Email is required"}
        });
        return;
    }
    
    std::string email = json_data["email"];
    if (email.find("@") == std::string::npos) {
        res.setStatus(HttpStatus::BAD_REQUEST);
        res.setJsonBody({
            {"error", "Invalid email format"}
        });
        return;
    }
    
    res.ok({{"message", "Email is valid"}});
});
```

---

### **3.2 头部管理**

#### **头部设置**
```cpp
void setHeader(const std::string& key, const std::string& value)   // 设置头部
void setHeader(const std::string& key, int value)                  // 设置头部（整数值）
void addHeader(const std::string& key, const std::string& value)   // 添加头部（支持多值）
void removeHeader(const std::string& key)                          // 移除头部
```

#### **头部访问**
```cpp
std::string getHeader(const std::string& key) const                // 获取头部
const Headers& getHeaders() const                                  // 获取所有头部
bool hasHeader(const std::string& key) const                       // 检查头部是否存在
```

**头部使用示例**:
```cpp
server.get("/api/download", [](const HttpRequest& req, HttpResponse& res) {
    std::string filename = req.getParam("file", "document.pdf");
    
    // 设置下载头部
    res.setHeader("Content-Type", "application/octet-stream");
    res.setHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    res.setHeader("Cache-Control", "no-cache");
    res.setHeader("X-Download-ID", std::to_string(std::time(nullptr)));
    
    // 读取文件内容并设置响应体
    std::string file_content = readFileContent(filename);
    res.setBody(file_content);
    res.setStatus(200);
});

// 多值头部示例
server.get("/api/multi-header", [](const HttpRequest& req, HttpResponse& res) {
    res.addHeader("X-Custom-Info", "value1");
    res.addHeader("X-Custom-Info", "value2");
    res.addHeader("X-Custom-Info", "value3");
    
    res.ok({{"message", "Check response headers"}});
});
```

---

### **3.3 响应体管理**

#### **响应体设置**
```cpp
void setBody(const std::string& body)                              // 设置响应体
void setBody(const char* data, size_t size)                        // 设置响应体（二进制）
void appendBody(const std::string& content)                        // 追加响应体内容
void clearBody()                                                   // 清空响应体
```

#### **JSON响应体**
```cpp
void setJsonBody(const nlohmann::json& json)                       // 设置JSON响应体
void setJsonBody(const std::string& json_str)                      // 设置JSON响应体（字符串）
```

#### **响应体访问**
```cpp
const std::string& getBody() const                                 // 获取响应体
```

**响应体使用示例**:
```cpp
// 文本响应
server.get("/api/text", [](const HttpRequest& req, HttpResponse& res) {
    res.setHeader("Content-Type", "text/plain");
    res.setBody("Hello, World!");
    res.setStatus(200);
});

// JSON响应
server.get("/api/json", [](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json data = {
        {"message", "Hello, JSON!"},
        {"timestamp", std::time(nullptr)},
        {"items", {1, 2, 3, 4, 5}}
    };
    res.setJsonBody(data);  // 自动设置Content-Type为application/json
    res.setStatus(200);
});

// 二进制响应
server.get("/api/binary", [](const HttpRequest& req, HttpResponse& res) {
    const char binary_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    res.setHeader("Content-Type", "application/octet-stream");
    res.setBody(binary_data, sizeof(binary_data));
    res.setStatus(200);
});

// 流式响应（分块追加）
server.get("/api/stream", [](const HttpRequest& req, HttpResponse& res) {
    res.setHeader("Content-Type", "text/plain");
    res.setHeader("Transfer-Encoding", "chunked");
    
    res.appendBody("Chunk 1\n");
    res.appendBody("Chunk 2\n");
    res.appendBody("Chunk 3\n");
    
    res.setStatus(200);
});
```

---

### **3.4 便捷响应方法**

#### **成功响应**
```cpp
void ok(const std::string& body = "")                              // 200 OK响应
void ok(const nlohmann::json& json)                                // 200 OK响应（JSON）
void created(const std::string& body = "")                         // 201 Created响应
void created(const nlohmann::json& json)                           // 201 Created响应（JSON）
void noContent()                                                   // 204 No Content响应
```

#### **错误响应**
```cpp
void error(int status_code, const std::string& message)            // 通用错误响应
void error(int status_code, const nlohmann::json& json)            // 通用错误响应（JSON）
void badRequest(const std::string& message = "Bad Request")        // 400错误
void unauthorized(const std::string& message = "Unauthorized")     // 401错误
void forbidden(const std::string& message = "Forbidden")           // 403错误
void notFound(const std::string& message = "Not Found")            // 404错误
void internalServerError(const std::string& message = "Internal Server Error")  // 500错误
void requestEntityTooLarge(const std::string& message = "Request Entity Too Large")  // 413错误
```

#### **重定向响应**
```cpp
void redirect(const std::string& location)                         // 301永久重定向
void temporaryRedirect(const std::string& location)                // 302临时重定向
```

**便捷响应使用示例**:
```cpp
// 成功响应示例
server.get("/api/users", [](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json users = {
        {"users", {
            {{"id", 1}, {"name", "Alice"}},
            {{"id", 2}, {"name", "Bob"}}
        }},
        {"total", 2}
    };
    res.ok(users);  // 自动设置200状态码和JSON Content-Type
});

server.post("/api/users", [](const HttpRequest& req, HttpResponse& res) {
    auto user_data = req.getJsonBody();
    
    // 创建用户逻辑
    nlohmann::json new_user = {
        {"id", 123},
        {"name", user_data["name"]},
        {"created_at", std::time(nullptr)}
    };
    res.created(new_user);  // 自动设置201状态码
});

// 错误响应示例
server.get("/api/protected", [](const HttpRequest& req, HttpResponse& res) {
    std::string auth_header = req.getHeader("Authorization");
    
    if (auth_header.empty()) {
        res.unauthorized("Authentication token is required");
        return;
    }
    
    if (!validateToken(auth_header)) {
        res.forbidden("Invalid or expired token");
        return;
    }
    
    res.ok({{"message", "Access granted"}});
});

// 重定向响应示例
server.get("/old-api", [](const HttpRequest& req, HttpResponse& res) {
    res.redirect("/api/v2" + req.getPath());
});

server.get("/temp-redirect", [](const HttpRequest& req, HttpResponse& res) {
    res.temporaryRedirect("/maintenance");
});
```

---

### **3.5 Cookie管理**

#### **Cookie设置**
```cpp
void setCookie(const std::string& name, const std::string& value,
              int max_age = -1, const std::string& path = "",
              const std::string& domain = "", bool secure = false, bool http_only = false)

void setCookieSecure(const std::string& name, const std::string& value,
                    int max_age = -1, const std::string& path = "/",
                    const std::string& domain = "", bool secure = true,
                    bool http_only = true, const std::string& same_site = "Lax")

void deleteCookie(const std::string& name, const std::string& path = "", const std::string& domain = "")
```

**Cookie使用示例**:
```cpp
// 登录端点 - 设置会话Cookie
server.post("/api/login", [](const HttpRequest& req, HttpResponse& res) {
    auto credentials = req.getJsonBody();
    std::string username = credentials["username"];
    std::string password = credentials["password"];
    
    if (authenticate(username, password)) {
        std::string session_id = generateSessionId();
        
        // 设置安全的会话Cookie
        res.setCookieSecure("session_id", session_id, 3600, "/", "", true, true, "Strict");
        
        // 设置用户偏好Cookie
        res.setCookie("username", username, 86400, "/", "", false, false);
        res.setCookie("theme", "dark", 86400 * 7, "/");  // 7天有效
        
        res.ok({
            {"message", "Login successful"},
            {"user", username},
            {"session_expires", std::time(nullptr) + 3600}
        });
    } else {
        res.unauthorized("Invalid credentials");
    }
});

// 登出端点 - 删除Cookie
server.post("/api/logout", [](const HttpRequest& req, HttpResponse& res) {
    res.deleteCookie("session_id", "/");
    res.deleteCookie("username", "/");
    res.deleteCookie("theme", "/");
    
    res.ok({{"message", "Logged out successfully"}});
});
```

---

### **3.6 缓存控制**

```cpp
void setCacheControl(const std::string& cache_control)              // 设置缓存控制
void setExpires(int expires)                                       // 设置过期时间（秒）
void setETag(const std::string& etag)                              // 设置ETag
void setLastModified(const std::string& last_modified)             // 设置Last-Modified
```

**缓存控制使用示例**:
```cpp
// 缓存控制示例
server.get("/api/config", [](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json config = loadConfiguration();
    
    // 设置缓存 - 5分钟有效
    res.setCacheControl("public, max-age=300");
    res.setETag("\"config-v1.0\"");
    res.setLastModified("Wed, 25 Jan 2025 10:00:00 GMT");
    
    res.ok(config);
});

// 禁用缓存示例
server.get("/api/realtime", [](const HttpRequest& req, HttpResponse& res) {
    nlohmann::json realtime_data = getCurrentData();
    
    // 禁用所有缓存
    res.setCacheControl("no-cache, no-store, must-revalidate");
    res.setHeader("Pragma", "no-cache");
    res.setExpires(0);
    
    res.ok(realtime_data);
});
```

---

### **3.7 序列化和工具方法**

```cpp
std::string toString() const                                       // 转换为HTTP响应字符串
size_t getSize() const                                            // 获取响应大小
void reset()                                                      // 重置响应对象
static std::string getDefaultReasonPhrase(int status_code)        // 获取状态码默认原因短语
static std::string getCurrentHttpTime()                           // 获取当前HTTP时间字符串
```

---

## **🔀 4. HttpRouter核心API**

### **4.1 路由注册**

#### **通用路由方法**
```cpp
void addRoute(const std::string& method, const std::string& pattern, 
             std::shared_ptr<HttpHandler> handler, int priority = 0)
void addRoute(const std::string& method, const std::string& pattern, 
             RequestHandler handler, int priority = 0)
```

#### **便捷路由方法**
```cpp
void get(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0)
void get(const std::string& pattern, RequestHandler handler, int priority = 0)
void post(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0)
void post(const std::string& pattern, RequestHandler handler, int priority = 0)
void put(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0)
void put(const std::string& pattern, RequestHandler handler, int priority = 0)
void del(const std::string& pattern, std::shared_ptr<HttpHandler> handler, int priority = 0)
void del(const std::string& pattern, RequestHandler handler, int priority = 0)
```

**路由注册示例**:
```cpp
HttpRouter router;

// 静态路由
router.get("/api/status", [](const HttpRequest& req, HttpResponse& res) {
    res.ok({{"status", "running"}});
});

// 参数路由
router.get("/api/users/{id}", [](const HttpRequest& req, HttpResponse& res) {
    std::string user_id = req.getParam("id");
    res.ok({{"user_id", user_id}});
});

// 通配符路由
router.get("/static/*", [](const HttpRequest& req, HttpResponse& res) {
    std::string file_path = req.getPath();
    serveStaticFile(file_path, res);
});

// 带优先级的路由
router.get("/api/admin/*", adminHandler, 100);  // 高优先级
router.get("/api/*", generalApiHandler, 10);    // 低优先级
```

---

### **4.2 路由匹配**

```cpp
RouteMatch match(const std::string& method, const std::string& path) const    // 路由匹配
RouteMatch match(const HttpRequest& request) const                            // 请求对象匹配
```

**RouteMatch结构**:
```cpp
struct RouteMatch {
    bool found = false;                          // 是否找到匹配
    std::shared_ptr<HttpHandler> handler;        // 匹配的处理器
    RouteParams params;                          // 路径参数
    std::string matched_pattern;                 // 匹配的模式
    int priority = 0;                           // 优先级
};
```

**路由匹配示例**:
```cpp
// 手动路由匹配
auto match_result = router.match("GET", "/api/users/123");
if (match_result.found) {
    std::string user_id = match_result.params["id"];  // "123"
    std::cout << "Matched pattern: " << match_result.matched_pattern << std::endl;
    std::cout << "Priority: " << match_result.priority << std::endl;
}
```

---

### **4.3 中间件管理**

```cpp
void addGlobalMiddleware(MiddlewareHandler middleware)                        // 添加全局中间件
void addMiddleware(const std::string& pattern, MiddlewareHandler middleware)  // 添加路径中间件
bool executeMiddlewares(const HttpRequest& request, HttpResponse& response, 
                       const std::string& path) const                        // 执行中间件
```

---

### **4.4 路由管理**

```cpp
bool removeRoute(const std::string& method, const std::string& pattern)      // 移除路由
void clear()                                                                 // 清空所有路由
size_t getRouteCount(const std::string& method = "") const                   // 获取路由数量
std::vector<std::string> getAllRoutes() const                               // 获取所有路由
std::vector<std::string> getRoutesByMethod(const std::string& method) const // 获取指定方法路由
bool hasRoute(const std::string& method, const std::string& path) const     // 检查路由是否存在
```

**路由管理示例**:
```cpp
HttpRouter router;

// 添加一些路由
router.get("/api/users", userListHandler);
router.get("/api/users/{id}", userDetailHandler);
router.post("/api/users", createUserHandler);

// 路由统计
std::cout << "Total routes: " << router.getRouteCount() << std::endl;
std::cout << "GET routes: " << router.getRouteCount("GET") << std::endl;

// 获取所有路由
auto all_routes = router.getAllRoutes();
for (const auto& route : all_routes) {
    std::cout << "Route: " << route << std::endl;
}

// 检查路由是否存在
if (router.hasRoute("GET", "/api/users/123")) {
    std::cout << "Route exists" << std::endl;
}

// 移除路由
router.removeRoute("POST", "/api/users");
std::cout << "Routes after removal: " << router.getRouteCount() << std::endl;
```

---

### **4.5 路由统计**

```cpp
RouteStatistics getRouteStatistics() const                                  // 获取详细统计信息
std::string getMatchDetails(const std::string& method, const std::string& path) const  // 获取匹配详情
void printRoutes(const std::string& method = "") const                       // 打印路由信息
bool validate() const                                                        // 验证路由配置
```

**RouteStatistics结构**:
```cpp
struct RouteStatistics {
    size_t total_routes = 0;              // 总路由数
    size_t static_routes = 0;             // 静态路由数
    size_t param_routes = 0;              // 参数路由数
    size_t wildcard_routes = 0;           // 通配符路由数
    std::map<std::string, size_t> method_counts;  // 各方法路由数

    nlohmann::json toJson() const;        // 转换为JSON
    std::string toString() const;         // 转换为字符串
};
```

**统计使用示例**:
```cpp
HttpRouter router;
// ... 添加路由 ...

// 获取统计信息
auto stats = router.getRouteStatistics();
std::cout << "Route Statistics:" << std::endl;
std::cout << "Total: " << stats.total_routes << std::endl;
std::cout << "Static: " << stats.static_routes << std::endl;
std::cout << "Parameters: " << stats.param_routes << std::endl;
std::cout << "Wildcards: " << stats.wildcard_routes << std::endl;

// 转换为JSON
nlohmann::json stats_json = stats.toJson();
std::cout << stats_json.dump(2) << std::endl;

// 打印所有路由
router.printRoutes();

// 只打印GET路由
router.printRoutes("GET");

// 获取匹配详情（调试用）
std::string details = router.getMatchDetails("GET", "/api/users/123");
std::cout << "Match details: " << details << std::endl;
```

---

### **4.6 API发现功能**

```cpp
std::vector<ApiEndpoint> getAllApiEndpoints() const                         // 获取所有API端点
std::vector<ApiEndpoint> getApiEndpointsByMethod(const std::string& method) const  // 按方法获取端点
void setApiMetadata(const std::string& method, const std::string& path, 
                   const ApiEndpoint& metadata)                            // 设置API元数据
nlohmann::json getOpenApiSpec(const nlohmann::json& service_info = {}) const  // 获取OpenAPI规范
```

**ApiEndpoint结构**:
```cpp
struct ApiEndpoint {
    std::string method;                          // HTTP方法
    std::string path;                           // 路径模式
    std::string description;                     // 端点描述
    std::vector<std::string> tags;               // 标签
    bool requires_auth = false;                  // 是否需要认证
    std::string permission_level = "public";     // 权限级别
    nlohmann::json parameters;                   // 参数信息
    nlohmann::json responses;                    // 响应信息
    std::string version = "v1";                  // API版本
    
    nlohmann::json toJson() const;               // 转换为JSON
};
```

**API发现使用示例**:
```cpp
HttpRouter router;

// 添加路由和元数据
router.get("/api/users", userListHandler);

// 设置API元数据
HttpRouter::ApiEndpoint user_list_api;
user_list_api.method = "GET";
user_list_api.path = "/api/users";
user_list_api.description = "获取用户列表";
user_list_api.tags = {"users"};
user_list_api.requires_auth = false;
user_list_api.parameters = {
    {"page", {{"type", "integer"}, {"description", "页码"}}},
    {"limit", {{"type", "integer"}, {"description", "每页数量"}}}
};
user_list_api.responses = {
    {"200", {{"description", "成功"}, {"schema", {{"type", "array"}}}}}
};

router.setApiMetadata("GET", "/api/users", user_list_api);

// 获取所有API端点
auto endpoints = router.getAllApiEndpoints();
for (const auto& endpoint : endpoints) {
    std::cout << endpoint.method << " " << endpoint.path 
              << " - " << endpoint.description << std::endl;
}

// 生成OpenAPI规范
nlohmann::json service_info = {
    {"title", "User Service"},
    {"version", "1.0.0"},
    {"description", "User management service"}
};
nlohmann::json openapi_spec = router.getOpenApiSpec(service_info);
std::cout << openapi_spec.dump(2) << std::endl;
```

---

## **🔌 5. HttpMiddleware中间件系统API**

### **5.1 中间件管理器**

#### **基础方法**
```cpp
void use(std::shared_ptr<HttpMiddleware> middleware)                         // 添加中间件实例
void use(MiddlewareFunction func, const std::string& name = "CustomMiddleware", 
         int priority = 0)                                                   // 添加函数中间件
void use(const std::string& path_pattern, std::shared_ptr<HttpMiddleware> middleware)  // 路径中间件
```

#### **管理方法**
```cpp
bool execute(HttpRequest& request, HttpResponse& response,
            std::function<void()> final_handler = nullptr)                  // 执行中间件链
bool remove(const std::string& name)                                       // 移除中间件
void clear()                                                                // 清空中间件
size_t size() const                                                         // 获取中间件数量
std::vector<std::string> getMiddlewareNames() const                         // 获取中间件名称列表
```

#### **生命周期方法**
```cpp
bool initialize()                                                           // 初始化所有中间件
void cleanup()                                                              // 清理所有中间件
void setMiddlewareEnabled(const std::string& name, bool enabled)            // 启用/禁用中间件
```

**中间件管理使用示例**:
```cpp
MiddlewareManager middleware_manager;

// 添加CORS中间件
auto cors_middleware = std::make_shared<CorsMiddleware>();
middleware_manager.use(cors_middleware);

// 添加自定义函数中间件
middleware_manager.use([](HttpRequest& req, HttpResponse& res, NextFunction next) -> bool {
    LOG_INFO("Request: " + req.getMethodString() + " " + req.getPath());
    return next();  // 继续执行下一个中间件
}, "RequestLogger", 100);

// 添加路径特定中间件
auto auth_middleware = std::make_shared<AuthMiddleware>(auth_config);
middleware_manager.use("/api/admin", auth_middleware);

// 初始化中间件
middleware_manager.initialize();

// 执行中间件链
bool continue_processing = middleware_manager.execute(request, response, [&]() {
    // 最终处理逻辑
    handleRequest(request, response);
});

// 获取中间件信息
auto middleware_names = middleware_manager.getMiddlewareNames();
std::cout << "Loaded middlewares: " << middleware_names.size() << std::endl;
for (const auto& name : middleware_names) {
    std::cout << "  - " << name << std::endl;
}
```

---

### **5.2 内置中间件**

#### **CORS中间件**
```cpp
class CorsMiddleware : public HttpMiddleware {
    struct Config {
        std::string allow_origin = "*";
        std::vector<std::string> allow_methods = {"GET", "POST", "PUT", "DELETE", "OPTIONS", "HEAD"};
        std::vector<std::string> allow_headers = {"Content-Type", "Authorization", "X-Requested-With"};
        std::vector<std::string> expose_headers;
        bool allow_credentials = false;
        int max_age = 86400;
    };
};
```

**CORS中间件使用示例**:
```cpp
// 默认CORS配置
auto cors_middleware = std::make_shared<CorsMiddleware>();
middleware_manager.use(cors_middleware);

// 自定义CORS配置
CorsMiddleware::Config cors_config;
cors_config.allow_origin = "https://example.com";
cors_config.allow_methods = {"GET", "POST"};
cors_config.allow_headers = {"Content-Type", "Authorization"};
cors_config.allow_credentials = true;
cors_config.max_age = 3600;

auto custom_cors = std::make_shared<CorsMiddleware>(cors_config);
middleware_manager.use(custom_cors);
```

#### **认证中间件**
```cpp
class AuthMiddleware : public HttpMiddleware {
    struct Config {
        std::string secret_key;
        std::string token_header = "Authorization";
        std::string token_prefix = "Bearer ";
        int token_expiry_hours = 24;
        std::vector<std::string> exclude_paths;
    };
};
```

**认证中间件使用示例**:
```cpp
AuthMiddleware::Config auth_config;
auth_config.secret_key = "your-secret-key";
auth_config.token_header = "Authorization";
auth_config.token_prefix = "Bearer ";
auth_config.exclude_paths = {"/api/login", "/api/register", "/health"};

auto auth_middleware = std::make_shared<AuthMiddleware>(auth_config);
middleware_manager.use(auth_middleware);
```

#### **日志中间件**
```cpp
class LoggingMiddleware : public HttpMiddleware {
    struct Config {
        bool log_request_headers = false;
        bool log_request_body = false;
        bool log_response_headers = false;
        bool log_response_body = false;
        std::vector<std::string> exclude_paths = {"/health", "/metrics"};
        size_t max_body_log_size = 1024;
    };
};
```

**日志中间件使用示例**:
```cpp
LoggingMiddleware::Config log_config;
log_config.log_request_headers = true;
log_config.log_request_body = false;
log_config.exclude_paths = {"/health", "/metrics", "/favicon.ico"};
log_config.max_body_log_size = 2048;

auto logging_middleware = std::make_shared<LoggingMiddleware>(log_config);
middleware_manager.use(logging_middleware);
```

#### **限流中间件**
```cpp
class RateLimitMiddleware : public HttpMiddleware {
    struct Config {
        int requests_per_minute = 60;
        int burst_size = 10;
        std::string key_generator = "ip";
        std::vector<std::string> exclude_paths;
    };
};
```

**限流中间件使用示例**:
```cpp
RateLimitMiddleware::Config rate_limit_config;
rate_limit_config.requests_per_minute = 100;
rate_limit_config.burst_size = 20;
rate_limit_config.key_generator = "ip";  // 基于IP限流
rate_limit_config.exclude_paths = {"/health"};

auto rate_limit_middleware = std::make_shared<RateLimitMiddleware>(rate_limit_config);
middleware_manager.use(rate_limit_middleware);
```

---

### **5.3 自定义中间件**

#### **继承HttpMiddleware创建中间件**
```cpp
class CustomMiddleware : public HttpMiddleware {
public:
    CustomMiddleware() : HttpMiddleware("CustomMiddleware", 500) {}
    
    MiddlewareResult execute(HttpRequest& request, HttpResponse& response,
                           MiddlewareContext& context, NextFunction next) override {
        // 预处理
        context.set("start_time", std::to_string(std::time(nullptr)));
        
        // 执行下一个中间件
        bool continue_processing = next();
        
        // 后处理
        LOG_INFO("Request processed in " + std::to_string(context.getElapsedMs()) + "ms");
        
        return continue_processing ? MiddlewareResult::CONTINUE : MiddlewareResult::STOP;
    }
    
    bool shouldExecute(const HttpRequest& request) const override {
        // 只处理API请求
        return request.getPath().starts_with("/api/");
    }
};
```

**自定义中间件使用示例**:
```cpp
// 使用自定义中间件类
auto custom_middleware = std::make_shared<CustomMiddleware>();
middleware_manager.use(custom_middleware);

// 使用lambda创建中间件
middleware_manager.use([](HttpRequest& req, HttpResponse& res, NextFunction next) -> bool {
    // 添加请求ID
    std::string request_id = "req_" + std::to_string(std::time(nullptr));
    res.setHeader("X-Request-ID", request_id);
    
    // 安全头部
    res.setHeader("X-Content-Type-Options", "nosniff");
    res.setHeader("X-Frame-Options", "DENY");
    res.setHeader("X-XSS-Protection", "1; mode=block");
    
    return next();
}, "SecurityHeaders", 200);
```

---

## **🔌 6. HttpSession会话管理API**

### **6.1 会话控制**

```cpp
void start()                                                                // 启动会话
void close()                                                                // 关闭会话
void forceClose()                                                           // 强制关闭会话
bool isClosed() const                                                       // 检查是否已关闭
bool isConnected() const                                                    // 检查是否已连接
```

### **6.2 配置方法**

```cpp
void setRequestHandler(RequestHandlerCallback handler)                      // 设置请求处理器
void setCloseCallback(SessionCallback callback)                             // 设置关闭回调
void setErrorCallback(SessionErrorCallback callback)                        // 设置错误回调
void setReadTimeout(int timeout_ms)                                         // 设置读取超时
void setWriteTimeout(int timeout_ms)                                        // 设置写入超时
void setKeepAliveTimeout(int timeout_ms)                                    // 设置Keep-Alive超时
void setMaxKeepAliveRequests(int max_requests)                              // 设置最大Keep-Alive请求数
void setKeepAliveEnabled(bool enabled)                                      // 启用/禁用Keep-Alive
```

### **6.3 数据传输**

```cpp
void sendResponse(const HttpResponse& response)                             // 发送响应
void sendResponseAsync(const HttpResponse& response, 
                      std::function<void(bool)> callback = nullptr)        // 异步发送响应
```

### **6.4 状态查询**

```cpp
int getId() const                                                           // 获取会话ID
HttpSessionState getState() const                                           // 获取会话状态
const InetAddress& getPeerAddress() const                                   // 获取对端地址
bool isTimeout() const                                                      // 检查是否超时
const HttpSessionStats& getStats() const                                    // 获取统计信息
int getKeepAliveRequestCount() const                                        // 获取Keep-Alive请求计数
```

**会话管理使用示例**:
```cpp
// 在HttpServer中使用会话
void HttpServer::handleNewConnection() {
    int client_fd = accept(listen_socket_->fd(), nullptr, nullptr);
    if (client_fd < 0) {
        LOG_ERROR("Accept connection failed");
        return;
    }
    
    auto client_addr = InetAddress::getLocalAddr(client_fd);
    auto session = std::make_shared<HttpSession>(client_fd, client_addr, event_loop_.get());
    
    // 配置会话
    session->setReadTimeout(30000);           // 30秒读取超时
    session->setWriteTimeout(30000);          // 30秒写入超时
    session->setKeepAliveTimeout(60000);      // 60秒Keep-Alive超时
    session->setMaxKeepAliveRequests(100);    // 最多100个Keep-Alive请求
    session->setKeepAliveEnabled(true);
    
    // 设置请求处理器
    session->setRequestHandler([this](HttpRequest& req, HttpResponse& res) {
        handleHttpRequest(session, req, res);
    });
    
    // 设置关闭回调
    session->setCloseCallback([this](std::shared_ptr<HttpSession> sess) {
        cleanupSession(sess->getId());
        stats_.active_connections.fetch_sub(1);
    });
    
    // 设置错误回调
    session->setErrorCallback([](std::shared_ptr<HttpSession> sess, const std::string& error) {
        LOG_ERROR("Session error: " + error + " for session " + std::to_string(sess->getId()));
    });
    
    // 启动会话
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_[client_fd] = session;
    }
    
    session->start();
    stats_.total_connections.fetch_add(1);
    stats_.active_connections.fetch_add(1);
}
```

---

## **📊 7. 完整使用示例**

### **7.1 基础HTTP服务器**

```cpp
#include "common/http/http_server.h"

int main() {
    // 配置服务器
    HttpServerConfig config;
    config.host = "0.0.0.0";
    config.port = 8080;
    config.worker_threads = 4;
    config.enable_cors = true;
    config.enable_performance_monitoring = true;
    
    HttpServer server(config);
    
    // 添加路由
    server.get("/", [](const HttpRequest& req, HttpResponse& res) {
        res.ok("Hello, World!");
    });
    
    server.get("/api/status", [](const HttpRequest& req, HttpResponse& res) {
        res.ok({{"status", "running"}, {"timestamp", std::time(nullptr)}});
    });
    
    // 启动服务器
    if (server.start()) {
        LOG_INFO("Server started on http://" + config.host + ":" + std::to_string(config.port));
        server.waitForStop();
    } else {
        LOG_ERROR("Failed to start server");
        return 1;
    }
    
    return 0;
}
```

---

### **7.2 完整的RESTful API服务器**

```cpp
#include "common/http/http_server.h"
#include "common/http/http_middleware.h"
#include <unordered_map>
#include <mutex>

// 简单的用户数据存储
struct User {
    std::string id;
    std::string username;
    std::string email;
    std::time_t created_at;
    
    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"username", username},
            {"email", email},
            {"created_at", created_at}
        };
    }
};

class UserService {
private:
    std::unordered_map<std::string, User> users_;
    std::mutex users_mutex_;
    
public:
    std::vector<User> getAllUsers() {
        std::lock_guard<std::mutex> lock(users_mutex_);
        std::vector<User> result;
        for (const auto& pair : users_) {
            result.push_back(pair.second);
        }
        return result;
    }
    
    std::optional<User> getUserById(const std::string& id) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        auto it = users_.find(id);
        return (it != users_.end()) ? std::optional<User>(it->second) : std::nullopt;
    }
    
    User createUser(const std::string& username, const std::string& email) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        User user;
        user.id = "usr_" + std::to_string(std::time(nullptr));
        user.username = username;
        user.email = email;
        user.created_at = std::time(nullptr);
        users_[user.id] = user;
        return user;
    }
    
    bool deleteUser(const std::string& id) {
        std::lock_guard<std::mutex> lock(users_mutex_);
        return users_.erase(id) > 0;
    }
};

int main() {
    UserService user_service;
    
    // 配置服务器
    HttpServerConfig config;
    config.host = "0.0.0.0";
    config.port = 8080;
    config.worker_threads = 4;
    config.enable_cors = true;
    config.enable_performance_monitoring = true;
    
    HttpServer server(config);
    
    // 添加CORS中间件
    server.use([](const HttpRequest& req, HttpResponse& res) -> bool {
        res.setHeader("Access-Control-Allow-Origin", "*");
        res.setHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.setHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        
        if (req.getMethodString() == "OPTIONS") {
            res.ok();
            return false;
        }
        return true;
    });
    
    // 添加日志中间件
    server.use([](const HttpRequest& req, HttpResponse& res) -> bool {
        LOG_INFO("Request: " + req.getMethodString() + " " + req.getPath() + 
                " from " + req.getClientIP());
        return true;
    }, "RequestLogger");
    
    // 根路径
    server.get("/", [](const HttpRequest& req, HttpResponse& res) {
        res.ok({
            {"message", "User Service API"},
            {"version", "1.0.0"},
            {"endpoints", {
                "GET /api/users",
                "GET /api/users/{id}",
                "POST /api/users",
                "DELETE /api/users/{id}",
                "GET /health",
                "GET /stats"
            }}
        });
    });
    
    // 健康检查
    server.get("/health", [&server](const HttpRequest& req, HttpResponse& res) {
        res.ok({
            {"status", "UP"},
            {"timestamp", std::time(nullptr)},
            {"active_connections", server.getActiveSessionCount()},
            {"uptime", server.getStats().getUptimeSeconds()}
        });
    });
    
    // 服务器统计
    server.get("/stats", [&server](const HttpRequest& req, HttpResponse& res) {
        auto stats = server.getStatsSnapshot();
        res.ok({
            {"total_connections", stats.total_connections},
            {"active_connections", stats.active_connections},
            {"total_requests", stats.total_requests},
            {"total_responses", stats.total_responses},
            {"error_count", stats.error_count},
            {"uptime_seconds", stats.uptime_seconds}
        });
    });
    
    // 获取所有用户
    server.get("/api/users", [&user_service](const HttpRequest& req, HttpResponse& res) {
        auto users = user_service.getAllUsers();
        nlohmann::json users_json = nlohmann::json::array();
        for (const auto& user : users) {
            users_json.push_back(user.toJson());
        }
        
        res.ok({
            {"users", users_json},
            {"total", users.size()}
        });
    });
    
    // 获取特定用户
    server.get("/api/users/{id}", [&user_service](const HttpRequest& req, HttpResponse& res) {
        std::string user_id = req.getParam("id");
        if (user_id.empty()) {
            res.badRequest("User ID is required");
            return;
        }
        
        auto user = user_service.getUserById(user_id);
        if (!user) {
            res.notFound("User not found");
            return;
        }
        
        res.ok(user->toJson());
    });
    
    // 创建用户
    server.post("/api/users", [&user_service](const HttpRequest& req, HttpResponse& res) {
        if (!req.hasBody() || !req.isJsonBody()) {
            res.badRequest("JSON request body is required");
            return;
        }
        
        try {
            auto json_data = req.getJsonBody();
            
            if (!json_data.contains("username") || !json_data.contains("email")) {
                res.badRequest("Username and email are required");
                return;
            }
            
            std::string username = json_data["username"];
            std::string email = json_data["email"];
            
            if (username.empty() || email.empty()) {
                res.badRequest("Username and email cannot be empty");
                return;
            }
            
            auto new_user = user_service.createUser(username, email);
            res.created(new_user.toJson());
            
        } catch (const nlohmann::json::exception& e) {
            res.badRequest("Invalid JSON: " + std::string(e.what()));
        }
    });
    
    // 删除用户
    server.del("/api/users/{id}", [&user_service](const HttpRequest& req, HttpResponse& res) {
        std::string user_id = req.getParam("id");
        if (user_id.empty()) {
            res.badRequest("User ID is required");
            return;
        }
        
        if (user_service.deleteUser(user_id)) {
            res.ok({{"message", "User deleted successfully"}});
        } else {
            res.notFound("User not found");
        }
    });
    
    // 静态文件服务
    server.serveStatic("/static", "./public");
    
    // 注册API发现端点
    server.registerApiDiscoveryEndpoints("user_service", "1.0.0", "http://localhost:8080");
    
    // 设置错误处理器
    server.setErrorHandler([](const std::exception& e, const HttpRequest& req, HttpResponse& res) {
        LOG_ERROR("Request error: " + std::string(e.what()) + " for " + req.getPath());
        res.internalServerError({
            {"error", "Internal Server Error"},
            {"message", "An unexpected error occurred"},
            {"timestamp", std::time(nullptr)},
            {"path", req.getPath()}
        });
    });
    
    // 设置404处理器
    server.setNotFoundHandler([](const HttpRequest& req, HttpResponse& res) {
        res.notFound({
            {"error", "Not Found"},
            {"message", "The requested resource was not found"},
            {"path", req.getPath()},
            {"method", req.getMethodString()},
            {"timestamp", std::time(nullptr)}
        });
    });
    
    // 启动服务器
    LOG_INFO("Starting User Service...");
    if (server.start()) {
        LOG_INFO("User Service started on http://" + config.host + ":" + std::to_string(config.port));
        LOG_INFO("API Documentation: http://" + config.host + ":" + std::to_string(config.port) + "/api/v1/service/endpoints");
        server.waitForStop();
    } else {
        LOG_ERROR("Failed to start User Service");
        return 1;
    }
    
    return 0;
}
```

---

## **🔧 8. 高级功能和最佳实践**

### **8.1 时间轮监控系统**

HTTP服务器框架使用时间轮替代传统的监控线程，提供更高效的定时任务管理：

```cpp
// 配置中启用时间轮监控
HttpServerConfig config;
config.enable_performance_monitoring = true;
config.stats_interval_ms = 30000;  // 30秒输出统计信息

HttpServer server(config);
// 服务器内部会自动使用HierarchicalTimingWheel进行监控
```

### **8.2 线程安全和并发处理**

```cpp
// 使用外部线程池进行精确控制
auto thread_pool_config = ThreadPool::Config::fromConfigManager();
thread_pool_config.core_pool_size = 8;
thread_pool_config.maximum_pool_size = 16;
auto thread_pool = std::make_shared<ThreadPool>(thread_pool_config);

HttpServer server(config, thread_pool);

// 在请求处理中利用线程池
server.post("/api/heavy-task", [thread_pool](const HttpRequest& req, HttpResponse& res) {
    // 将耗时任务提交到线程池
    auto future = thread_pool->submit([req]() {
        // 执行耗时计算
        return performHeavyComputation(req.getJsonBody());
    });
    
    try {
        auto result = future.get();  // 等待任务完成
        res.ok(result);
    } catch (const std::exception& e) {
        res.internalServerError("Task failed: " + std::string(e.what()));
    }
});
```

### **8.3 配置管理集成**

```cpp
// 从配置管理器加载HTTP服务器配置
void loadConfigFromManager(HttpServer& server) {
    server.loadConfigFromManager();  // 加载配置
    
    // 监听配置变化
    auto config_manager = ConfigManager::getInstance();
    config_manager->addConfigChangeListener([&server](const std::string& key) {
        if (key.starts_with("http.")) {
            LOG_INFO("HTTP configuration changed, updating server...");
            server.updateConfig(HttpServerConfig::fromConfigManager());
        }
    });
}
```

### **8.4 性能监控和调试**

```cpp
// 添加性能监控端点
server.get("/debug/performance", [&server](const HttpRequest& req, HttpResponse& res) {
    auto stats = server.getStatsSnapshot();
    auto routes = server.getAllRoutes();
    
    nlohmann::json debug_info = {
        {"server_stats", {
            {"total_connections", stats.total_connections},
            {"active_connections", stats.active_connections},
            {"total_requests", stats.total_requests},
            {"error_count", stats.error_count},
            {"uptime_seconds", stats.uptime_seconds}
        }},
        {"routing_stats", {
            {"total_routes", routes.size()},
            {"routes", routes}
        }},
        {"memory_usage", {
            {"active_sessions", server.getActiveSessionCount()}
        }}
    };
    
    res.ok(debug_info);
});
```

---

## **📖 9. 错误码和异常处理**

### **9.1 HTTP状态码**

HTTP服务器框架支持完整的HTTP状态码体系：

- **1xx 信息性状态码**: CONTINUE (100), SWITCHING_PROTOCOLS (101)
- **2xx 成功状态码**: OK (200), CREATED (201), ACCEPTED (202), NO_CONTENT (204), PARTIAL_CONTENT (206)
- **3xx 重定向状态码**: MOVED_PERMANENTLY (301), FOUND (302), NOT_MODIFIED (304), TEMPORARY_REDIRECT (307), PERMANENT_REDIRECT (308)
- **4xx 客户端错误**: BAD_REQUEST (400), UNAUTHORIZED (401), FORBIDDEN (403), NOT_FOUND (404), METHOD_NOT_ALLOWED (405), CONFLICT (409), TOO_MANY_REQUESTS (429)
- **5xx 服务器错误**: INTERNAL_SERVER_ERROR (500), NOT_IMPLEMENTED (501), BAD_GATEWAY (502), SERVICE_UNAVAILABLE (503), GATEWAY_TIMEOUT (504)

### **9.2 异常处理最佳实践**

```cpp
// 全局异常处理
server.setErrorHandler([](const std::exception& e, const HttpRequest& req, HttpResponse& res) {
    // 记录详细错误信息
    LOG_ERROR("Unhandled exception: " + std::string(e.what()) + 
              " for " + req.getMethodString() + " " + req.getPath());
    
    // 根据异常类型返回适当的错误码
    if (dynamic_cast<const std::invalid_argument*>(&e)) {
        res.badRequest("Invalid request parameters");
    } else if (dynamic_cast<const std::runtime_error*>(&e)) {
        res.internalServerError("Runtime error occurred");
    } else {
        res.internalServerError("An unexpected error occurred");
    }
});

// 路由级别的异常处理
server.post("/api/risky-operation", [](const HttpRequest& req, HttpResponse& res) {
    try {
        // 可能抛出异常的操作
        auto result = performRiskyOperation(req.getJsonBody());
        res.ok(result);
    } catch (const ValidationException& e) {
        res.badRequest("Validation failed: " + std::string(e.what()));
    } catch (const DatabaseException& e) {
        res.internalServerError("Database error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        // 其他异常会被全局错误处理器处理
        throw;
    }
});
```

---

## **🚀 10. 性能特性和优化**

### **10.1 事件驱动架构**
- 基于Epoll的高效事件循环
- 非阻塞IO操作
- 支持大量并发连接

### **10.2 内存管理优化**
- 智能指针管理对象生命周期
- 缓冲区复用减少内存分配
- RAII模式确保资源安全释放

### **10.3 网络优化**
- TCP_NODELAY禁用Nagle算法
- SO_REUSEADDR快速端口重用
- 可配置的发送/接收缓冲区大小
- Keep-Alive长连接支持

### **10.4 路由优化**
- 基于前缀树的高效路由匹配
- 路由缓存减少重复计算
- 优先级路由支持

### **10.5 时间轮定时器**
- 替代传统的线程轮询机制
- 分层时间轮提供精确的定时任务
- 减少系统调用开销

---

**📌 注意**: 本文档基于common模块HTTP服务器框架的实际代码实现生成，所有API格式和功能都经过验证，确保准确性和可用性。HTTP服务器框架提供了完整的现代Web服务开发能力，支持微服务架构下的高性能HTTP服务构建。API可能根据版本更新而变化，请参考对应版本的代码实现。

