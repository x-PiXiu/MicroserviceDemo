/**
 * @file http_client.cpp
 * @brief HTTP客户端实现 - 基于libcurl的高性能实现
 * @author 29108
 * @date 2025/7/25
 * @version 2.0 - 使用libcurl替换原生socket实现，提高稳定性和性能
 */

#include "common/http/http_client.h"
#include <curl/curl.h>
#include <sstream>
#include <mutex>

namespace common {
namespace http {

    // CURL全局初始化（线程安全）
    static std::once_flag curl_init_flag;
    static void initCurl() {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    // CURL写入回调函数
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t total_size = size * nmemb;
        userp->append(static_cast<char*>(contents), total_size);
        return total_size;
    }

    // CURL头部回调函数
    static size_t headerCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
        size_t total_size = size * nitems;
        auto* headers = static_cast<std::unordered_map<std::string, std::string>*>(userdata);
        
        std::string header_line(buffer, total_size);
        
        // 移除行尾的\r\n
        while (!header_line.empty() && (header_line.back() == '\r' || header_line.back() == '\n')) {
            header_line.pop_back();
        }
        
        // 解析头部
        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = header_line.substr(0, colon_pos);
            std::string value = header_line.substr(colon_pos + 1);
            
            // 去除前后空格
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            (*headers)[key] = value;
        }
        
        return total_size;
    }

    HttpClient::HttpClient() {
        // 确保CURL全局初始化
        std::call_once(curl_init_flag, initCurl);
        
        // 设置默认请求头
        default_headers_["User-Agent"] = "HttpClient/2.0 (libcurl)";
        default_timeout_ms_ = 5000;
        
        // LOG_DEBUG("HttpClient initialized (using libcurl)");
    }

    HttpClient::~HttpClient() = default;

    HttpClientResponse HttpClient::get(const std::string& url, int timeout_ms) {
        return sendRequest("GET", url, "", {}, timeout_ms);
    }
    
    HttpClientResponse HttpClient::get(const std::string& url, 
                                     const std::unordered_map<std::string, std::string>& headers,
                                     int timeout_ms) {
        return sendRequest("GET", url, "", headers, timeout_ms);
    }

    HttpClientResponse HttpClient::post(const std::string& url, 
                                      const std::string& body,
                                      const std::unordered_map<std::string, std::string>& headers,
                                      int timeout_ms) {
        return sendRequest("POST", url, body, headers, timeout_ms);
    }

    HttpClientResponse HttpClient::put(const std::string& url,
                                     const std::string& body,
                                     const std::unordered_map<std::string, std::string>& headers,
                                     int timeout_ms) {
        return sendRequest("PUT", url, body, headers, timeout_ms);
    }

    HttpClientResponse HttpClient::del(const std::string& url, int timeout_ms) {
        return sendRequest("DELETE", url, "", {}, timeout_ms);
    }
    
    HttpClientResponse HttpClient::del(const std::string& url,
                                     const std::unordered_map<std::string, std::string>& headers,
                                     int timeout_ms) {
        return sendRequest("DELETE", url, "", headers, timeout_ms);
    }

    HttpClientResponse HttpClient::options(const std::string& url,
                                         const std::unordered_map<std::string, std::string>& headers,
                                         int timeout_ms) {
        return sendRequest("OPTIONS", url, "", headers, timeout_ms);
    }

    void HttpClient::setDefaultHeader(const std::string& key, const std::string& value) {
        default_headers_[key] = value;
    }

    void HttpClient::setDefaultTimeout(int timeout_ms) {
        default_timeout_ms_ = timeout_ms;
    }

    HttpClientResponse HttpClient::sendRequest(const std::string& method,
                                             const std::string& url,
                                             const std::string& body,
                                             const std::unordered_map<std::string, std::string>& headers,
                                             int timeout_ms) {
        HttpClientResponse response;
        int actual_timeout = (timeout_ms > 0) ? timeout_ms : default_timeout_ms_;
        
        // LOG_DEBUG("Sending " + method + " request to: " + url);

        CURL* curl = nullptr;
        struct curl_slist* curl_headers = nullptr;
        
        try {
            // 创建CURL句柄
            curl = curl_easy_init();
            if (!curl) {
                response.error_message = "Failed to initialize CURL";
                LOG_ERROR(response.error_message);
                return response;
            }

            // 设置URL
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            
            // 设置HTTP方法
            if (method == "GET") {
                curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            } else if (method == "POST") {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());
            } else if (method == "PUT") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.length());
            } else if (method == "DELETE") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            } else if (method == "OPTIONS") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
            } else {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
            }

            // 设置超时时间
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)actual_timeout);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, (long)actual_timeout);
            
            // 设置写回调（接收响应体）
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
            
            // 设置头部回调（接收响应头）
            curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
            curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

            // 设置请求头
            // 添加默认头部
            for (const auto& [key, value] : default_headers_) {
                std::string header = key + ": " + value;
                curl_headers = curl_slist_append(curl_headers, header.c_str());
            }
            
            // 添加自定义头部
            for (const auto& [key, value] : headers) {
                std::string header = key + ": " + value;
                curl_headers = curl_slist_append(curl_headers, header.c_str());
            }
            
            if (curl_headers) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
            }

            // 启用详细日志（仅在DEBUG模式下）
            #ifdef DEBUG_CURL
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
            #endif
            
            // 跟随重定向
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
            
            // SSL选项（生产环境应启用证书验证）
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  // 开发环境可以关闭
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            // 执行请求
            // LOG_DEBUG("Executing CURL request...");
            CURLcode res = curl_easy_perform(curl);
            
            // 获取HTTP状态码
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);
            
            // 检查CURL执行结果
            if (res != CURLE_OK) {
                response.success = false;
                response.error_message = "CURL error: " + std::string(curl_easy_strerror(res));
                LOG_ERROR("HTTP request failed: " + response.error_message + " (URL: " + url + ")");
            } else {
                // HTTP状态码2xx表示成功
                if (response.status_code >= 200 && response.status_code < 300) {
                    response.success = true;
                    // LOG_DEBUG("HTTP request succeeded with status " + std::to_string(response.status_code));
                } else {
                    response.success = false;
                    response.error_message = "HTTP error: " + std::to_string(response.status_code);
                    LOG_WARNING("HTTP request returned status " + std::to_string(response.status_code));
                }
            }
            
            // LOG_DEBUG("Received response: " + std::to_string(response.body.length()) + " bytes");
            // if (!response.body.empty() && response.body.length() < 500) {
            //     LOG_DEBUG("Response body: " + response.body);
            // }

        } catch (const std::exception& e) {
            response.success = false;
            response.error_message = "Exception: " + std::string(e.what());
            LOG_ERROR("HTTP request exception: " + response.error_message);
        }
        
        // 清理资源
        if (curl_headers) {
            curl_slist_free_all(curl_headers);
        }
        if (curl) {
            curl_easy_cleanup(curl);
        }

        return response;
    }

    // 保留这些方法用于向后兼容（虽然libcurl实现中不再需要）
    bool HttpClient::parseUrl(const std::string& url, std::string& host, int& port, std::string& path) {
        // libcurl会自动处理URL解析，此方法保留用于兼容性
        return true;
    }

    std::string HttpClient::buildHttpRequest(const std::string& method,
                                           const std::string& path,
                                           const std::string& host,
                                           const std::string& body,
                                           const std::unordered_map<std::string, std::string>& headers) {
        // libcurl会自动构建请求，此方法保留用于兼容性
        return "";
    }

    HttpClientResponse HttpClient::parseHttpResponse(const std::string& response_data) {
        // libcurl会自动解析响应，此方法保留用于兼容性
        HttpClientResponse response;
        return response;
    }

} // namespace http
} // namespace common
