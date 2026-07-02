/**
 * @file http_request.cpp
 * @brief HTTP请求类的实现
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 * 
 * 实现说明:
 * - 完整的HTTP/1.0、HTTP/1.1协议支持
 * - 高效的请求解析算法
 * - 完善的错误处理和日志记录
 * - URL解码和参数解析
 * - JSON请求体支持
 */

#include "common/http/http_request.h"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <algorithm>

namespace common {
    namespace http {

        HttpRequest::HttpRequest()
            : parsed_(false)                                        // 初始化为未解析状态
            , method_(HttpMethod::UNKNOWN)                          // 初始化为未知HTTP方法
            , version_(HttpVersion::UNKNOWN)                        // 初始化为未知HTTP版本
            , client_ip_("unknown")                                 // 初始化客户端IP为未知
            , parse_time_(std::chrono::steady_clock::now()) {       // 记录对象创建时间
            // 默认构造函数：创建一个空的HTTP请求对象
            // 所有容器（headers_, params_, cookies_等）会自动初始化为空
        }

        HttpRequest::HttpRequest(const HttpRequest& other)
            : parsed_(other.parsed_)                                // 复制解析状态
            , method_(other.method_)                                // 复制HTTP方法
            , path_(other.path_)                                    // 复制请求路径
            , query_(other.query_)                                  // 复制查询字符串
            , version_(other.version_)                              // 复制HTTP版本
            , headers_(other.headers_)                              // 复制HTTP头部映射
            , params_(other.params_)                                // 复制查询参数映射
            , cookies_(other.cookies_)                              // 复制Cookie映射
            , body_(other.body_)                                    // 复制请求体
            , client_ip_(other.client_ip_)                          // 复制客户端IP
            , parse_time_(other.parse_time_) {                      // 复制解析时间
            // 拷贝构造函数：创建另一个HttpRequest对象的完整副本
        }

        HttpRequest& HttpRequest::operator=(const HttpRequest& other) {
            // 拷贝赋值运算符：将另一个HttpRequest对象的内容复制到当前对象
            // 使用自赋值检查避免不必要的操作
            if (this != &other) {
                // 逐个复制所有成员变量
                parsed_ = other.parsed_;                            // 复制解析状态
                method_ = other.method_;                            // 复制HTTP方法
                path_ = other.path_;                                // 复制请求路径
                query_ = other.query_;                              // 复制查询字符串
                version_ = other.version_;                          // 复制HTTP版本
                headers_ = other.headers_;                          // 复制HTTP头部映射
                params_ = other.params_;                            // 复制查询参数映射
                cookies_ = other.cookies_;                          // 复制Cookie映射
                body_ = other.body_;                                // 复制请求体
                client_ip_ = other.client_ip_;                      // 复制客户端IP
                parse_time_ = other.parse_time_;                    // 复制解析时间
            }
            return *this;  // 返回当前对象的引用，支持链式赋值
        }

        HttpRequest::HttpRequest(HttpRequest&& other) noexcept
            : parsed_(other.parsed_)
            , method_(other.method_)
            , path_(std::move(other.path_))
            , query_(std::move(other.query_))
            , version_(other.version_)
            , headers_(std::move(other.headers_))
            , params_(std::move(other.params_))
            , cookies_(std::move(other.cookies_))
            , body_(std::move(other.body_))
            , client_ip_(std::move(other.client_ip_))
            , parse_time_(other.parse_time_) {
            other.reset();
        }

        HttpRequest& HttpRequest::operator=(HttpRequest&& other) noexcept {
            if (this != &other) {
                parsed_ = other.parsed_;
                method_ = other.method_;
                path_ = std::move(other.path_);
                query_ = std::move(other.query_);
                version_ = other.version_;
                headers_ = std::move(other.headers_);
                params_ = std::move(other.params_);
                cookies_ = std::move(other.cookies_);
                body_ = std::move(other.body_);
                client_ip_ = std::move(other.client_ip_);
                parse_time_ = other.parse_time_;
                other.reset();
            }
            return *this;
        }

        bool HttpRequest::parseFromRawData(const std::string& raw_data) {
            return parseFromBytes(raw_data.c_str(), raw_data.size());
        }

        bool HttpRequest::parseFromBytes(const char* data, size_t size) {
            // 验证输入参数：数据指针不能为空，大小不能为0
            if (!data || size == 0) {
                LOG_ERROR("Invalid HTTP request data");
                return false;
            }

            try {
                // 重置所有成员变量，准备解析新的HTTP请求
                reset();
                // 记录解析开始时间，用于性能统计
                parse_time_ = std::chrono::steady_clock::now();

                // 将原始字节数据转换为字符串，便于后续处理
                std::string raw_data(data, size);

                // 查找HTTP头部结束标志 "\r\n\r\n"
                // HTTP协议规定：头部和请求体之间用双CRLF分隔
                // 示例: "GET /path HTTP/1.1\r\nHost: example.com\r\n\r\nbody"
                size_t header_end = raw_data.find("\r\n\r\n");
                if (header_end == std::string::npos) {
                    LOG_ERROR("HTTP request headers incomplete");
                    return false;
                }

                // 分离HTTP头部和请求体
                // header_part: 包含请求行和所有头部字段
                // body_part: 包含请求体数据（POST/PUT等方法的数据）
                std::string header_part = raw_data.substr(0, header_end);
                std::string body_part = raw_data.substr(header_end + 4); // 跳过 "\r\n\r\n"

                // 将头部按行分割，每行一个头部字段
                // HTTP头部格式：每行以\r\n结尾
                // 第一行是请求行，后续行是头部字段
                std::vector<std::string> lines = split(header_part, "\r\n");
                if (lines.empty()) {
                    LOG_ERROR("Empty HTTP request");
                    return false;
                }

                // 解析请求行（第一行）
                // 请求行格式: "METHOD /path?query HTTP/version"
                // 示例: "GET /api/users?page=1 HTTP/1.1"
                // LOG_DEBUG("parseFromBytes() - 解析请求行: [" + lines[0] + "]");
                if (!parseRequestLine(lines[0])) {
                    LOG_ERROR("Failed to parse HTTP request line: " + lines[0]);
                    return false;
                }
                // LOG_DEBUG("parseFromBytes() - 请求行解析成功");

                // 解析HTTP头部字段（除第一行外的所有行）
                // 头部格式: "Header-Name: Header-Value"
                // 示例: "Content-Type: application/json"
                std::vector<std::string> header_lines(lines.begin() + 1, lines.end());
                if (!parseHeaders(header_lines)) {
                    LOG_ERROR("Failed to parse HTTP headers");
                    return false;
                }

                // 解析URL查询参数（如果存在）
                // 查询参数格式: "key1=value1&key2=value2"
                // 示例: "page=1&limit=10&search=test"
                if (!query_.empty()) {
                    parseQueryParams(query_);
                }

                // 解析Cookie头部（如果存在）
                // Cookie格式: "name1=value1; name2=value2; name3=value3"
                // 示例: "session_id=abc123; user_pref=dark_mode"
                if (hasHeader("cookie")) {
                    parseCookies(getHeader("cookie"));
                }

                // 处理HTTP请求体
                // 根据Content-Length头部确定请求体的实际长度
                size_t content_length = getContentLength();
                if (content_length > 0) {
                    // 验证接收到的数据长度是否符合Content-Length声明
                    if (body_part.size() >= content_length) {
                        // 截取指定长度的请求体数据
                        body_ = body_part.substr(0, content_length);
                        // LOG_DEBUG("HTTP request body parsed successfully, length: " + std::to_string(body_.size()));
                    } else {
                        // 数据不完整，但如果body_part为空且我们只是在解析头部，这是正常的
                        if (body_part.empty()) {
                            LOG_WARNING("HTTP request headers parsed, body will be set separately");
                            body_ = "";  // 暂时设置为空，稍后会被设置
                        } else {
                            // 数据不完整，可能是分片传输或网络问题
                            LOG_ERROR("HTTP request body incomplete, expected: " +
                                    std::to_string(content_length) + ", got: " + std::to_string(body_part.size()));
                            return false;
                        }
                    }
                } else {
                    // 没有Content-Length或为0，使用所有可用数据
                    body_ = body_part;
                }

                // 🔧 兼容性修复：在验证前检查并补充缺失的Host头部
                if (version_ == HttpVersion::HTTP_1_1 && !hasHeader("host")) {
                    // 自动添加默认Host头部，提高客户端兼容性
                    // 这对于某些HTTP客户端（如Postman的某些配置）或代理环境很有用
                    std::string default_host = "localhost";
                    
                    // 尝试从其他头部推断Host（如果有的话）
                    if (hasHeader("x-forwarded-host")) {
                        default_host = getHeader("x-forwarded-host");
                    } else if (hasHeader("x-original-host")) {
                        default_host = getHeader("x-original-host");
                    }
                    
                    // 将Host头部转为小写存储（因为头部名称已被统一转换为小写）
                    headers_["host"] = default_host;
                    
                    LOG_INFO("🔧 [HTTP_FIX] HTTP/1.1请求缺少Host头部，自动补充为: " + default_host);
                    // LOG_DEBUG("🔧 [HTTP_FIX] 原始请求 " + getMethodString() + " " + path_ + " 已自动添加Host头部");
                }

                // 🔧 添加请求验证：检查解析后的请求是否符合HTTP规范
                if (!validateRequest()) {
                    LOG_ERROR("HTTP request validation failed after parsing");
                    reset();
                    return false;
                }

                // 标记解析完成，设置解析状态
                parsed_ = true;
                // LOG_DEBUG("HTTP request parsed successfully: " + getMethodString() + " " + path_);
                return true;

            } catch (const std::exception& e) {
                LOG_ERROR("Exception parsing HTTP request: " + std::string(e.what()));
                reset();
                return false;
            }
        }

        void HttpRequest::reset() {
            // 重置HTTP请求对象到初始状态
            // 清空所有解析的数据，准备解析新的HTTP请求
            parsed_ = false;                                        // 重置解析状态为未解析
            method_ = HttpMethod::UNKNOWN;                          // 重置HTTP方法为未知
            path_.clear();                                          // 清空请求路径
            query_.clear();                                         // 清空查询字符串
            version_ = HttpVersion::UNKNOWN;                        // 重置HTTP版本为未知
            headers_.clear();                                       // 清空HTTP头部映射
            params_.clear();                                        // 清空查询参数映射
            cookies_.clear();                                       // 清空Cookie映射
            body_.clear();                                          // 清空请求体
            client_ip_ = "unknown";                                 // 重置客户端IP
            parse_time_ = std::chrono::steady_clock::now();         // 更新解析时间为当前时间
            // 注意：这个方法通常在解析新请求前调用，确保对象状态干净
        }

        std::string HttpRequest::getMethodString() const {
            return methodToString(method_);
        }

        std::string HttpRequest::getUrl() const {
            std::string url = path_;
            if (!query_.empty()) {
                url += "?" + query_;
            }
            return url;
        }

        std::string HttpRequest::getHeader(const std::string& name) const {
            std::string lower_name = toLower(name);
            auto it = headers_.find(lower_name);
            return (it != headers_.end()) ? it->second : "";
        }

        bool HttpRequest::hasHeader(const std::string& name) const {
            std::string lower_name = toLower(name);
            return headers_.find(lower_name) != headers_.end();
        }

        std::string HttpRequest::getContentType() const {
            return getHeader("content-type");
        }

        size_t HttpRequest::getContentLength() const {
            std::string length_str = getHeader("content-length");
            if (length_str.empty()) {
                return 0;
            }
            
            // 🔧 增强容错性：检查Content-Length值是否为纯数字
            // 移除首尾空白字符
            length_str.erase(0, length_str.find_first_not_of(" \t"));
            length_str.erase(length_str.find_last_not_of(" \t") + 1);
            
            // 检查是否包含非数字字符（除了开头的可选空格）
            for (char c : length_str) {
                if (!std::isdigit(c)) {
                    LOG_WARNING("Invalid Content-Length value: '" + length_str + "' - contains non-digit characters");
                    return 0;  // 返回0表示无Content-Length
                }
            }
            
            try {
                size_t value = std::stoull(length_str);
                // 🔧 添加合理性检查：Content-Length不应该过大
                const size_t MAX_CONTENT_LENGTH = 100 * 1024 * 1024; // 100MB
                if (value > MAX_CONTENT_LENGTH) {
                    LOG_WARNING("Content-Length too large: " + std::to_string(value) + " bytes (max: " + std::to_string(MAX_CONTENT_LENGTH) + ")");
                    return 0;
                }
                return value;
            } catch (const std::exception& e) {
                LOG_WARNING("Failed to parse Content-Length: '" + length_str + "' - " + std::string(e.what()));
                return 0;
            }
        }

        std::string HttpRequest::getUserAgent() const {
            return getHeader("user-agent");
        }

        std::string HttpRequest::getHost() const {
            return getHeader("host");
        }

        std::string HttpRequest::getParam(const std::string& name, const std::string& default_value) const {
            auto it = params_.find(name);
            return (it != params_.end()) ? it->second : default_value;
        }

        bool HttpRequest::hasParam(const std::string& name) const {
            return params_.find(name) != params_.end();
        }

        bool HttpRequest::isJsonBody() const {
            // 检查Content-Type头部是否为JSON格式
            // application/json 是JSON数据的标准MIME类型
            // 也可能包含字符集信息，如: "application/json; charset=utf-8"
            std::string content_type = getContentType();
            return content_type.find("application/json") != std::string::npos;
        }

        nlohmann::json HttpRequest::getJsonBody() const {
            // 首先验证请求体是否为JSON格式
            // 只有Content-Type为application/json的请求才能解析JSON数据
            if (!isJsonBody()) {
                throw std::runtime_error("Request body is not JSON");
            }

            // 如果请求体为空，返回空的JSON对象
            // 这是合法的JSON，避免解析错误
            if (body_.empty()) {
                return nlohmann::json::object(); // 返回 {}
            }

            try {
                // 使用nlohmann::json库解析JSON字符串
                // 支持完整的JSON格式：对象、数组、基本类型等
                // 示例: {"name": "john", "age": 30, "active": true}
                return nlohmann::json::parse(body_);
            } catch (const nlohmann::json::parse_error& e) {
                // JSON解析失败，记录错误并抛出异常
                // 常见错误：语法错误、编码问题、格式不正确等
                LOG_ERROR("Failed to parse JSON body: " + std::string(e.what()));
                throw std::runtime_error("Invalid JSON format");
            }
        }

        bool HttpRequest::isFormBody() const {
            // 检查Content-Type头部是否为表单编码类型
            // application/x-www-form-urlencoded 是HTML表单的标准编码格式
            // 表单数据格式: "key1=value1&key2=value2&key3=value3"
            std::string content_type = getContentType();
            return content_type.find("application/x-www-form-urlencoded") != std::string::npos;
        }

        HttpRequest::Parameters HttpRequest::getFormData() {
            // 检查请求体是否为表单格式
            // 只有Content-Type为application/x-www-form-urlencoded的请求才能解析表单数据
            if (!isFormBody()) {
                return Parameters{}; // 返回空的参数映射
            }

            // 创建用于存储表单数据的容器
            Parameters form_data;

            // 解析请求体中的表单参数
            // 表单数据格式: "username=john&password=secret&remember=true"
            // 解析后: {"username": "john", "password": "secret", "remember": "true"}
            parseFormParams(body_, form_data);

            return form_data;
        }

        std::string HttpRequest::getCookie(const std::string& name, const std::string& default_value) const {
            // 在Cookie映射中查找指定名称的Cookie
            // Cookie在HTTP头部中格式: "Cookie: name1=value1; name2=value2"
            // 解析后存储在cookies_映射中: {"name1": "value1", "name2": "value2"}
            auto it = cookies_.find(name);

            // 如果找到Cookie则返回其值，否则返回默认值
            return (it != cookies_.end()) ? it->second : default_value;
        }

        bool HttpRequest::hasCookie(const std::string& name) const {
            // 检查是否存在指定名称的Cookie
            // 返回true表示Cookie存在，false表示不存在
            return cookies_.find(name) != cookies_.end();
        }

        bool HttpRequest::isKeepAlive() const {
            std::string connection = getHeader("connection");
            if (version_ == HttpVersion::HTTP_1_1) {
                // HTTP/1.1默认keep-alive，除非明确指定close
                return toLower(connection) != "close";
            } else {
                // HTTP/1.0默认close，除非明确指定keep-alive
                return toLower(connection) == "keep-alive";
            }
        }

        bool HttpRequest::isAjax() const {
            std::string requested_with = getHeader("x-requested-with");
            return toLower(requested_with) == "xmlhttprequest";
        }

        bool HttpRequest::isSecure() const {
            // 检查是否为HTTPS连接
            // 在实际实现中，这个信息应该从连接层获取
            std::string forwarded_proto = getHeader("x-forwarded-proto");
            return toLower(forwarded_proto) == "https";
        }

        // ==================== 静态方法实现 ====================

        std::string HttpRequest::methodToString(HttpMethod method) {
            switch (method) {
                case HttpMethod::GET:     return "GET";
                case HttpMethod::POST:    return "POST";
                case HttpMethod::PUT:     return "PUT";
                case HttpMethod::DELETE:  return "DELETE";
                case HttpMethod::PATCH:   return "PATCH";
                case HttpMethod::HEAD:    return "HEAD";
                case HttpMethod::OPTIONS: return "OPTIONS";
                case HttpMethod::TRACE:   return "TRACE";
                case HttpMethod::CONNECT: return "CONNECT";
                default:                  return "UNKNOWN";
            }
        }

        HttpMethod HttpRequest::stringToMethod(const std::string& method_str) {
            std::string upper_method = method_str;
            std::transform(upper_method.begin(), upper_method.end(), upper_method.begin(), ::toupper);
            
            if (upper_method == "GET")     return HttpMethod::GET;
            if (upper_method == "POST")    return HttpMethod::POST;
            if (upper_method == "PUT")     return HttpMethod::PUT;
            if (upper_method == "DELETE")  return HttpMethod::DELETE;
            if (upper_method == "PATCH")   return HttpMethod::PATCH;
            if (upper_method == "HEAD")    return HttpMethod::HEAD;
            if (upper_method == "OPTIONS") return HttpMethod::OPTIONS;
            if (upper_method == "TRACE")   return HttpMethod::TRACE;
            if (upper_method == "CONNECT") return HttpMethod::CONNECT;
            
            return HttpMethod::UNKNOWN;
        }

        std::string HttpRequest::versionToString(HttpVersion version) {
            switch (version) {
                case HttpVersion::HTTP_1_0: return "HTTP/1.0";
                case HttpVersion::HTTP_1_1: return "HTTP/1.1";
                case HttpVersion::HTTP_2_0: return "HTTP/2.0";
                default:                     return "HTTP/1.1";
            }
        }

        HttpVersion HttpRequest::stringToVersion(const std::string& version_str) {
            if (version_str == "HTTP/1.0") return HttpVersion::HTTP_1_0;
            if (version_str == "HTTP/1.1") return HttpVersion::HTTP_1_1;
            if (version_str == "HTTP/2.0") return HttpVersion::HTTP_2_0;
            return HttpVersion::UNKNOWN;
        }

        std::string HttpRequest::urlDecode(const std::string& encoded) {
            std::string decoded;
            decoded.reserve(encoded.length());
            
            for (size_t i = 0; i < encoded.length(); ++i) {
                if (encoded[i] == '%' && i + 2 < encoded.length()) {
                    // 解码百分号编码
                    std::string hex = encoded.substr(i + 1, 2);
                    try {
                        int value = std::stoi(hex, nullptr, 16);
                        decoded += static_cast<char>(value);
                        i += 2;
                    } catch (const std::exception&) {
                        decoded += encoded[i]; // 无效编码，保持原样
                    }
                } else if (encoded[i] == '+') {
                    decoded += ' '; // 加号转空格
                } else {
                    decoded += encoded[i];
                }
            }
            
            return decoded;
        }

        std::string HttpRequest::urlEncode(const std::string& decoded) {
            std::ostringstream encoded;
            encoded << std::hex << std::uppercase;
            
            for (char c : decoded) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    encoded << c;
                } else {
                    encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<unsigned char>(c);
                }
            }
            
            return encoded.str();
        }

        // ==================== 私有方法实现 ====================

        bool HttpRequest::parseRequestLine(const std::string& request_line) {
            // 解析HTTP请求行，格式: "METHOD /path?query HTTP/version"
            // 示例: "GET /api/users?page=1&limit=10 HTTP/1.1"
            // 按空格分割，应该得到3个部分：方法、URL、版本
            // LOG_DEBUG("parseRequestLine() - 输入: [" + request_line + "]");
            std::vector<std::string> parts = split(request_line, " ");
            // LOG_DEBUG("parseRequestLine() - 分割后部分数量: " + std::to_string(parts.size()));
            // for (size_t i = 0; i < parts.size(); ++i) {
            //     LOG_DEBUG("parseRequestLine() - 部分[" + std::to_string(i) + "]: [" + parts[i] + "]");
            // }

            if (parts.size() != 3) {
                LOG_ERROR("Invalid request line format: " + request_line + " (expected 3 parts, got " + std::to_string(parts.size()) + ")");
                return false;
            }

            // 解析HTTP方法（第一部分）
            // 支持的方法: GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH等
            // 将字符串转换为HttpMethod枚举值
            // LOG_DEBUG("parseRequestLine() - 解析HTTP方法: [" + parts[0] + "]");
            method_ = stringToMethod(parts[0]);
            if (method_ == HttpMethod::UNKNOWN) {
                LOG_ERROR("Unknown HTTP method: " + parts[0]);
                return false;
            }
            // LOG_DEBUG("parseRequestLine() - HTTP方法解析成功");

            // 解析URL和查询参数（第二部分）
            // URL可能包含查询参数，格式: "/path?param1=value1&param2=value2"
            std::string url = parts[1];
            size_t query_pos = url.find('?');
            if (query_pos != std::string::npos) {
                // URL包含查询参数，分离路径和查询字符串
                path_ = url.substr(0, query_pos);           // 路径部分: "/path"
                query_ = url.substr(query_pos + 1);         // 查询部分: "param1=value1&param2=value2"
            } else {
                // URL不包含查询参数，整个URL就是路径
                path_ = url;
                query_.clear();
            }

            // 对URL路径进行解码
            // 处理URL编码字符，如 %20 -> 空格, %2F -> /
            path_ = urlDecode(path_);

            // 解析HTTP版本（第三部分）
            // 支持的版本: HTTP/1.0, HTTP/1.1, HTTP/2.0等
            // 将字符串转换为HttpVersion枚举值
            // LOG_DEBUG("parseRequestLine() - 解析HTTP版本: [" + parts[2] + "]");
            version_ = stringToVersion(parts[2]);
            if (version_ == HttpVersion::UNKNOWN) {
                LOG_ERROR("Unknown HTTP version: " + parts[2]);
                return false;
            }
            // LOG_DEBUG("parseRequestLine() - HTTP版本解析成功");

            return true;
        }

        bool HttpRequest::parseHeaders(const std::vector<std::string>& header_lines) {
            // 解析HTTP头部字段
            // 每个头部字段格式: "Header-Name: Header-Value"
            // 示例: "Content-Type: application/json", "Authorization: Bearer token123"
            for (const auto& line : header_lines) {
                // 跳过空行
                if (line.empty()) {
                    continue;
                }

                // 查找冒号分隔符，分离头部名称和值
                size_t colon_pos = line.find(':');
                if (colon_pos == std::string::npos) {
                    // 无效的头部格式，记录警告但继续处理其他头部
                    LOG_WARNING("Invalid header line (missing colon): " + line);
                    continue;
                }

                // 提取头部名称和值，并去除首尾空白字符
                std::string name = trim(line.substr(0, colon_pos));           // 头部名称
                std::string value = trim(line.substr(colon_pos + 1));         // 头部值

                // 只处理非空的头部名称
                if (!name.empty()) {
                    // 🔧 增强验证：检查常见的头部格式错误
                    std::string lower_name = toLower(name);
                    
                    // 检测常见的头部名称拼写错误或格式错误
                    if (lower_name == "content-length") {
                        // 验证Content-Length值格式
                        std::string trimmed_value = value;
                        trimmed_value.erase(0, trimmed_value.find_first_not_of(" \t"));
                        trimmed_value.erase(trimmed_value.find_last_not_of(" \t") + 1);
                        
                        // 检查是否包含非数字字符
                        bool is_valid_number = !trimmed_value.empty();
                        for (char c : trimmed_value) {
                            if (!std::isdigit(c)) {
                                is_valid_number = false;
                                break;
                            }
                        }
                        
                        if (!is_valid_number) {
                            LOG_WARNING("Invalid Content-Length header value: '" + value + 
                                       "' (expected numeric value) - skipping this header");
                            continue;  // 跳过无效的Content-Length头部
                        }
                    }
                    
                    // 将头部名称转换为小写存储，便于后续查找
                    // HTTP头部名称不区分大小写，统一用小写存储
                    headers_[lower_name] = value;

                    // LOG_DEBUG("Parsed header: " + lower_name + " = " + value);
                } else {
                    LOG_WARNING("Empty header name in line: " + line);
                }
            }

            return true;
        }

        void HttpRequest::parseQueryParams(const std::string& query_string) {
            // 解析URL查询参数
            // 查询字符串格式: "key1=value1&key2=value2&key3=value3"
            // 示例: "page=1&limit=10&search=hello%20world&active"
            if (query_string.empty()) {
                return;
            }

            // 按 & 符号分割查询字符串，得到键值对数组
            std::vector<std::string> pairs = split(query_string, "&");
            for (const auto& pair : pairs) {
                // 查找等号，分离键和值
                size_t eq_pos = pair.find('=');
                if (eq_pos != std::string::npos) {
                    // 包含等号的键值对: "key=value"
                    std::string key = urlDecode(trim(pair.substr(0, eq_pos)));     // 键名
                    std::string value = urlDecode(trim(pair.substr(eq_pos + 1)));  // 键值
                    if (!key.empty()) {
                        // 存储到参数映射中，键名作为索引
                        params_[key] = value;
                    }
                } else {
                    // 没有等号的参数，视为标志参数: "active", "debug"
                    // 这种参数通常表示布尔值，存在即为true
                    std::string key = urlDecode(trim(pair));
                    if (!key.empty()) {
                        // 标志参数的值设为空字符串
                        params_[key] = "";
                    }
                }
            }
        }

        void HttpRequest::parseFormParams(const std::string& form_string, Parameters& form_data) {
            // 解析HTML表单数据
            // 表单数据格式与查询参数相同: "key1=value1&key2=value2"
            // 但数据来源是HTTP请求体，而不是URL查询字符串
            // 示例: "username=john&password=secret&remember=on"
            if (form_string.empty()) {
                return;
            }

            // 按 & 符号分割表单字符串，得到键值对数组
            std::vector<std::string> pairs = split(form_string, "&");
            for (const auto& pair : pairs) {
                // 查找等号，分离键和值
                size_t eq_pos = pair.find('=');
                if (eq_pos != std::string::npos) {
                    // 包含等号的键值对: "key=value"
                    std::string key = urlDecode(trim(pair.substr(0, eq_pos)));     // 表单字段名
                    std::string value = urlDecode(trim(pair.substr(eq_pos + 1)));  // 表单字段值
                    if (!key.empty()) {
                        // 存储到传入的form_data参数中
                        // 注意：这里不是存储到成员变量params_，而是输出参数
                        form_data[key] = value;
                    }
                } else {
                    // 没有等号的参数，通常是复选框未选中或特殊标志
                    std::string key = urlDecode(trim(pair));
                    if (!key.empty()) {
                        // 标志参数的值设为空字符串
                        form_data[key] = "";
                    }
                }
            }
        }

        void HttpRequest::parseCookies(const std::string& cookie_header) {
            // 解析HTTP Cookie头部
            // Cookie头部格式: "Cookie: name1=value1; name2=value2; name3=value3"
            // 注意：Cookie使用分号(;)分隔，而不是&符号
            // 示例: "session_id=abc123; user_pref=dark_mode; lang=en"
            if (cookie_header.empty()) {
                return;
            }

            // 按分号分割Cookie字符串，得到各个Cookie键值对
            std::vector<std::string> pairs = split(cookie_header, ";");
            for (const auto& pair : pairs) {
                // 查找等号，分离Cookie名称和值
                size_t eq_pos = pair.find('=');
                if (eq_pos != std::string::npos) {
                    // 提取Cookie名称和值，并去除首尾空白字符
                    std::string name = trim(pair.substr(0, eq_pos));      // Cookie名称
                    std::string value = trim(pair.substr(eq_pos + 1));    // Cookie值
                    if (!name.empty()) {
                        // 存储到Cookie映射中
                        // 注意：Cookie名称区分大小写，不像HTTP头部
                        cookies_[name] = value;
                    }
                }
                // 注意：Cookie必须有值，不支持无值的标志Cookie
            }
        }

        std::vector<std::string> HttpRequest::split(const std::string& str, const std::string& delimiter) const {
            // 字符串分割工具方法
            // 将输入字符串按指定分隔符分割成字符串数组
            // 示例: split("a,b,c", ",") -> ["a", "b", "c"]
            std::vector<std::string> result;
            size_t start = 0;
            size_t end = str.find(delimiter);

            // 循环查找分隔符，提取子字符串
            while (end != std::string::npos) {
                // 提取从start到end之间的子字符串
                result.push_back(str.substr(start, end - start));
                // 更新起始位置，跳过分隔符
                start = end + delimiter.length();
                // 查找下一个分隔符
                end = str.find(delimiter, start);
            }

            // 添加最后一个子字符串（分隔符后的部分）
            result.push_back(str.substr(start));
            return result;
        }

        std::string HttpRequest::trim(const std::string& str) const {
            // 字符串去空白工具方法
            // 移除字符串首尾的空白字符（空格、制表符、换行符等）
            // 示例: trim("  hello world  ") -> "hello world"

            // 查找第一个非空白字符的位置
            size_t start = str.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) {
                // 字符串全是空白字符，返回空字符串
                return "";
            }

            // 查找最后一个非空白字符的位置
            size_t end = str.find_last_not_of(" \t\r\n");

            // 提取去除首尾空白后的子字符串
            return str.substr(start, end - start + 1);
        }

        std::string HttpRequest::toLower(const std::string& str) const {
            // 字符串转小写工具方法
            // 将输入字符串的所有字符转换为小写
            // 主要用于HTTP头部名称的标准化处理
            // 示例: toLower("Content-Type") -> "content-type"
            std::string result = str;

            // 使用标准库函数将每个字符转换为小写
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);

            return result;
        }

        // ==================== 新增方法实现 ====================

        std::string HttpRequest::getClientIP() const {
            // 获取客户端IP地址
            // ⭐ 优先级顺序：X-Client-IP > X-Forwarded-For > X-Real-IP > 直连IP
            
            // 1. ⭐ 优先读取X-Client-IP（Nginx配置的真实客户端IP）
            if (hasHeader("X-Client-IP")) {
                std::string client_ip = getHeader("X-Client-IP");
                // 去除首尾空格
                size_t start = client_ip.find_first_not_of(" \t");
                size_t end = client_ip.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    client_ip = client_ip.substr(start, end - start + 1);
                }
                if (!client_ip.empty()) {
                    return client_ip;
                }
            }
            
            // 2. 尝试从X-Forwarded-For头部获取（标准代理服务器场景）
            if (hasHeader("X-Forwarded-For")) {
                std::string forwarded = getHeader("X-Forwarded-For");
                // X-Forwarded-For可能包含多个IP，取第一个（真实客户端IP）
                size_t comma_pos = forwarded.find(',');
                if (comma_pos != std::string::npos) {
                    forwarded = forwarded.substr(0, comma_pos);
                }
                // 去除首尾空格
                size_t start = forwarded.find_first_not_of(" \t");
                size_t end = forwarded.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    forwarded = forwarded.substr(start, end - start + 1);
                }
                if (!forwarded.empty()) {
                    return forwarded;
                }
            }

            // 3. 尝试从X-Real-IP头部获取
            if (hasHeader("X-Real-IP")) {
                std::string real_ip = getHeader("X-Real-IP");
                // 去除首尾空格
                size_t start = real_ip.find_first_not_of(" \t");
                size_t end = real_ip.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    real_ip = real_ip.substr(start, end - start + 1);
                }
                if (!real_ip.empty()) {
                    return real_ip;
                }
            }

            // 4. 返回存储的客户端IP（直连场景，通常在连接建立时设置）
            return client_ip_;
        }


        std::string HttpRequest::getQueryParam(const std::string& name, const std::string& default_value) const {
            // 获取查询参数（URL参数的别名）
            return getParam(name, default_value);
        }

        void HttpRequest::setClientIP(const std::string& ip) {
            // 设置客户端IP地址（通常在连接建立时调用）
            client_ip_ = ip;
        }

        void HttpRequest::setRouteParams(const Parameters& route_params) {
            // 设置路径参数（由路由器解析后设置）
            // 将路径参数合并到现有的参数映射中
            // 路径参数优先级高于查询参数（如果有同名参数）
            for (const auto& param : route_params) {
                params_[param.first] = param.second;
            }
        }

        bool HttpRequest::validateRequest() const {
            // 🔧 验证HTTP请求的完整性和合规性
            
            // 1. 验证HTTP方法
            if (method_ == HttpMethod::UNKNOWN) {
                LOG_WARNING("Request validation failed: Unknown HTTP method");
                return false;
            }
            
            // 2. 验证HTTP版本
            if (version_ == HttpVersion::UNKNOWN) {
                LOG_WARNING("Request validation failed: Unknown HTTP version");
                return false;
            }
            
            // 3. 验证请求路径
            if (path_.empty() || path_[0] != '/') {
                LOG_WARNING("Request validation failed: Invalid path - '" + path_ + "'");
                return false;
            }
            
            // 4. 验证Host头部（HTTP/1.1必须）- 🔧 修复：现在已在解析阶段自动补充
            if (version_ == HttpVersion::HTTP_1_1 && !hasHeader("host")) {
                LOG_WARNING("Request validation failed: HTTP/1.1 requests must include Host header");
                return false;
            }
            
            // 5. 验证Content-Length和请求体的一致性
            if (hasHeader("content-length")) {
                size_t declared_length = getContentLength();
                size_t actual_length = body_.size();
                
                // 对于有请求体的方法，检查长度一致性
                if (method_ == HttpMethod::POST || method_ == HttpMethod::PUT || method_ == HttpMethod::PATCH) {
                    if (declared_length != actual_length) {
                        LOG_WARNING("Request validation failed: Content-Length mismatch - declared: " + 
                                   std::to_string(declared_length) + ", actual: " + std::to_string(actual_length));
                        // 对于轻微的差异给予容忍，但记录警告
                        const size_t TOLERANCE = 10; // 容忍10字节的差异
                        if (abs(static_cast<long>(declared_length) - static_cast<long>(actual_length)) > TOLERANCE) {
                            return false;
                        }
                    }
                }
                
                // 对于GET、HEAD等方法，如果有Content-Length但值不为0，给出警告
                if ((method_ == HttpMethod::GET || method_ == HttpMethod::HEAD || method_ == HttpMethod::DELETE) 
                    && declared_length > 0) {
                    LOG_WARNING("Request validation warning: " + getMethodString() + 
                               " request should not have Content-Length > 0 (found: " + std::to_string(declared_length) + ")");
                    // 这里不返回false，因为这是警告级别的问题
                }
            }
            
            // 🔧 新增：验证Transfer-Encoding与Content-Length互斥性
            bool has_content_length = hasHeader("content-length");
            bool has_transfer_encoding = hasHeader("transfer-encoding");
            
            if (has_content_length && has_transfer_encoding) {
                LOG_WARNING("Request validation failed: Transfer-Encoding and Content-Length headers cannot both be present (RFC 7230)");
                return false;
            }
            
            // 验证Transfer-Encoding值的有效性
            if (has_transfer_encoding) {
                std::string te_value = getHeader("transfer-encoding");
                std::transform(te_value.begin(), te_value.end(), te_value.begin(), ::tolower);
                
                // 目前只支持chunked编码
                if (te_value.find("chunked") == std::string::npos) {
                    LOG_WARNING("Request validation warning: Unsupported Transfer-Encoding: " + te_value + " (only 'chunked' is supported)");
                    // 这里不返回false，而是记录警告，因为可能有其他编码
                }
            }
            
            // 验证chunked编码与HTTP版本的兼容性
            if (has_transfer_encoding && version_ == HttpVersion::HTTP_1_0) {
                LOG_WARNING("Request validation warning: Transfer-Encoding is not supported in HTTP/1.0");
                // HTTP/1.0不支持chunked，但我们可以尝试处理
            }
            
            // 6. 验证头部格式
            for (const auto& [name, value] : headers_) {
                // 检查头部名称是否包含非法字符
                for (char c : name) {
                    if (!std::isalnum(c) && c != '-' && c != '_') {
                        LOG_WARNING("Request validation failed: Invalid character in header name '" + name + "'");
                        return false;
                    }
                }
                
                // 检查头部值是否过长
                if (value.size() > 8192) { // 8KB限制
                    LOG_WARNING("Request validation failed: Header value too long for '" + name + "' (" + 
                               std::to_string(value.size()) + " bytes)");
                    return false;
                }
            }
            
            // 7. 验证URL路径长度
            if (path_.size() > 2048) { // 2KB限制
                LOG_WARNING("Request validation failed: Path too long (" + std::to_string(path_.size()) + " bytes)");
                return false;
            }
            
            // 8. 验证查询字符串长度
            if (query_.size() > 4096) { // 4KB限制
                LOG_WARNING("Request validation failed: Query string too long (" + std::to_string(query_.size()) + " bytes)");
                return false;
            }
            
            // LOG_DEBUG("Request validation passed for " + getMethodString() + " " + path_);
            return true;
        }

    } // namespace http
} // namespace common
