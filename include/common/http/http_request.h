/**
 * @file http_request.h
 * @brief HTTP请求类 - 解析和管理HTTP请求数据
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - 完整的HTTP/1.0、HTTP/1.1协议支持
 * - 高效的请求解析算法
 * - URL参数和查询字符串解析
 * - JSON请求体支持
 * - 文件上传处理
 * - Cookie解析
 * - 多种编码格式支持
 *
 * 与现有模块集成:
 * - 使用Logger进行调试日志记录
 * - 集成nlohmann::json进行JSON处理
 * - 支持与网络模块的Socket数据交互
 *
 * 使用示例:
 * @code
 * HttpRequest request;
 * if (request.parseFromRawData(raw_http_data)) {
 *     std::string method = request.getMethodString();
 *     std::string path = request.getPath();
 *     auto json_body = request.getJsonBody();
 *     std::string param = request.getParam("user_id");
 * }
 * @endcode
 */

#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <chrono>
#include <unordered_map>

#include "common/logger/logger.h"
#include <nlohmann/json.hpp>

namespace common {
    namespace http {

        /**
         * @brief HTTP请求方法枚举
         */
        enum class HttpMethod {
            GET,
            POST,
            PUT,
            DELETE,
            PATCH,
            HEAD,
            OPTIONS,
            TRACE,
            CONNECT,
            UNKNOWN
        };

        /**
         * @brief HTTP版本枚举
         */
        enum class HttpVersion {
            HTTP_1_0,
            HTTP_1_1,
            HTTP_2_0,
            UNKNOWN
        };

        /**
         * @brief HTTP请求类
         * @details 负责解析和管理HTTP请求的所有数据
         * 
         * 核心职责:
         * - HTTP协议解析：解析请求行、头部、请求体
         * - 参数管理：URL参数、查询参数、表单参数
         * - 内容处理：JSON、表单、文件上传
         * - 编码处理：URL编码/解码、字符集转换
         * - 验证功能：请求格式验证、大小限制检查
         */
        class HttpRequest {
        public:
            // ==================== 类型定义 ====================
            using Headers = std::unordered_map<std::string, std::string>;
            using Parameters = std::unordered_map<std::string, std::string>;
            using Cookies = std::unordered_map<std::string, std::string>;

            // ==================== 构造函数和析构函数 ====================

            /**
             * @brief 默认构造函数
             */
            HttpRequest();

            /**
             * @brief 析构函数
             */
            ~HttpRequest() = default;

            /**
             * @brief 拷贝构造函数
             */
            HttpRequest(const HttpRequest& other);

            /**
             * @brief 赋值操作符
             */
            HttpRequest& operator=(const HttpRequest& other);

            /**
             * @brief 移动构造函数
             */
            HttpRequest(HttpRequest&& other) noexcept;

            /**
             * @brief 移动赋值操作符
             */
            HttpRequest& operator=(HttpRequest&& other) noexcept;

            // ==================== 解析方法 ====================

            /**
             * @brief 从原始HTTP数据解析请求
             * @param raw_data 原始HTTP请求数据
             * @return 解析成功返回true
             */
            bool parseFromRawData(const std::string& raw_data);

            /**
             * @brief 从字节流解析请求
             * @param data 字节数据
             * @param size 数据大小
             * @return 解析成功返回true
             */
            bool parseFromBytes(const char* data, size_t size);

            /**
             * @brief 检查请求是否已完整解析
             * @return 解析完成返回true
             */
            bool isParsed() const { return parsed_; }

            /**
             * @brief 重置请求对象
             */
            void reset();

            // ==================== 基本信息访问 ====================

            /**
             * @brief 获取HTTP方法
             */
            HttpMethod getMethod() const { return method_; }

            /**
             * @brief 获取HTTP方法字符串
             */
            std::string getMethodString() const;

            /**
             * @brief 获取请求路径
             */
            const std::string& getPath() const { return path_; }

            /**
             * @brief 获取查询字符串
             */
            const std::string& getQuery() const { return query_; }

            /**
             * @brief 获取查询字符串（别名方法）
             */
            const std::string& getQueryString() const { return query_; }

            /**
             * @brief 获取HTTP版本
             */
            // HttpVersion getVersion() const { return version_; }
            std::string getVersion() const { return versionToString(version_); }

            /**
             * @brief 获取完整URL
             */
            std::string getUrl() const;

            // ==================== 头部管理 ====================

            /**
             * @brief 获取请求头
             * @param name 头部名称（不区分大小写）
             * @return 头部值，不存在返回空字符串
             */
            std::string getHeader(const std::string& name) const;

            /**
             * @brief 获取所有请求头
             */
            const Headers& getHeaders() const { return headers_; }

            /**
             * @brief 检查是否存在指定头部
             */
            bool hasHeader(const std::string& name) const;

            /**
             * @brief 获取Content-Type
             */
            std::string getContentType() const;

            /**
             * @brief 获取Content-Length
             */
            size_t getContentLength() const;

            /**
             * @brief 获取User-Agent
             */
            std::string getUserAgent() const;

            /**
             * @brief 获取Host
             */
            std::string getHost() const;

            /**
             * @brief 获取客户端IP地址
             */
            std::string getClientIP() const;

            /**
             * @brief 设置客户端IP地址
             * @param ip 客户端IP地址
             */
            void setClientIP(const std::string& ip);

            /**
             * @brief 设置路径参数（由路由器解析后设置）
             * @param route_params 路径参数映射
             */
            void setRouteParams(const Parameters& route_params);

            // ==================== 参数管理 ====================

            /**
             * @brief 获取URL参数
             * @param name 参数名
             * @param default_value 默认值
             * @return 参数值
             */
            std::string getParam(const std::string& name, const std::string& default_value = "") const;

            /**
             * @brief 获取所有URL参数
             */
            const Parameters& getParams() const { return params_; }

            /**
             * @brief 检查是否存在指定参数
             */
            bool hasParam(const std::string& name) const;

            /**
             * @brief 获取查询参数（URL参数的别名）
             * @param name 参数名
             * @param default_value 默认值
             * @return 参数值
             */
            std::string getQueryParam(const std::string& name, const std::string& default_value = "") const;

            // ==================== 请求体处理 ====================

            /**
             * @brief 获取请求体
             */
            const std::string& getBody() const { return body_; }

            /**
             * @brief 设置请求体
             */
            void setBody(const std::string& body) { body_ = body; }

            /**
             * @brief 检查是否有请求体
             */
            bool hasBody() const { return !body_.empty(); }

            /**
             * @brief 检查是否为JSON请求体
             */
            bool isJsonBody() const;

            /**
             * @brief 获取JSON请求体
             * @return JSON对象，解析失败抛出异常
             */
            nlohmann::json getJsonBody() const;

            /**
             * @brief 检查是否为表单请求体
             */
            bool isFormBody() const;

            /**
             * @brief 获取表单数据
             */
            Parameters getFormData() ;

            // ==================== Cookie管理 ====================

            /**
             * @brief 获取Cookie值
             * @param name Cookie名称
             * @param default_value 默认值
             * @return Cookie值
             */
            std::string getCookie(const std::string& name, const std::string& default_value = "") const;

            /**
             * @brief 获取所有Cookie
             */
            const Cookies& getCookies() const { return cookies_; }

            /**
             * @brief 检查是否存在指定Cookie
             */
            bool hasCookie(const std::string& name) const;

            // ==================== 便捷方法 ====================

            /**
             * @brief 检查是否为GET请求
             */
            bool isGet() const { return method_ == HttpMethod::GET; }

            /**
             * @brief 检查是否为POST请求
             */
            bool isPost() const { return method_ == HttpMethod::POST; }

            /**
             * @brief 检查是否为PUT请求
             */
            bool isPut() const { return method_ == HttpMethod::PUT; }

            /**
             * @brief 检查是否为DELETE请求
             */
            bool isDelete() const { return method_ == HttpMethod::DELETE; }

            /**
             * @brief 检查是否支持Keep-Alive
             */
            bool isKeepAlive() const;

            /**
             * @brief 检查是否为AJAX请求
             */
            bool isAjax() const;

            /**
             * @brief 检查是否为安全连接
             */
            bool isSecure() const;

            // ==================== 静态工具方法 ====================

            /**
             * @brief HTTP方法转字符串
             */
            static std::string methodToString(HttpMethod method);

            /**
             * @brief 字符串转HTTP方法
             */
            static HttpMethod stringToMethod(const std::string& method_str);

            /**
             * @brief HTTP版本转字符串
             */
            static std::string versionToString(HttpVersion version);

            /**
             * @brief 字符串转HTTP版本
             */
            static HttpVersion stringToVersion(const std::string& version_str);

            /**
             * @brief URL解码
             */
            static std::string urlDecode(const std::string& encoded);

            /**
             * @brief URL编码
             */
            static std::string urlEncode(const std::string& decoded);

        private:
            // ==================== 私有成员变量 ====================
            bool parsed_;                           ///< 是否已解析
            HttpMethod method_;                     ///< HTTP方法
            std::string path_;                      ///< 请求路径
            std::string query_;                     ///< 查询字符串
            HttpVersion version_;                   ///< HTTP版本
            Headers headers_;                       ///< 请求头
            Parameters params_;                     ///< URL参数
            Cookies cookies_;                       ///< Cookie
            std::string body_;                      ///< 请求体
            std::string client_ip_;                 ///< 客户端IP地址
            // size_t parsed_bytes_;                   ///< 已解析的字节数
            std::chrono::steady_clock::time_point parse_time_; ///< 解析时间

            // ==================== 私有解析方法 ====================

            /**
             * @brief 解析请求行
             * @param request_line 请求行
             * @return 解析成功返回true
             */
            bool parseRequestLine(const std::string& request_line);

            /**
             * @brief 解析请求头
             * @param header_lines 头部行列表
             * @return 解析成功返回true
             */
            bool parseHeaders(const std::vector<std::string>& header_lines);

            /**
             * @brief 解析查询参数
             * @param query_string 查询字符串
             */
            void parseQueryParams(const std::string& query_string);

            /**
             * @brief 解析表单参数
             * @param form_string 表单字符串
             * @param form_data 输出的表单数据
             */
            void parseFormParams(const std::string& form_string, Parameters& form_data);

            /**
             * @brief 解析Cookie
             * @param cookie_header Cookie头部值
             */
            void parseCookies(const std::string& cookie_header);

            /**
             * @brief 分割字符串
             * @param str 待分割字符串
             * @param delimiter 分隔符
             * @return 分割结果
             */
            std::vector<std::string> split(const std::string& str, const std::string& delimiter) const;

            /**
             * @brief 去除字符串首尾空白
             * @param str 待处理字符串
             * @return 处理后字符串
             */
            std::string trim(const std::string& str) const;

            /**
             * @brief 转换为小写
             * @param str 待转换字符串
             * @return 小写字符串
             */
            std::string toLower(const std::string& str) const;

            /**
             * @brief 验证HTTP请求的有效性
             * @return 请求有效返回true
             */
            bool validateRequest() const;
        };

    } // namespace http
} // namespace common

#endif // HTTP_REQUEST_H
