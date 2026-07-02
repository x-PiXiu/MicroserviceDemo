/**
 * @file http_session.h
 * @brief HTTP会话管理类 - 管理HTTP连接的生命周期和状态
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 *
 * 功能特性:
 * - HTTP连接状态管理
 * - Keep-Alive长连接支持
 * - 请求解析和响应发送
 * - 超时管理和错误处理
 * - 连接统计和监控
 * - 线程安全的会话操作
 * - 异步IO支持
 *
 * 会话状态:
 * - CONNECTING: 正在建立连接
 * - READING: 正在读取请求
 * - PROCESSING: 正在处理请求
 * - WRITING: 正在发送响应
 * - KEEP_ALIVE: Keep-Alive等待状态
 * - CLOSING: 正在关闭连接
 * - CLOSED: 连接已关闭
 *
 * 与现有模块集成:
 * - 使用网络模块的Channel和EventLoop
 * - 集成Logger进行连接日志
 * - 支持统计信息收集
 * - 与HttpRequest/HttpResponse集成
 *
 * 使用示例:
 * @code
 * auto session = std::make_shared<HttpSession>(fd, client_addr, event_loop);
 * session->setRequestHandler([](HttpRequest& req, HttpResponse& res) {
 *     res.ok("Hello World");
 * });
 * session->start();
 * @endcode
 */

#ifndef HTTP_SESSION_H
#define HTTP_SESSION_H

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <functional>
#include <queue>
#include <cstdint>

#include "common/http/http_request.h"
#include "common/http/http_response.h"
#include "common/network/channel.h"
#include "common/network/event_loop.h"
#include "common/network/inet_address.h"
#include "common/logger/logger.h"

namespace common {
    namespace http {

        /**
         * @brief HTTP会话状态枚举
         */
        enum class HttpSessionState {
            CONNECTING,     ///< 正在建立连接
            READING,        ///< 正在读取请求
            PROCESSING,     ///< 正在处理请求
            WRITING,        ///< 正在发送响应
            KEEP_ALIVE,     ///< Keep-Alive等待状态
            CLOSING,        ///< 正在关闭连接
            CLOSED          ///< 连接已关闭
        };

        /**
         * @brief HTTP会话统计信息
         */
        struct HttpSessionStats {
            uint64_t requests_handled = 0;      ///< 处理的请求数
            uint64_t bytes_received = 0;        ///< 接收的字节数
            uint64_t bytes_sent = 0;            ///< 发送的字节数
            uint64_t errors = 0;                ///< 错误次数
            std::chrono::steady_clock::time_point created_time; ///< 创建时间
            std::chrono::steady_clock::time_point last_activity; ///< 最后活动时间

            /**
             * @brief 构造函数
             */
            HttpSessionStats() {
                auto now = std::chrono::steady_clock::now();
                created_time = now;
                last_activity = now;
            }

            /**
             * @brief 更新活动时间
             */
            void updateActivity() {
                last_activity = std::chrono::steady_clock::now();
            }

            /**
             * @brief 获取会话存活时间（毫秒）
             */
            long getLifetimeMs() const {
                auto now = std::chrono::steady_clock::now();
                return std::chrono::duration_cast<std::chrono::milliseconds>(now - created_time).count();
            }

            /**
             * @brief 获取空闲时间（毫秒）
             */
            long getIdleTimeMs() const {
                auto now = std::chrono::steady_clock::now();
                return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity).count();
            }
        };

        // 前置声明
        class HttpSession;

        /**
         * @brief 会话回调函数类型
         */
        using SessionCallback = std::function<void(std::shared_ptr<HttpSession>)>;
        using SessionErrorCallback = std::function<void(std::shared_ptr<HttpSession>, const std::string&)>;
        using RequestHandlerCallback = std::function<void(HttpRequest&, HttpResponse&)>;

        /**
         * @brief HTTP会话管理类
         * @details 管理单个HTTP连接的完整生命周期
         * 
         * 核心职责:
         * - 连接状态管理：从建立到关闭的完整生命周期
         * - 数据传输：HTTP请求接收和响应发送
         * - 协议处理：HTTP协议解析和Keep-Alive管理
         * - 超时管理：读写超时和Keep-Alive超时
         * - 错误处理：网络错误和协议错误处理
         * - 统计监控：连接统计和性能监控
         */
        class HttpSession : public std::enable_shared_from_this<HttpSession> {
        public:
            /**
             * @brief 构造函数
             * @param fd 套接字文件描述符
             * @param peer_addr 对端地址
             * @param event_loop 事件循环
             * @param recv_buffer_size 接收缓冲区大小（字节）
             * @param send_buffer_size 发送缓冲区大小（字节）
             * @param max_request_size 最大请求大小（字节）
             */
            HttpSession(int fd, const common::network::InetAddress& peer_addr, 
                       common::network::EventLoop* event_loop,
                       int recv_buffer_size = 131072,
                       int send_buffer_size = 131072,
                       size_t max_request_size = 2097152);

            /**
             * @brief 析构函数
             */
            ~HttpSession();

            // 禁用拷贝构造和赋值
            HttpSession(const HttpSession&) = delete;
            HttpSession& operator=(const HttpSession&) = delete;

            // ==================== 会话控制方法 ====================

            /**
             * @brief 启动会话
             */
            void start();

            /**
             * @brief 关闭会话
             */
            void close();

            /**
             * @brief 强制关闭会话
             */
            void forceClose();

            /**
             * @brief 检查会话是否已关闭
             */
            bool isClosed() const { return state_.load() == HttpSessionState::CLOSED; }

            /**
             * @brief 检查会话是否已连接
             */
            bool isConnected() const { 
                auto state = state_.load();
                return state != HttpSessionState::CLOSED && state != HttpSessionState::CLOSING;
            }

            // ==================== 数据传输方法 ====================

            /**
             * @brief 发送HTTP响应
             * @param response HTTP响应
             */
            void sendResponse(const HttpResponse& response);

            /**
             * @brief 内部发送HTTP响应（支持锁控制）
             * @param response HTTP响应
             * @param need_lock 是否需要获取锁
             */
            void sendResponseInternal(const HttpResponse& response, bool need_lock);

            // ==================== 配置方法 ====================

            /**
             * @brief 设置请求处理器
             * @param handler 请求处理器
             */
            void setRequestHandler(RequestHandlerCallback handler) {
                request_handler_ = handler;
            }

            /**
             * @brief 设置关闭回调
             * @param callback 关闭回调
             */
            void setCloseCallback(SessionCallback callback) {
                close_callback_ = callback;
            }

            /**
             * @brief 设置错误回调
             * @param callback 错误回调
             */
            void setErrorCallback(SessionErrorCallback callback) {
                error_callback_ = callback;
            }

            /**
             * @brief 设置读取超时
             * @param timeout_ms 超时时间（毫秒）
             */
            void setReadTimeout(int timeout_ms) {
                read_timeout_ms_ = timeout_ms;
            }

            /**
             * @brief 设置写入超时
             * @param timeout_ms 超时时间（毫秒）
             */
            void setWriteTimeout(int timeout_ms) {
                write_timeout_ms_ = timeout_ms;
            }

            /**
             * @brief 设置Keep-Alive超时
             * @param timeout_ms 超时时间（毫秒）
             */
            void setKeepAliveTimeout(int timeout_ms) {
                keep_alive_timeout_ms_ = timeout_ms;
            }

            /**
             * @brief 设置Keep-Alive最大请求数
             * @param max_requests 最大请求数
             */
            void setMaxKeepAliveRequests(int max_requests) {
                max_keep_alive_requests_ = max_requests;
            }

            /**
             * @brief 启用/禁用Keep-Alive
             * @param enabled 是否启用
             */
            void setKeepAliveEnabled(bool enabled) {
                keep_alive_enabled_ = enabled;
            }

            // ==================== 状态查询方法 ====================

            /**
             * @brief 获取会话ID
             */
            int getId() const { return session_id_; }

            /**
             * @brief 获取会话状态
             */
            HttpSessionState getState() const { return state_.load(); }

            /**
             * @brief 获取对端地址
             */
            const common::network::InetAddress& getPeerAddress() const { return peer_addr_; }

            /**
             * @brief 检查是否超时
             */
            bool isTimeout() const;

            /**
             * @brief 获取统计信息
             */
            const HttpSessionStats& getStats() const { return stats_; }

            /**
             * @brief 获取Keep-Alive请求计数
             */
            int getKeepAliveRequestCount() const { return keep_alive_request_count_; }

        private:
            // ==================== 私有数据结构 ====================

            /**
             * @brief 异步写入任务结构
             */
            struct AsyncWriteTask {
                int session_id;                                 ///< 会话ID
                std::string data;                               ///< 要写入的数据
                size_t offset = 0;
                std::function<void(bool)> callback;            ///< 完成回调
                std::chrono::steady_clock::time_point start_time; ///< 开始时间
                size_t bytes_written = 0;                      ///< 已写入字节数
                int retry_count = 0;                           ///< 重试次数
                std::atomic<bool> callback_called{false};      ///< 回调是否已被调用

                // 👇 新增字段
                bool keep_alive = false;      // 是否应保持连接
                std::string connection_header; // 用于日志或调试
                std::string http_version;

                /**
                 * @brief 获取剩余数据
                 */
                std::string getRemainingData() const {
                    return data.substr(bytes_written);
                }

                /**
                 * @brief 检查是否完成
                 */
                bool isComplete() const {
                    return bytes_written >= data.size();
                }

                /**
                 * @brief 获取进度百分比
                 */
                double getProgress() const {
                    return data.empty() ? 100.0 : (static_cast<double>(bytes_written) / data.size() * 100.0);
                }
            };

            // ==================== 私有成员变量 ====================
            int fd_;                                            ///< 套接字文件描述符
            int session_id_;                                    ///< 会话ID（保存原始fd，避免关闭后丢失）
            common::network::InetAddress peer_addr_;           ///< 对端地址
            common::network::EventLoop* event_loop_;           ///< 事件循环
            std::unique_ptr<common::network::Channel> channel_; ///< 网络通道

            std::atomic<HttpSessionState> state_;               ///< 会话状态
            std::mutex session_mutex_;                          ///< 会话锁

            // 缓冲区
            std::string input_buffer_;                          ///< 输入缓冲区
            std::string output_buffer_;                         ///< 输出缓冲区
            size_t bytes_to_write_;                             ///< 待写入字节数

            // 超时配置
            int read_timeout_ms_;                               ///< 读取超时（毫秒）
            int write_timeout_ms_;                              ///< 写入超时（毫秒）
            int keep_alive_timeout_ms_;                         ///< Keep-Alive超时（毫秒）
            std::chrono::steady_clock::time_point last_io_time_; ///< 最后IO时间
            
            // 缓冲区配置 ⭐ 新增
            int recv_buffer_size_;                              ///< 接收缓冲区大小（字节）
            int send_buffer_size_;                              ///< 发送缓冲区大小（字节）
            size_t max_request_size_;                           ///< 最大请求大小（字节）

            // Keep-Alive配置
            bool keep_alive_enabled_;                           ///< 是否启用Keep-Alive
            int max_keep_alive_requests_;                       ///< 最大Keep-Alive请求数
            int keep_alive_request_count_;                      ///< 当前Keep-Alive请求计数

            // 请求处理
            HttpRequest current_request_;                       ///< 当前请求
            RequestHandlerCallback request_handler_;            ///< 请求处理器

            // 回调函数
            SessionCallback close_callback_;                    ///< 关闭回调
            SessionErrorCallback error_callback_;               ///< 错误回调
            std::atomic<bool> close_callback_called_;           ///< 关闭回调是否已调用

            // 统计信息
            HttpSessionStats stats_;                            ///< 统计信息

            // 配置常量
            static constexpr size_t MAX_RESPONSE_SIZE = 10 * 1024 * 1024;     ///< 最大响应大小(10MB)
            static constexpr size_t MAX_PENDING_WRITES = 1024;     ///< 最大队列大小

            // ==================== 私有方法 ====================

            /**
             * @brief 处理读取事件
             */
            void handleRead();

            /**
             * @brief 处理写入事件
             */
            void handleWrite();

            /**
             * @brief 处理关闭事件
             */
            void handleClose();

            /**
             * @brief 处理错误事件
             */
            void handleError();

            /**
             * @brief 解析HTTP请求
             * @return 解析成功返回true
             */
            bool parseHttpRequest();

            /**
             * @brief 解析chunked编码的HTTP请求
             * @param header_end 头部结束位置
             * @return 完整请求的总长度，0表示数据不完整
             */
            size_t parseChunkedRequest(size_t header_end);

            /**
             * @brief 处理HTTP请求
             */
            void processHttpRequest();

            /**
             * @brief 准备响应数据
             * @param response HTTP响应
             */
            void prepareResponse(const HttpResponse& response);

            /**
             * @brief 检查是否应该保持连接
             * @param request HTTP请求
             * @param response HTTP响应
             * @return 应该保持连接返回true
             */
            bool shouldKeepAlive(const HttpRequest& request, const HttpResponse& response) const;

            bool shouldKeepAliveFromHeaders(const std::string& connection_header, const std::string& http_version);
            /**
             * @brief 重置会话以处理下一个请求
             */
            void resetForNextRequest();

            /**
             * @brief 更新IO时间
             */
            void updateIOTime();

            /**
             * @brief 设置会话状态
             * @param new_state 新状态
             */
            void setState(HttpSessionState new_state);

            /**
             * @brief 触发错误
             * @param error 错误信息
             */
            void triggerError(const std::string& error);

            /**
             * @brief 安全关闭连接
             */
            void removeChannelFromEventLoop();

            // ==================== 异步写入相关方法 ====================

            /**
             * @brief 处理下一个写入任务
             */
            void processNextWriteTask();

            /**
             * @brief 处理异步写入完成
             * @param task 写入任务
             * @param success 是否成功
             */
            void onAsyncWriteComplete(std::shared_ptr<AsyncWriteTask> task, bool success);

            /**
             * @brief 清理写入任务队列
             */
            void cleanupWriteTasks();

            /**
             * @brief 安全调用关闭回调（确保只调用一次）
             */
            void callCloseCallbackOnce();
        };

    } // namespace http
} // namespace common

#endif // HTTP_SESSION_H
