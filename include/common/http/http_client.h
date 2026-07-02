/**
 * @file http_client.h
 * @brief HTTP客户端 - 基于现有网络模块的HTTP客户端实现
 * @author 29108
 * @date 2025/7/25
 * @version 1.0
 *
 * 功能特性:
 * - 支持GET、POST、PUT、DELETE等HTTP方法
 * - 支持自定义请求头和超时设置
 * - 基于现有Socket和网络模块实现
 * - 线程安全的HTTP请求处理
 * - 支持JSON请求和响应
 *
 * 与现有模块集成:
 * - 使用common/network/socket.h进行网络通信
 * - 使用common/logger/logger.h进行日志记录
 * - 集成nlohmann::json进行JSON处理
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <nlohmann/json.hpp>
#include "common/logger/logger.h"
#include "common/network/socket.h"
#include "common/network/inet_address.h"

namespace common {
namespace http {

    /**
     * @brief HTTP响应结构体
     */
    struct HttpClientResponse {
        int status_code = 0;                                    ///< HTTP状态码
        std::string body;                                       ///< 响应体
        std::unordered_map<std::string, std::string> headers;   ///< 响应头
        bool success = false;                                   ///< 请求是否成功
        std::string error_message;                              ///< 错误信息
    };

    /**
     * @brief HTTP客户端类
     * @details 基于现有网络模块实现的简单HTTP客户端
     */
    class HttpClient {
    public:
        /**
         * @brief 构造函数
         */
        HttpClient();

        /**
         * @brief 析构函数
         */
        ~HttpClient();

        /**
         * @brief 发送GET请求
         * @param url 请求URL
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse get(const std::string& url, int timeout_ms = 5000);
        
        /**
         * @brief 发送GET请求（支持自定义头部）
         * @param url 请求URL
         * @param headers 请求头
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse get(const std::string& url, 
                             const std::unordered_map<std::string, std::string>& headers,
                             int timeout_ms = 5000);

        /**
         * @brief 发送POST请求
         * @param url 请求URL
         * @param body 请求体
         * @param headers 请求头
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse post(const std::string& url, 
                              const std::string& body,
                              const std::unordered_map<std::string, std::string>& headers = {},
                              int timeout_ms = 5000);

        /**
         * @brief 发送PUT请求
         * @param url 请求URL
         * @param body 请求体
         * @param headers 请求头
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse put(const std::string& url,
                             const std::string& body,
                             const std::unordered_map<std::string, std::string>& headers = {},
                             int timeout_ms = 5000);

        /**
         * @brief 发送DELETE请求
         * @param url 请求URL
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse del(const std::string& url, int timeout_ms = 5000);
        
        /**
         * @brief 发送DELETE请求（支持自定义头部）
         * @param url 请求URL
         * @param headers 请求头
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse del(const std::string& url,
                             const std::unordered_map<std::string, std::string>& headers,
                             int timeout_ms = 5000);

        /**
         * @brief 发送OPTIONS请求（支持自定义头部）
         * @param url 请求URL
         * @param headers 请求头
         * @param timeout_ms 超时时间（毫秒）
         * @return HTTP响应
         */
        HttpClientResponse options(const std::string& url,
                                 const std::unordered_map<std::string, std::string>& headers = {},
                                 int timeout_ms = 5000);

        /**
         * @brief 设置默认请求头
         * @param key 头部名称
         * @param value 头部值
         */
        void setDefaultHeader(const std::string& key, const std::string& value);

        /**
         * @brief 设置默认超时时间
         * @param timeout_ms 超时时间（毫秒）
         */
        void setDefaultTimeout(int timeout_ms);

    private:
        std::unordered_map<std::string, std::string> default_headers_; ///< 默认请求头
        int default_timeout_ms_ = 5000;                                ///< 默认超时时间

        /**
         * @brief 发送HTTP请求的通用方法
         * @param method HTTP方法
         * @param url 请求URL
         * @param body 请求体
         * @param headers 请求头
         * @param timeout_ms 超时时间
         * @return HTTP响应
         */
        HttpClientResponse sendRequest(const std::string& method,
                                     const std::string& url,
                                     const std::string& body,
                                     const std::unordered_map<std::string, std::string>& headers,
                                     int timeout_ms);

        /**
         * @brief 解析URL
         * @param url 完整URL
         * @param host 输出主机名
         * @param port 输出端口
         * @param path 输出路径
         * @return 解析成功返回true
         */
        bool parseUrl(const std::string& url, std::string& host, int& port, std::string& path);

        /**
         * @brief 构建HTTP请求字符串
         * @param method HTTP方法
         * @param path 请求路径
         * @param host 主机名
         * @param body 请求体
         * @param headers 请求头
         * @return HTTP请求字符串
         */
        std::string buildHttpRequest(const std::string& method,
                                   const std::string& path,
                                   const std::string& host,
                                   const std::string& body,
                                   const std::unordered_map<std::string, std::string>& headers);

        /**
         * @brief 解析HTTP响应
         * @param response_data 响应数据
         * @return 解析后的HTTP响应
         */
        HttpClientResponse parseHttpResponse(const std::string& response_data);
    };

} // namespace http
} // namespace common

#endif // HTTP_CLIENT_H
