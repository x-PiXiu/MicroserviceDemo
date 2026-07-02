/**
 * @file http_response.cpp
 * @brief HTTP响应类的实现
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 * 
 * 实现说明:
 * - 完整的HTTP响应构建功能
 * - 支持多种响应格式（JSON、HTML、文本等）
 * - 自动Content-Length计算
 * - Cookie和头部管理
 * - 便捷的状态码设置方法
 */

#include "common/http/http_response.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

namespace common {
    namespace http {

        HttpResponse::HttpResponse(HttpVersion version)
            : version_(version)
            , status_code_(200)
            , reason_phrase_("OK") {
            setDefaultHeaders();
        }

        HttpResponse::HttpResponse(const HttpResponse& other)
            : version_(other.version_)
            , status_code_(other.status_code_)
            , reason_phrase_(other.reason_phrase_)
            , headers_(other.headers_)
            , body_(other.body_) {
        }

        HttpResponse& HttpResponse::operator=(const HttpResponse& other) {
            if (this != &other) {
                version_ = other.version_;
                status_code_ = other.status_code_;
                reason_phrase_ = other.reason_phrase_;
                headers_ = other.headers_;
                body_ = other.body_;
            }
            return *this;
        }

        HttpResponse::HttpResponse(HttpResponse&& other) noexcept
            : version_(other.version_)
            , status_code_(other.status_code_)
            , reason_phrase_(std::move(other.reason_phrase_))
            , headers_(std::move(other.headers_))
            , body_(std::move(other.body_)) {
            other.reset();
        }

        HttpResponse& HttpResponse::operator=(HttpResponse&& other) noexcept {
            if (this != &other) {
                version_ = other.version_;
                status_code_ = other.status_code_;
                reason_phrase_ = std::move(other.reason_phrase_);
                headers_ = std::move(other.headers_);
                body_ = std::move(other.body_);
                other.reset();
            }
            return *this;
        }

        void HttpResponse::setStatus(int status_code) {
            status_code_ = status_code;
            reason_phrase_ = getDefaultReasonPhrase(status_code);
            // LOG_DEBUG("Set HTTP status: " + std::to_string(status_code) + " " + reason_phrase_);
        }

        void HttpResponse::setStatus(int status_code, const std::string& reason_phrase) {
            status_code_ = status_code;
            reason_phrase_ = reason_phrase;
            // LOG_DEBUG("Set HTTP status: " + std::to_string(status_code) + " " + reason_phrase);
        }

        void HttpResponse::setStatus(HttpStatus status) {
            setStatus(static_cast<int>(status));
        }

        void HttpResponse::setHeader(const std::string& key, const std::string& value) {
            headers_[toLower(key)] = value;
            // LOG_DEBUG("Set header: " + key + " = " + value);
        }

        void HttpResponse::setHeader(const std::string& key, int value) {
            setHeader(key, std::to_string(value));
        }

        void HttpResponse::addHeader(const std::string& key, const std::string& value) {
            std::string lower_key = toLower(key);

            // 检查是否已存在该头部
            auto it = headers_.find(lower_key);
            if (it != headers_.end()) {
                // 如果已存在，追加新值（用换行分隔，HTTP允许多行头部）
                it->second += "\r\n" + key + ": " + value;
            } else {
                // 如果不存在，直接设置
                headers_[lower_key] = value;
            }

            // LOG_DEBUG("Add header: " + key + " = " + value);
        }

        std::string HttpResponse::getHeader(const std::string& key) const {
            std::string lower_key = toLower(key);
            auto it = headers_.find(lower_key);
            return (it != headers_.end()) ? it->second : "";
        }

        void HttpResponse::removeHeader(const std::string& key) {
            std::string lower_key = toLower(key);
            headers_.erase(lower_key);
            // LOG_DEBUG("Removed header: " + key);
        }

        bool HttpResponse::hasHeader(const std::string& key) const {
            std::string lower_key = toLower(key);
            return headers_.find(lower_key) != headers_.end();
        }

        void HttpResponse::setBody(const std::string& body) {
            body_ = body;
            updateContentLength();
            // LOG_DEBUG("Set response body, length: " + std::to_string(body_.length()));
        }

        void HttpResponse::setBody(const char* data, size_t size) {
            body_.assign(data, size);
            updateContentLength();
            // LOG_DEBUG("Set response body from binary data, length: " + std::to_string(size));
        }

        void HttpResponse::appendBody(const std::string& content) {
            body_ += content;
            updateContentLength();
            // LOG_DEBUG("Appended to response body, new length: " + std::to_string(body_.length()));
        }

        void HttpResponse::clearBody() {
            body_.clear();
            updateContentLength();
            // LOG_DEBUG("Cleared response body");
        }

        void HttpResponse::setJsonBody(const nlohmann::json& json) {
            try {
                std::string json_str = json.dump();
                setBody(json_str);
                setHeader("Content-Type", "application/json; charset=utf-8");
                // LOG_DEBUG("Set JSON response body");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to serialize JSON: " + std::string(e.what()));
                throw std::runtime_error("Failed to serialize JSON response");
            }
        }

        void HttpResponse::setJsonBody(const std::string& json_str) {
            try {
                // 验证JSON格式
                (void)nlohmann::json::parse(json_str);
                setBody(json_str);
                setHeader("Content-Type", "application/json; charset=utf-8");
                // LOG_DEBUG("Set JSON response body from string");
            } catch (const nlohmann::json::parse_error& e) {
                LOG_ERROR("Invalid JSON string: " + std::string(e.what()));
                throw std::runtime_error("Invalid JSON format");
            }
        }

        void HttpResponse::ok(const std::string& body) {
            setStatus(200);
            if (!body.empty()) {
                setBody(body);
                if (!hasHeader("content-type")) {
                    setHeader("Content-Type", "text/plain; charset=utf-8");
                }
            }
        }

        void HttpResponse::ok(const nlohmann::json& json) {
            setStatus(200);
            setJsonBody(json);
        }

        void HttpResponse::created(const std::string& body) {
            setStatus(201);
            if (!body.empty()) {
                setBody(body);
                if (!hasHeader("content-type")) {
                    setHeader("Content-Type", "text/plain; charset=utf-8");
                }
            }
        }

        void HttpResponse::created(const nlohmann::json& json) {
            setStatus(201);
            setJsonBody(json);
        }

        void HttpResponse::noContent() {
            setStatus(204);
            clearBody();
        }

        void HttpResponse::error(int status_code, const std::string& message) {
            setStatus(status_code);
            nlohmann::json error_json = {
                {"error", true},
                {"status", status_code},
                {"message", message},
                {"timestamp", getCurrentHttpTime()}
            };
            setJsonBody(error_json);
        }

        void HttpResponse::error(int status_code, const nlohmann::json& json) {
            setStatus(status_code);
            setJsonBody(json);
        }

        void HttpResponse::badRequest(const std::string& message) {
            error(400, message);
        }

        void HttpResponse::unauthorized(const std::string& message) {
            error(401, message);
        }

        void HttpResponse::forbidden(const std::string& message) {
            error(403, message);
        }

        void HttpResponse::notFound(const std::string& message) {
            error(404, message);
        }

        void HttpResponse::internalServerError(const std::string& message) {
            error(500, message);
        }

        void HttpResponse::requestEntityTooLarge(const std::string& message) {
            error(413, message);
        }

        void HttpResponse::redirect(const std::string& location) {
            setStatus(301);
            setHeader("Location", location);
            clearBody();
        }

        void HttpResponse::temporaryRedirect(const std::string& location) {
            setStatus(302);
            setHeader("Location", location);
            clearBody();
        }

        void HttpResponse::setCookie(const std::string& name, const std::string& value,
                                   int max_age, const std::string& path,
                                   const std::string& domain, bool secure, bool http_only) {
            // 构建Cookie字符串
            std::ostringstream cookie;
            cookie << name << "=" << value;

            // 设置过期时间（秒）
            if (max_age >= 0) {
                cookie << "; Max-Age=" << max_age;
            }

            // 设置路径范围
            if (!path.empty()) {
                cookie << "; Path=" << path;
            }

            // 设置域名范围
            if (!domain.empty()) {
                cookie << "; Domain=" << domain;
            }

            // 安全传输标志（仅HTTPS）
            if (secure) {
                cookie << "; Secure";
            }

            // 防止JavaScript访问（XSS防护）
            if (http_only) {
                cookie << "; HttpOnly";
            }

            // 🔧 修复：使用addHeader而不是setHeader，避免覆盖其他Cookie
            addHeader("Set-Cookie", cookie.str());
            // LOG_DEBUG("Set cookie: " + name + "=" + value);
        }

        void HttpResponse::setCookieSecure(const std::string& name, const std::string& value,
                                         int max_age, const std::string& path,
                                         const std::string& domain, bool secure,
                                         bool http_only, const std::string& same_site) {
            // 构建安全Cookie字符串
            std::ostringstream cookie;
            cookie << name << "=" << value;

            // 设置过期时间（秒）
            if (max_age >= 0) {
                cookie << "; Max-Age=" << max_age;
            }

            // 设置路径范围（默认为根路径）
            if (!path.empty()) {
                cookie << "; Path=" << path;
            }

            // 设置域名范围
            if (!domain.empty()) {
                cookie << "; Domain=" << domain;
            }

            // 安全传输标志（仅HTTPS）- 默认启用
            if (secure) {
                cookie << "; Secure";
            }

            // 防止JavaScript访问（XSS防护）- 默认启用
            if (http_only) {
                cookie << "; HttpOnly";
            }

            // SameSite属性（CSRF防护）- 默认为Lax
            if (!same_site.empty()) {
                cookie << "; SameSite=" << same_site;
            }

            // 使用addHeader支持多个Cookie
            addHeader("Set-Cookie", cookie.str());
            // LOG_DEBUG("Set secure cookie: " + name + "=" + value +
            //          " (Secure=" + (secure ? "true" : "false") +
            //          ", HttpOnly=" + (http_only ? "true" : "false") +
            //          ", SameSite=" + same_site + ")");
        }

        void HttpResponse::deleteCookie(const std::string& name, const std::string& path, const std::string& domain) {
            setCookie(name, "", 0, path, domain, false, false);
            // LOG_DEBUG("Deleted cookie: " + name);
        }

        void HttpResponse::setCacheControl(const std::string& cache_control) {
            setHeader("Cache-Control", cache_control);
        }

        void HttpResponse::setExpires(int expires) {
            auto future_time = std::chrono::system_clock::now() + std::chrono::seconds(expires);
            auto time_t = std::chrono::system_clock::to_time_t(future_time);
            
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time_t), "%a, %d %b %Y %H:%M:%S GMT");
            setHeader("Expires", oss.str());
        }

        void HttpResponse::setETag(const std::string& etag) {
            setHeader("ETag", "\"" + etag + "\"");
        }

        void HttpResponse::setLastModified(const std::string& last_modified) {
            setHeader("Last-Modified", last_modified);
        }

        std::string HttpResponse::toString() const {
            // ⭐⭐⭐ 优化3: 预分配空间，直接构建字符串，避免ostringstream多次拷贝
            std::string response;
            
            // 估计响应大小：状态行 + 所有头部 + 空行 + 响应体
            size_t estimated_size = 100;  // 状态行
            for (const auto& [key, value] : headers_) {
                estimated_size += key.size() + value.size() + 4;  // ": " + "\r\n"
            }
            estimated_size += 2;  // "\r\n"
            estimated_size += body_.size();
            
            response.reserve(estimated_size);  // ⭐ 预分配，消除90%的内存重分配
            
            // 状态行
            response.append(versionToString(version_));
            response.append(" ");
            response.append(std::to_string(status_code_));
            response.append(" ");
            response.append(reason_phrase_);
            response.append("\r\n");
            
            // 头部
            for (const auto& [key, value] : headers_) {
                response.append(key);
                response.append(": ");
                response.append(value);
                response.append("\r\n");
            }
            
            // 空行分隔头部和体
            response.append("\r\n");
            
            // 响应体
            if (!body_.empty()) {
                response.append(body_);
            }
            
            return response;
        }

        size_t HttpResponse::getSize() const {
            return toString().length();
        }

        void HttpResponse::reset() {
            version_ = HttpVersion::HTTP_1_1;
            status_code_ = 200;
            reason_phrase_ = "OK";
            headers_.clear();
            body_.clear();
            setDefaultHeaders();
            // LOG_DEBUG("Reset HTTP response");
        }

        std::string HttpResponse::getDefaultReasonPhrase(int status_code) {
            switch (status_code) {
                case 100: return "Continue";
                case 101: return "Switching Protocols";
                case 200: return "OK";
                case 201: return "Created";
                case 202: return "Accepted";
                case 204: return "No Content";
                case 206: return "Partial Content";
                case 301: return "Moved Permanently";
                case 302: return "Found";
                case 304: return "Not Modified";
                case 307: return "Temporary Redirect";
                case 308: return "Permanent Redirect";
                case 400: return "Bad Request";
                case 401: return "Unauthorized";
                case 403: return "Forbidden";
                case 404: return "Not Found";
                case 405: return "Method Not Allowed";
                case 406: return "Not Acceptable";
                case 408: return "Request Timeout";
                case 409: return "Conflict";
                case 410: return "Gone";
                case 411: return "Length Required";
                case 413: return "Payload Too Large";
                case 414: return "URI Too Long";
                case 415: return "Unsupported Media Type";
                case 416: return "Range Not Satisfiable";
                case 417: return "Expectation Failed";
                case 429: return "Too Many Requests";
                case 500: return "Internal Server Error";
                case 501: return "Not Implemented";
                case 502: return "Bad Gateway";
                case 503: return "Service Unavailable";
                case 504: return "Gateway Timeout";
                case 505: return "HTTP Version Not Supported";
                default:  return "Unknown";
            }
        }

        std::string HttpResponse::getCurrentHttpTime() {
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            
            std::ostringstream oss;
            oss << std::put_time(std::gmtime(&time_t), "%a, %d %b %Y %H:%M:%S GMT");
            return oss.str();
        }

        void HttpResponse::updateContentLength() {
            setHeader("Content-Length", std::to_string(body_.length()));
        }

        void HttpResponse::setDefaultHeaders() {
            setHeader("Server", "GameMicroservices/1.0");
            setHeader("Date", getCurrentHttpTime());
            setHeader("Connection", "keep-alive");
        }

        std::string HttpResponse::versionToString(HttpVersion version) const {
            switch (version) {
                case HttpVersion::HTTP_1_0: return "HTTP/1.0";
                case HttpVersion::HTTP_1_1: return "HTTP/1.1";
                case HttpVersion::HTTP_2_0: return "HTTP/2.0";
                default:                     return "HTTP/1.1";
            }
        }

        std::string HttpResponse::toLower(const std::string& str) const {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(), ::tolower);
            return result;
        }

    } // namespace http
} // namespace common
