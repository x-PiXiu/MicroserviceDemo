/**
 * @file http_response.h
 * @brief HTTP响应类 - 构建和管理HTTP响应数据
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - 完整的HTTP响应构建功能
 * - 支持多种响应格式（JSON、HTML、文本等）
 * - 自动Content-Length计算
 * - Cookie和头部管理
 * - 便捷的状态码设置方法
 * - 响应压缩支持
 * - 缓存控制
 *
 * 与现有模块集成:
 * - 使用Logger进行调试日志记录
 * - 集成nlohmann::json进行JSON处理
 * - 支持与网络模块的Socket数据传输
 *
 * 使用示例:
 * @code
 * HttpResponse response;
 * response.ok({{"message", "Hello World"}});
 * response.setHeader("Cache-Control", "no-cache");
 * response.setCookie("session_id", "abc123", 3600);
 * std::string response_data = response.toString();
 * @endcode
 */

#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>

#include "common/http/http_request.h"
#include "common/logger/logger.h"
#include <nlohmann/json.hpp>

namespace common {
    namespace http {

        /**
         * @brief HTTP状态码枚举
         */
        enum class HttpStatus {
            // ==================== 1xx 信息性状态码 ====================
            // 表示请求已被接收，继续处理

            CONTINUE = 100,                     // 继续：客户端应继续发送请求的剩余部分
                                               // 用于大文件上传时的确认机制

            SWITCHING_PROTOCOLS = 101,          // 切换协议：服务器同意切换到客户端请求的协议
                                               // 常用于WebSocket握手升级

            // ==================== 2xx 成功状态码 ====================
            // 表示请求已成功被服务器接收、理解并处理

            OK = 200,                          // 成功：请求已成功处理，这是最常见的成功状态码
                                               // 用于GET、POST等请求的标准成功响应

            CREATED = 201,                     // 已创建：请求成功并创建了新的资源
                                               // 常用于POST请求创建新用户、文章等资源

            ACCEPTED = 202,                    // 已接受：请求已被接受但尚未处理完成
                                               // 用于异步处理，如后台任务、批量操作

            NO_CONTENT = 204,                  // 无内容：请求成功但没有返回内容
                                               // 常用于DELETE请求或PUT更新操作

            PARTIAL_CONTENT = 206,             // 部分内容：服务器成功处理了部分GET请求
                                               // 用于断点续传、视频流等分块传输

            // ==================== 3xx 重定向状态码 ====================
            // 表示需要客户端采取进一步的操作才能完成请求

            MOVED_PERMANENTLY = 301,           // 永久重定向：资源已永久移动到新位置
                                               // 搜索引擎会更新索引，浏览器会缓存重定向

            FOUND = 302,                       // 临时重定向：资源临时移动到新位置
                                               // 原URL仍然有效，常用于临时维护页面

            NOT_MODIFIED = 304,                // 未修改：资源未修改，可使用缓存版本
                                               // 配合If-Modified-Since等条件请求头使用

            TEMPORARY_REDIRECT = 307,          // 临时重定向：与302类似但保证不改变请求方法
                                               // POST请求重定向后仍为POST

            PERMANENT_REDIRECT = 308,          // 永久重定向：与301类似但保证不改变请求方法
                                               // POST请求重定向后仍为POST

            // ==================== 4xx 客户端错误状态码 ====================
            // 表示客户端发送的请求有错误

            BAD_REQUEST = 400,                 // 错误请求：请求语法错误或参数无效
                                               // 如JSON格式错误、必需参数缺失

            UNAUTHORIZED = 401,                // 未授权：请求需要身份验证
                                               // 如未登录、Token过期、密码错误

            FORBIDDEN = 403,                   // 禁止访问：服务器理解请求但拒绝执行
                                               // 如权限不足、IP被封禁、资源被保护

            NOT_FOUND = 404,                   // 未找到：请求的资源不存在
                                               // 如URL错误、资源已删除、路由不匹配

            METHOD_NOT_ALLOWED = 405,          // 方法不允许：请求方法不被允许
                                               // 如对只读资源使用POST、DELETE等

            NOT_ACCEPTABLE = 406,              // 不可接受：服务器无法生成客户端可接受的内容
                                               // 如请求JSON但服务器只能返回XML

            REQUEST_TIMEOUT = 408,             // 请求超时：客户端请求超时
                                               // 如网络延迟、客户端处理缓慢

            CONFLICT = 409,                    // 冲突：请求与当前资源状态冲突
                                               // 如并发修改、重复创建、版本冲突

            GONE = 410,                        // 已删除：资源已被永久删除
                                               // 比404更明确，表示资源曾经存在但已删除

            LENGTH_REQUIRED = 411,             // 需要长度：请求必须包含Content-Length头
                                               // 用于POST/PUT请求体长度验证

            PAYLOAD_TOO_LARGE = 413,           // 载荷过大：请求实体超过服务器限制
                                               // 如文件上传过大、请求体超限

            URI_TOO_LONG = 414,                // URI过长：请求URI超过服务器处理能力
                                               // 如GET请求参数过多、URL过长

            UNSUPPORTED_MEDIA_TYPE = 415,      // 不支持的媒体类型：请求格式不被支持
                                               // 如上传不支持的文件格式

            RANGE_NOT_SATISFIABLE = 416,       // 范围无法满足：请求的Range头无效
                                               // 用于断点续传时范围超出文件大小

            EXPECTATION_FAILED = 417,          // 期望失败：服务器无法满足Expect头的要求
                                               // 如Expect: 100-continue无法满足

            TOO_MANY_REQUESTS = 429,           // 请求过多：客户端发送请求过于频繁
                                               // 用于API限流、防止DDoS攻击

            // ==================== 5xx 服务器错误状态码 ====================
            // 表示服务器在处理请求时发生了错误

            INTERNAL_SERVER_ERROR = 500,       // 内部服务器错误：服务器遇到意外错误
                                               // 如代码异常、数据库连接失败、空指针

            NOT_IMPLEMENTED = 501,             // 未实现：服务器不支持请求的功能
                                               // 如不支持的HTTP方法、未实现的API

            BAD_GATEWAY = 502,                 // 错误网关：网关或代理服务器收到无效响应
                                               // 如上游服务器故障、负载均衡器错误

            SERVICE_UNAVAILABLE = 503,         // 服务不可用：服务器暂时无法处理请求
                                               // 如服务器维护、过载、临时故障

            GATEWAY_TIMEOUT = 504,             // 网关超时：网关或代理服务器等待上游响应超时
                                               // 如上游服务器响应缓慢、网络延迟

            HTTP_VERSION_NOT_SUPPORTED = 505   // HTTP版本不支持：服务器不支持请求的HTTP版本
                                               // 如使用HTTP/2.0但服务器只支持HTTP/1.1
        };

        /**
         * @brief HTTP响应类
         * @details 负责构建和管理HTTP响应的所有数据
         * 
         * 核心职责:
         * - 响应构建：状态码、头部、响应体
         * - 内容管理：JSON、HTML、文本、二进制数据
         * - 头部管理：标准头部和自定义头部
         * - Cookie管理：设置和删除Cookie
         * - 缓存控制：缓存策略设置
         * - 压缩支持：Gzip压缩
         */
        class HttpResponse {
        public:
            // ==================== 类型定义 ====================
            using Headers = std::unordered_map<std::string, std::string>;

            // ==================== 构造函数和析构函数 ====================

            /**
             * @brief 默认构造函数
             * @param version HTTP版本
             */
            explicit HttpResponse(HttpVersion version = HttpVersion::HTTP_1_1);

            /**
             * @brief 析构函数
             */
            ~HttpResponse() = default;

            /**
             * @brief 拷贝构造函数
             */
            HttpResponse(const HttpResponse& other);

            /**
             * @brief 赋值操作符
             */
            HttpResponse& operator=(const HttpResponse& other);

            /**
             * @brief 移动构造函数
             */
            HttpResponse(HttpResponse&& other) noexcept;

            /**
             * @brief 移动赋值操作符
             */
            HttpResponse& operator=(HttpResponse&& other) noexcept;

            // ==================== 状态码管理 ====================

            /**
             * @brief 设置状态码
             * @param status_code 状态码
             */
            void setStatus(int status_code);

            /**
             * @brief 设置状态码和原因短语
             * @param status_code 状态码
             * @param reason_phrase 原因短语
             */
            void setStatus(int status_code, const std::string& reason_phrase);

            /**
             * @brief 设置状态码（枚举版本）
             * @param status 状态码枚举
             */
            void setStatus(HttpStatus status);

            /**
             * @brief 获取状态码
             */
            int getStatus() const { return status_code_; }

            /**
             * @brief 获取原因短语
             */
            const std::string& getReasonPhrase() const { return reason_phrase_; }

            // ==================== 头部管理 ====================

            /**
             * @brief 设置响应头
             * @param key 头部名称
             * @param value 头部值
             */
            void setHeader(const std::string& key, const std::string& value);

            /**
             * @brief 设置响应头（整数值）
             * @param key 头部名称
             * @param value 头部值
             */
            void setHeader(const std::string& key, int value);

            /**
             * @brief 添加响应头（支持多值头部）
             * @param key 头部名称
             * @param value 头部值
             * @details 与setHeader不同，addHeader不会覆盖已存在的同名头部，
             *          而是添加新的头部行。主要用于Set-Cookie等可以有多个值的头部。
             */
            void addHeader(const std::string& key, const std::string& value);

            /**
             * @brief 获取响应头
             * @param key 头部名称
             * @return 头部值，不存在返回空字符串
             */
            std::string getHeader(const std::string& key) const;

            /**
             * @brief 获取所有响应头
             */
            const Headers& getHeaders() const { return headers_; }

            /**
             * @brief 移除响应头
             * @param key 头部名称
             */
            void removeHeader(const std::string& key);

            /**
             * @brief 检查是否存在指定头部
             */
            bool hasHeader(const std::string& key) const;

            // ==================== 响应体管理 ====================

            /**
             * @brief 设置响应体
             * @param body 响应体内容
             */
            void setBody(const std::string& body);

            /**
             * @brief 设置响应体（二进制数据）
             * @param data 二进制数据
             * @param size 数据大小
             */
            void setBody(const char* data, size_t size);

            /**
             * @brief 追加响应体内容
             * @param content 要追加的内容
             */
            void appendBody(const std::string& content);

            /**
             * @brief 获取响应体
             */
            const std::string& getBody() const { return body_; }

            /**
             * @brief 清空响应体
             */
            void clearBody();

            /**
             * @brief 设置JSON响应体
             * @param json JSON对象
             */
            void setJsonBody(const nlohmann::json& json);

            /**
             * @brief 设置JSON响应体（字符串版本）
             * @param json_str JSON字符串
             */
            void setJsonBody(const std::string& json_str);

            // ==================== 便捷响应方法 ====================

            /**
             * @brief 200 OK响应
             * @param body 响应体（可选）
             */
            void ok(const std::string& body = "");

            /**
             * @brief 200 OK响应（JSON）
             * @param json JSON对象
             */
            void ok(const nlohmann::json& json);

            /**
             * @brief 201 Created响应
             * @param body 响应体（可选）
             */
            void created(const std::string& body = "");

            /**
             * @brief 201 Created响应（JSON）
             * @param json JSON对象
             */
            void created(const nlohmann::json& json);

            /**
             * @brief 204 No Content响应
             */
            void noContent();

            /**
             * @brief 错误响应
             * @param status_code 状态码
             * @param message 错误消息
             */
            void error(int status_code, const std::string& message);

            /**
             * @brief 错误响应（JSON）
             * @param status_code 状态码
             * @param json JSON对象
             */
            void error(int status_code, const nlohmann::json& json);

            /**
             * @brief 400 Bad Request响应
             * @param message 错误消息
             */
            void badRequest(const std::string& message = "Bad Request");

            /**
             * @brief 401 Unauthorized响应
             * @param message 错误消息
             */
            void unauthorized(const std::string& message = "Unauthorized");

            /**
             * @brief 403 Forbidden响应
             * @param message 错误消息
             */
            void forbidden(const std::string& message = "Forbidden");

            /**
             * @brief 404 Not Found响应
             * @param message 错误消息
             */
            void notFound(const std::string& message = "Not Found");

            /**
             * @brief 500 Internal Server Error响应
             * @param message 错误消息
             */
            void internalServerError(const std::string& message = "Internal Server Error");

            /**
             * @brief 413 Request Entity Too Large响应
             * @param message 错误消息
             */
            void requestEntityTooLarge(const std::string& message = "Request Entity Too Large");

            // ==================== 重定向方法 ====================

            /**
             * @brief 301永久重定向
             * @param location 重定向地址
             */
            void redirect(const std::string& location);

            /**
             * @brief 302临时重定向
             * @param location 重定向地址
             */
            void temporaryRedirect(const std::string& location);

            // ==================== Cookie管理 ====================

            /**
             * @brief 设置Cookie
             * @param name Cookie名称
             * @param value Cookie值
             * @param max_age 最大存活时间（秒）
             * @param path 路径
             * @param domain 域名
             * @param secure 是否仅HTTPS
             * @param http_only 是否仅HTTP访问
             */
            void setCookie(const std::string& name, const std::string& value,
                          int max_age = -1, const std::string& path = "",
                          const std::string& domain = "", bool secure = false, bool http_only = false);

            /**
             * @brief 设置安全Cookie（增强版本，支持SameSite属性）
             * @param name Cookie名称
             * @param value Cookie值
             * @param max_age 最大存活时间（秒），-1表示会话Cookie
             * @param path 路径（默认为"/"）
             * @param domain 域名
             * @param secure 是否仅HTTPS传输（默认true）
             * @param http_only 是否禁止JavaScript访问（默认true）
             * @param same_site SameSite属性："Strict", "Lax", "None", 或空字符串（默认"Lax"）
             * @details 这是setCookie的安全增强版本，默认启用安全选项，
             *          推荐用于敏感数据的Cookie设置
             */
            void setCookieSecure(const std::string& name, const std::string& value,
                               int max_age = -1, const std::string& path = "/",
                               const std::string& domain = "", bool secure = true,
                               bool http_only = true, const std::string& same_site = "Lax");

            /**
             * @brief 删除Cookie
             * @param name Cookie名称
             * @param path 路径
             * @param domain 域名
             */
            void deleteCookie(const std::string& name, const std::string& path = "", const std::string& domain = "");

            // ==================== 缓存控制 ====================

            /**
             * @brief 设置缓存控制
             * @param cache_control 缓存控制指令
             */
            void setCacheControl(const std::string& cache_control);

            /**
             * @brief 设置过期时间
             * @param expires 过期时间（秒）
             */
            void setExpires(int expires);

            /**
             * @brief 设置ETag
             * @param etag ETag值
             */
            void setETag(const std::string& etag);

            /**
             * @brief 设置Last-Modified
             * @param last_modified 最后修改时间
             */
            void setLastModified(const std::string& last_modified);

            // ==================== 序列化方法 ====================

            /**
             * @brief 转换为HTTP响应字符串
             * @return HTTP响应字符串
             */
            std::string toString() const;

            /**
             * @brief 获取响应大小
             * @return 响应字节数
             */
            size_t getSize() const;

            /**
             * @brief 重置响应对象
             */
            void reset();

            // ==================== 静态工具方法 ====================

            /**
             * @brief 获取状态码的默认原因短语
             * @param status_code 状态码
             * @return 原因短语
             */
            static std::string getDefaultReasonPhrase(int status_code);

            /**
             * @brief 获取当前HTTP时间字符串
             * @return HTTP格式的时间字符串
             */
            static std::string getCurrentHttpTime();

        private:
            // ==================== 私有成员变量 ====================
            HttpVersion version_;                   ///< HTTP版本
            int status_code_;                       ///< 状态码
            std::string reason_phrase_;             ///< 原因短语
            Headers headers_;                       ///< 响应头
            std::string body_;                      ///< 响应体

            // ==================== 私有方法 ====================

            /**
             * @brief 更新Content-Length头部
             */
            void updateContentLength();

            /**
             * @brief 设置默认头部
             */
            void setDefaultHeaders();

            /**
             * @brief HTTP版本转字符串
             */
            std::string versionToString(HttpVersion version) const;

            /**
             * @brief 转换为小写
             */
            std::string toLower(const std::string& str) const;
        };

    } // namespace http
} // namespace common

#endif // HTTP_RESPONSE_H
