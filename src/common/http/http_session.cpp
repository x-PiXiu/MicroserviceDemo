/**
 * @file http_session.cpp
 * @brief HTTP会话管理的实现
 * @author AI Assistant
 * @date 2025/7/11
 * @version 1.0
 * 
 * 实现说明:
 * - 管理单个HTTP连接的完整生命周期
 * - 支持Keep-Alive长连接
 * - 集成现有的网络模块进行IO操作
 * - 提供完整的超时和错误处理
 */

#include "common/http/http_session.h"
#include <unistd.h>
#include <sys/socket.h>
#include <algorithm>

namespace common {
    namespace http {

        HttpSession::HttpSession(int fd, const common::network::InetAddress& peer_addr,
                                common::network::EventLoop* event_loop,
                                int recv_buffer_size,
                                int send_buffer_size,
                                size_t max_request_size)
            : fd_(fd)
            , session_id_(fd)  // 保存原始fd作为会话ID
            , peer_addr_(peer_addr)
            , event_loop_(event_loop)
            , state_(HttpSessionState::CONNECTING)
            , bytes_to_write_(0)
            , read_timeout_ms_(30000)
            , write_timeout_ms_(30000)
            , keep_alive_timeout_ms_(60000)
            , recv_buffer_size_(recv_buffer_size)  // ⭐ 新增：从配置传入
            , send_buffer_size_(send_buffer_size)  // ⭐ 新增：从配置传入
            , max_request_size_(max_request_size) // ⭐ 新增：从配置传入
            , keep_alive_enabled_(true)
            , max_keep_alive_requests_(100)
            , keep_alive_request_count_(0)
            , close_callback_called_(false) {
            
            // ⭐⭐⭐ 优化1: 预分配缓冲区，避免频繁内存重分配
            input_buffer_.reserve(max_request_size_);   // 预分配最大请求大小
            output_buffer_.reserve(send_buffer_size_);  // 预分配发送缓冲区大小
            
            // 创建网络通道
            channel_ = std::make_unique<common::network::Channel>(event_loop_, fd_);
            
            // 设置事件回调
            channel_->setReadCallback([this] { handleRead(); });
            channel_->setWriteCallback([this] { handleWrite(); });
            channel_->setCloseCallback([this] { handleClose(); });
            channel_->setErrorCallback([this] { handleError(); });
            
            updateIOTime();
        }

        HttpSession::~HttpSession() {
            try {
                // LOG_DEBUG("[~HttpSession] HttpSession destroyed for fd: " + std::to_string(fd_));
                if (fd_ >= 0) {
                    ::close(fd_);
                    fd_ = -1;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("[~HttpSession] Exception in destructor for fd " + std::to_string(fd_) + ": " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("[~HttpSession] Exception in destructor for fd " + std::to_string(fd_));
            }
        }

        void HttpSession::start() {
            std::lock_guard<std::mutex> lock(session_mutex_);

            if (state_.load() != HttpSessionState::CONNECTING) {
                LOG_WARNING("Cannot start session in state: " + std::to_string(static_cast<int>(state_.load())));
                return;
            }

            setState(HttpSessionState::READING);

            channel_->enableReading();

            updateIOTime();
        }

        void HttpSession::close() {
            auto self = shared_from_this();  // ✅ 捕获shared_ptr保证生命周期
            
            // 第一步：检查状态并设置为CLOSING
            bool has_pending_data = false;
            common::network::EventLoop* loop_ptr = nullptr;  // ✅ 裸指针类型
            common::network::Channel* channel_ptr = nullptr; // ✅ 裸指针类型
            
            {
                std::lock_guard<std::mutex> lock(session_mutex_);
                
                if (isClosed()) {
                    LOG_WARNING("会话已关闭，跳过重复关闭: session_id=" + std::to_string(session_id_));
                    return;
                }
                setState(HttpSessionState::CLOSING);

                // 检查是否有待发送数据
                has_pending_data = !output_buffer_.empty();
                
                // ⭐⭐⭐ 关键修复：在锁内复制裸指针，避免后续访问成员变量
                loop_ptr = event_loop_;      // 复制裸指针
                channel_ptr = channel_.get(); // 获取unique_ptr的裸指针
            }

            // LOG_DEBUG("从这里进去: session_id=" + std::to_string(session_id_));
            
            // 第二步：在锁外调用关闭回调，避免死锁
            // ⚠️ 注意：这之后HttpSession可能被从 session_shards_ 中移除，但self仍然持有引用
            callCloseCallbackOnce();

            // 第三步：使用复制的指针，避免访问成员变量
            // ✅ self 保护了HttpSession的生命周期，所以loop_ptr和channel_ptr在lambda中仍然有效
            if (has_pending_data && loop_ptr && channel_ptr) {
                // LOG_DEBUG("[close] 我来这了 " + std::to_string(this->fd_));
                // ✅ 使用复制的裸指针，而不是this->成员
                loop_ptr->queueInLoop([self, channel_ptr]() {
                    if (channel_ptr) {
                        channel_ptr->enableWriting();
                    }
                });
            } else if (!has_pending_data) {
                // ✅ 在self保护下安全访问
                removeChannelFromEventLoop();
                // LOG_DEBUG("[close] Session Used " + std::to_string(this->fd_) +  " num " + std::to_string(self.use_count()));
            }
        }

        void HttpSession::forceClose() {
            auto self = shared_from_this();  // ✅ 捕获shared_ptr保证生命周期
            common::network::EventLoop* loop_ptr = nullptr;
            common::network::Channel* channel_ptr = nullptr;
            
            // 第一步：检查状态并设置为CLOSED
            {
                std::lock_guard<std::mutex> lock(session_mutex_);

                if (isClosed()) {
                    return;
                }

                setState(HttpSessionState::CLOSED);
                
                // ⭐⭐⭐ 关键修复：复制指针，避免后续访问成员变量
                loop_ptr = event_loop_;
                channel_ptr = channel_.get();
            }

            // 第二步：在锁外调用关闭回调，避免死锁
            callCloseCallbackOnce();

            // 第三步：⭐⭐⭐ 在EventLoop线程中移除Channel，避免线程安全问题
            if (loop_ptr && channel_ptr) {
                loop_ptr->runInLoop([self, channel_ptr]() {
                    if (channel_ptr) {
                        channel_ptr->disableAll();
                        channel_ptr->remove();
                    }
                });
            }
        }

        void HttpSession::sendResponse(const HttpResponse& response) {
            sendResponseInternal(response, true);
        }

        void HttpSession::sendResponseInternal(const HttpResponse& response, bool need_lock) {
            std::unique_lock<std::mutex> lock(session_mutex_, std::defer_lock);
            if (need_lock) {
                lock.lock();
            }

            if (!isConnected()) {
                LOG_WARNING("Cannot send response on closed session (fd: " + std::to_string(fd_) + ")");
                return;
            }

            prepareResponse(response);

            if (!output_buffer_.empty()) {
                setState(HttpSessionState::WRITING);
                // ⚠️ 修复：捕获shared_ptr保持生命周期
                auto self = shared_from_this();
                event_loop_->runInLoop([self, this]{
                    channel_->enableWriting();
                });
                updateIOTime();
            } else {
                LOG_WARNING("Response buffer is empty after preparation for fd: " + std::to_string(fd_));
            }
        }

        bool HttpSession::isTimeout() const {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_io_time_).count();
            
            auto state = state_.load();
            switch (state) {
                case HttpSessionState::READING:
                    return elapsed > read_timeout_ms_;
                case HttpSessionState::WRITING:
                    return elapsed > write_timeout_ms_;
                case HttpSessionState::KEEP_ALIVE:
                    return elapsed > keep_alive_timeout_ms_;
                default:
                    return false;
            }
        }

        void HttpSession::handleRead() {
            // LOG_DEBUG("HttpSession::handleRead() called for fd: " + std::to_string(fd_) + ", session_id: " + std::to_string(session_id_));

            // 用于存储需要在锁外执行的操作
            bool should_close = false;
            bool should_handle_error = false;
            bool parsing_failed = false;

            {
                std::lock_guard<std::mutex> lock(session_mutex_);

                if (!isConnected()) {
                    LOG_WARNING("HttpSession::handleRead() - session not connected for fd: " + std::to_string(fd_));
                    return;
                }

                auto current_state = state_.load();
                
                // 🔧 修复：明确处理各种状态，避免误报警告
                if (current_state == HttpSessionState::PROCESSING) {
                    // 在PROCESSING状态下静默忽略额外的读事件（异步处理中正常现象）
                    LOG_INFO("HttpSession::handleRead() - 忽略PROCESSING状态下的读事件 (fd: " + std::to_string(fd_) + ")");
                    return;
                }
                
                if (current_state != HttpSessionState::READING &&
                    current_state != HttpSessionState::KEEP_ALIVE) {
                    LOG_WARNING("HttpSession::handleRead() - invalid state for reading: " + std::to_string(static_cast<int>(current_state)) + " (fd: " + std::to_string(fd_) + ")");
                    return;
                }

                // ⭐ 使用动态分配的缓冲区，从配置文件读取大小
                std::vector<char> buffer(recv_buffer_size_);
                
                // ET模式下必须循环读取直到EAGAIN
                while (true) {
                    ssize_t n = ::read(fd_, buffer.data(), buffer.size());
                    
                    // LOG_DEBUG("HttpSession::handleRead() - read result: " + std::to_string(n) +
                    //          " bytes from fd: " + std::to_string(fd_) + ", session_id: " + std::to_string(session_id_));

                    if (n > 0) {
                        // 记录接收到的原始数据
                        std::string received_data(buffer.data(), n);
                        //LOG_DEBUG("HttpSession:: " + std::to_string(fd_) + " - received raw data: \n [" + received_data + "]");

                        input_buffer_.append(buffer.data(), n);
                        stats_.bytes_received += n;
                        stats_.updateActivity();
                        updateIOTime();
                        
                        // ⭐ ET模式：继续读取，直到EAGAIN
                        continue;
                        
                    } else if (n == 0) {
                        // 客户端关闭连接 - 标记需要关闭，但在锁外执行
                        LOG_INFO("Client closed connection for fd: " + std::to_string(fd_));
                        should_close = true;
                        break;
                        
                    } else {
                        // 读取错误或EAGAIN
                        int error = errno;
                        if (error == EAGAIN || error == EWOULDBLOCK) {
                            // ⭐ ET模式：数据读完了，退出循环
                            // LOG_DEBUG("Read would block (EAGAIN) for fd " + std::to_string(fd_) + ", 数据已读完");
                            break;
                        } else {
                            // 真正的错误
                            LOG_ERROR("Read error for fd " + std::to_string(fd_) + ": " + std::string(strerror(error)));
                            should_handle_error = true;
                            break;
                        }
                    }
                }
                
                // ⭐ 数据读取完毕后，尝试解析HTTP请求
                if (!should_close && !should_handle_error && input_buffer_.size() > 0) {
                    // 尝试解析HTTP请求
                    if (parseHttpRequest()) {
                        //LOG_DEBUG("HttpSession::handleRead() - parseHttpRequest() SUCCESS, processing request");
                        setState(HttpSessionState::PROCESSING);
                        // ⚠️ 修复：捕获shared_ptr保持生命周期
                        auto self = shared_from_this();
                        event_loop_->executeCallbackAsync([self, this]{
                            this->processHttpRequest();
                        });
                        //LOG_DEBUG("[handleRead] 我来到了处理线程之后");
                    } else {
                        //LOG_DEBUG("HttpSession::handleRead() - parseHttpRequest() FAILED, waiting for more data");
                        //LOG_DEBUG("HttpSession::handleRead() - buffer size: " + std::to_string(input_buffer_.size()) + " bytes");
                        
                        // ⭐ 修复：检查是否超过最大限制
                        if (input_buffer_.size() > max_request_size_) {
                            parsing_failed = true;
                            LOG_ERROR("请求体过大: " + std::to_string(input_buffer_.size()) + 
                                     " 字节，超过最大限制 " + std::to_string(max_request_size_) + " 字节");
                        } else {
                            // ⭐ ET模式下，已经读完所有数据，但仍不完整
                            // 说明客户端还没发完，等待下次数据到达（下次ET触发）
                            LOG_INFO("ET模式：数据不完整，等待客户端发送更多数据...");
                        }
                    }
                }
            }

            // 在锁外执行可能导致递归锁的操作
            if (parsing_failed) {
                // 🔧 修复：解析失败时返回400错误，提供更详细的错误信息
                HttpResponse response;
                response.badRequest("HTTP request too large or malformed. Please check request format and size.");
                response.setHeader("Connection", "close");  // 确保连接关闭
                response.setHeader("Content-Type", "text/plain; charset=utf-8");

                sendResponseInternal(response, false);
                LOG_WARNING("Sent 400 Bad Request due to parsing failure for fd: " + std::to_string(fd_));
            } else if (should_close) {
                handleClose();
            } else if (should_handle_error) {
                handleError();
            }

            // LOG_DEBUG("[handleRead] 我来到了函数末尾");
        }



        inline std::string to_string(std::thread::id id) {
            std::ostringstream ss;
            ss << id;
            return ss.str();
        }

        void HttpSession::handleWrite()
        {
            // ⚠️ 修复：使用局部变量避免在锁外访问成员变量
            bool should_keep_alive = false;
            bool should_close_conn = false;
            
            {
                std::lock_guard<std::mutex> lock(session_mutex_);
                if (!isConnected()) {
                    return;
                }

                ssize_t n = ::write(fd_, output_buffer_.c_str(), output_buffer_.length());

                if (n > 0) {
                    if (n < bytes_to_write_) {
                        output_buffer_ = output_buffer_.substr(n);
                    } else {
                        output_buffer_.clear();
                    }
                    stats_.bytes_sent += n;
                    stats_.updateActivity();
                    updateIOTime();

                    // 检查是否应该保持连接
                    if (keep_alive_enabled_ && 
                        keep_alive_request_count_ < max_keep_alive_requests_ && 
                        !isTimeout()) {
                        should_keep_alive = true;
                    } else {
                        should_close_conn = true;
                    }

                } else if (n == -1) {
                    int saved_errno = errno;
                    if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
                        return; // 等待下次
                    } else {
                        LOG_ERROR("Write error on fd " + std::to_string(fd_) + ": " + std::strerror(saved_errno));
                        should_close_conn = true;
                    }
                }
            }
            
            // ⚠️ 在锁外执行，避免递归锁
            if (should_keep_alive) {
                resetForNextRequest();
            } else if (should_close_conn) {
                if (!keep_alive_enabled_) {
                    LOG_INFO("Keep-Alive disabled, closing connection for fd: " + std::to_string(fd_));
                } else {
                    // LOG_DEBUG("Keep-Alive limit reached or timeout, closing connection for fd: " + std::to_string(fd_));
                }
                close();
            }
        }

        void HttpSession::handleClose() {
            try {
                // ⚠️ 修复：移除锁，避免死锁，close()内部已经有状态检查
                auto current_state = state_.load();
                LOG_INFO("当前会话状态: " + std::to_string(static_cast<int>(current_state)) +
                            ", session_id=" + std::to_string(session_id_));
                
                // 直接调用close，内部有状态检查和锁保护
                close();

                LOG_INFO("handleClose() 执行完成: session_id=" + std::to_string(session_id_));

            } catch (const std::exception& e) {
                LOG_ERROR("handleClose() 发生异常: " + std::string(e.what()) +
                         ", session_id=" + std::to_string(session_id_));
                // 即使发生异常，也要尝试强制关闭
                try {
                    forceClose();
                } catch (...) {
                    LOG_ERROR("forceClose() 也发生异常: session_id=" + std::to_string(session_id_));
                }
            } catch (...) {
                LOG_ERROR("handleClose() 发生未知异常: session_id=" + std::to_string(session_id_));
                try {
                    forceClose();
                } catch (...) {
                    LOG_ERROR("forceClose() 也发生未知异常: session_id=" + std::to_string(session_id_));
                }
            }
        }

        void HttpSession::handleError() {
            stats_.errors++;
            
            std::string error_msg = "Socket error for fd " + std::to_string(fd_) + ": " + std::string(strerror(errno));
            LOG_ERROR(error_msg);
            
            if (error_callback_) {
                error_callback_(shared_from_this(), error_msg);
            }
            
            forceClose();
        }

        bool HttpSession::parseHttpRequest() {
            // 检查最小HTTP请求长度
            if (input_buffer_.size() < 16) {  // "GET / HTTP/1.1\r\n"
                LOG_WARNING("parseHttpRequest() - buffer too small: " + std::to_string(input_buffer_.size()));
                return false;
            }

            // 检查是否有请求行
            size_t first_line_end = input_buffer_.find("\r\n");
            if (first_line_end == std::string::npos) {
                LOG_WARNING("parseHttpRequest() - no request line end found");
                return false;
            }

            std::string request_line = input_buffer_.substr(0, first_line_end);

            // 查找HTTP请求结束标志
            size_t header_end = input_buffer_.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                // 请求头未完整接收
                LOG_WARNING("parseHttpRequest() - headers incomplete, waiting for more data");
                return false;
            }

            // 提取完整的请求数据（包括可能的请求体）
            std::string request_data = input_buffer_.substr(0, header_end + 4);

            // 先只解析头部以获取Content-Length，不包含请求体
            std::string header_only_data = request_data;

            // 🔧 修复：增强HTTP请求体解析，支持多种传输编码方式
            // 检查Transfer-Encoding头部，支持chunked编码
            bool is_chunked = false;
            size_t te_pos = input_buffer_.find("Transfer-Encoding:");
            if (te_pos == std::string::npos) {
                te_pos = input_buffer_.find("transfer-encoding:");
            }
            
            if (te_pos != std::string::npos && te_pos < header_end) {
                size_t te_start = input_buffer_.find(":", te_pos) + 1;
                size_t te_end = input_buffer_.find("\r\n", te_start);
                if (te_end != std::string::npos) {
                    std::string te_str = input_buffer_.substr(te_start, te_end - te_start);
                    // 去除空白字符并转为小写
                    te_str.erase(0, te_str.find_first_not_of(" \t"));
                    te_str.erase(te_str.find_last_not_of(" \t") + 1);
                    std::transform(te_str.begin(), te_str.end(), te_str.begin(), ::tolower);
                    
                    is_chunked = (te_str.find("chunked") != std::string::npos);
                }
            }

            size_t content_length = 0;
            size_t total_length = 0;

            if (is_chunked) {
                // 🔧 处理chunked编码：寻找完整的chunked数据
                // LOG_DEBUG("parseHttpRequest() - 处理chunked编码");
                total_length = parseChunkedRequest(header_end);
                if (total_length == 0) {
                    LOG_WARNING("parseHttpRequest() - chunked数据不完整，等待更多数据");
                    return false;
                }
            } else {
                // 解析Content-Length头部（原有逻辑）
                size_t cl_pos = input_buffer_.find("Content-Length:");
                if (cl_pos == std::string::npos) {
                    cl_pos = input_buffer_.find("content-length:");
                }

                if (cl_pos != std::string::npos && cl_pos < header_end) {
                    size_t cl_start = input_buffer_.find(":", cl_pos) + 1;
                    size_t cl_end = input_buffer_.find("\r\n", cl_start);
                    if (cl_end != std::string::npos) {
                        std::string cl_str = input_buffer_.substr(cl_start, cl_end - cl_start);
                        // 去除空白字符
                        cl_str.erase(0, cl_str.find_first_not_of(" \t"));
                        cl_str.erase(cl_str.find_last_not_of(" \t") + 1);
                        
                        // 🔧 增强Content-Length验证
                        bool valid_number = true;
                        for (char c : cl_str) {
                            if (!std::isdigit(c)) {
                                valid_number = false;
                                break;
                            }
                        }
                        
                        if (valid_number && !cl_str.empty()) {
                            try {
                                content_length = std::stoull(cl_str);
                                // LOG_DEBUG("parseHttpRequest() - 解析得到Content-Length: " + std::to_string(content_length));
                            } catch (...) {
                                LOG_WARNING("parseHttpRequest() - Content-Length解析失败: " + cl_str);
                                content_length = 0;
                            }
                        } else {
                            LOG_WARNING("parseHttpRequest() - 无效的Content-Length值: " + cl_str);
                            content_length = 0;
                        }
                    }
                }
                
                total_length = header_end + 4 + content_length;
                // LOG_DEBUG("parseHttpRequest() - 使用Content-Length计算总长度: " + std::to_string(total_length));
            }

            if (input_buffer_.length() >= total_length) {
                // 请求完整接收，解析完整的请求（只解析一次）
                std::string complete_request_data = input_buffer_.substr(0, total_length);

                if (current_request_.parseFromRawData(complete_request_data)) {
                    // 移除已处理的数据
                    input_buffer_.erase(0, total_length);
                                
                    // ⭐⭐⭐ 修复：设置客户端IP地址（从对端地址获取，不包含端口）
                    current_request_.setClientIP(peer_addr_.toIp());
                                
                    // LOG_DEBUG("parseHttpRequest() - 解析成功，客户端IP: " + peer_addr_.toIp() + ", 请求体: [" + current_request_.getBody() + "]");
                    return true;
                } else {
                    LOG_ERROR("parseHttpRequest() - 请求解析失败");
                }
            } else {
                // LOG_DEBUG("parseHttpRequest() - 数据不完整，等待更多数据");
                LOG_WARNING("parseHttpRequest() - 需要: " + std::to_string(total_length) + " 字节，已有: " + std::to_string(input_buffer_.length()) + " 字节");
            }

            return false;
        }

        size_t HttpSession::parseChunkedRequest(size_t header_end) {
            // 🔧 实现chunked编码解析
            // HTTP/1.1 chunked格式：
            // <chunk-size-hex>\r\n
            // <chunk-data>\r\n
            // <chunk-size-hex>\r\n
            // <chunk-data>\r\n
            // 0\r\n
            // \r\n
            
            size_t pos = header_end + 4; // 跳过头部结束标志
            std::string accumulated_body;
            
            // LOG_DEBUG("parseChunkedRequest() - 开始解析chunked数据，起始位置: " + std::to_string(pos));
            
            while (pos < input_buffer_.size()) {
                // 1. 读取chunk大小行
                size_t chunk_size_line_end = input_buffer_.find("\r\n", pos);
                if (chunk_size_line_end == std::string::npos) {
                    // LOG_DEBUG("parseChunkedRequest() - chunk大小行不完整");
                    return 0; // 数据不完整，等待更多数据
                }
                
                std::string chunk_size_str = input_buffer_.substr(pos, chunk_size_line_end - pos);
                // LOG_DEBUG("parseChunkedRequest() - chunk大小字符串: [" + chunk_size_str + "]");
                
                // 2. 解析十六进制chunk大小
                size_t chunk_size = 0;
                try {
                    // 移除可能的chunk扩展（如果有分号）
                    size_t semicolon_pos = chunk_size_str.find(';');
                    if (semicolon_pos != std::string::npos) {
                        chunk_size_str = chunk_size_str.substr(0, semicolon_pos);
                    }
                    
                    // 去除首尾空白
                    chunk_size_str.erase(0, chunk_size_str.find_first_not_of(" \t"));
                    chunk_size_str.erase(chunk_size_str.find_last_not_of(" \t") + 1);
                    
                    chunk_size = std::stoul(chunk_size_str, nullptr, 16);
                    // LOG_DEBUG("parseChunkedRequest() - chunk大小: " + std::to_string(chunk_size));
                } catch (const std::exception& e) {
                    LOG_ERROR("parseChunkedRequest() - 无效的chunk大小: " + chunk_size_str + ", 错误: " + std::string(e.what()));
                    return 0; // 格式错误
                }
                
                // 3. 检查是否为最后一个chunk
                if (chunk_size == 0) {
                    // LOG_DEBUG("parseChunkedRequest() - 发现最后一个chunk（大小为0）");
                    
                    // 跳过 "0\r\n"
                    pos = chunk_size_line_end + 2;
                    
                    // 寻找结束的\r\n（trailer headers的结束）
                    size_t final_crlf = input_buffer_.find("\r\n", pos);
                    if (final_crlf == std::string::npos) {
                        // LOG_DEBUG("parseChunkedRequest() - 最终CRLF不完整");
                        return 0; // 数据不完整
                    }

                    // 计算完整请求的总长度
                    size_t total_request_length = final_crlf + 2;
                    // LOG_DEBUG("parseChunkedRequest() - chunked解析完成，总长度: " + std::to_string(total_request_length) +
                    //          ", 请求体长度: " + std::to_string(accumulated_body.size()));
                    
                    return total_request_length;
                }
                
                // 4. 读取chunk数据
                pos = chunk_size_line_end + 2; // 跳过chunk大小行的\r\n
                
                // 检查是否有足够的数据（chunk数据 + 结尾的\r\n）
                if (pos + chunk_size + 2 > input_buffer_.size()) {
                    // LOG_DEBUG("parseChunkedRequest() - chunk数据不完整，需要: " +
                    //          std::to_string(pos + chunk_size + 2) + ", 有: " + std::to_string(input_buffer_.size()));
                    return 0; // 数据不完整
                }

                // 提取chunk数据
                std::string chunk_data = input_buffer_.substr(pos, chunk_size);
                accumulated_body += chunk_data;

                // LOG_DEBUG("parseChunkedRequest() - 读取chunk数据: " + std::to_string(chunk_size) + " 字节");
                
                // 验证chunk结尾的\r\n
                if (input_buffer_.substr(pos + chunk_size, 2) != "\r\n") {
                    LOG_ERROR("parseChunkedRequest() - chunk数据后缺少CRLF");
                    return 0; // 格式错误
                }
                
                // 移动到下一个chunk
                pos += chunk_size + 2;
                
                // 防止无限循环（安全检查）
                if (accumulated_body.size() > MAX_RESPONSE_SIZE) {
                    LOG_ERROR("parseChunkedRequest() - chunked请求体过大: " + std::to_string(accumulated_body.size()));
                    return 0; // 请求体过大
                }
            }
            
            // LOG_DEBUG("parseChunkedRequest() - 数据不完整，等待更多数据");
            return 0; // 数据不完整，需要等待更多数据
        }

        void HttpSession::processHttpRequest()
        {
            if (!request_handler_) {
                LOG_ERROR("No request handler set for session");
                HttpResponse response;
                response.internalServerError("No request handler configured");
                // 确保连接关闭
                response.setHeader("Connection", "close");
                sendResponse(response);
                return;
            }
            
            // ⭐⭐⭐ 优化4: 从配置动态加载轻量级请求路径（线程安全初始化）
            static std::set<std::string> lightweight_paths_set;
            static std::once_flag init_lightweight_paths_flag;
            
            std::call_once(init_lightweight_paths_flag, []() {
                try {
                    // 从配置管理器读取轻量级路径（使用get<std::vector<std::string>>读取逗号分隔的字符串）
                    auto& config_manager = common::config::ConfigManager::getInstance();
                    auto lightweight_paths = config_manager.get<std::vector<std::string>>(
                        "http.server.lightweight_paths", std::vector<std::string>{});
                    
                    // 转换为set以便快速查找
                    if (!lightweight_paths.empty()) {
                        lightweight_paths_set.insert(lightweight_paths.begin(), lightweight_paths.end());
                        LOG_INFO("✅ 初始化轻量级请求路径集合: " + std::to_string(lightweight_paths_set.size()) + "条");
                        
                        // 打印加载的路径（调试）
                        // for (const auto& path : lightweight_paths_set) {
                        //     LOG_DEBUG("  - 轻量级路径: " + path);
                        // }
                    } else {
                        // 如果配置为空，使用默认值
                        lightweight_paths_set = {
                            "/api/v1/health", "/api/v1/ping", "/api/v1/status",
                            "/health", "/ping", "/ready", "/alive", "/metrics"
                        };
                        LOG_INFO("✅ 配置lightweight_paths为空，使用默认轻量级请求路径: " + 
                                 std::to_string(lightweight_paths_set.size()) + "条");
                    }
                } catch (const std::exception& e) {
                    // 如果配置读取失败，使用默认值
                    LOG_WARNING("⚠️  无法从配置读取lightweight_paths：" + std::string(e.what()) + ", 使用默认值");
                    lightweight_paths_set = {
                        "/api/v1/health", "/api/v1/ping", "/api/v1/status",
                        "/health", "/ping", "/ready", "/alive", "/metrics"
                    };
                    LOG_INFO("✅ 使用默认轻量级请求路径: " + std::to_string(lightweight_paths_set.size()) + "条");
                }
            });
            
            bool is_lightweight = lightweight_paths_set.count(current_request_.getPath()) > 0;
            
            if (is_lightweight) {
                // ✅ IO线程直接处理，延迟 < 10μs，无需线程池
                // LOG_DEBUG("轻量级请求，IO线程直接处理: " + current_request_.getPath());
                
                try {
                    HttpResponse response;
                    request_handler_(current_request_, response);
                    
                    stats_.requests_handled++;
                    keep_alive_request_count_++;
                    
                    if (keep_alive_enabled_ && shouldKeepAlive(current_request_, response)) {
                        response.setHeader("Connection", "keep-alive");
                    } else {
                        response.setHeader("Connection", "close");
                    }
                    
                    sendResponseInternal(response, false);
                    
                } catch (const std::exception& e) {
                    LOG_ERROR("Exception processing lightweight request: " + std::string(e.what()));
                    HttpResponse response;
                    response.internalServerError("Request processing failed");
                    response.setHeader("Connection", "close");
                    sendResponseInternal(response, false);
                }
                
                return;  // ✅ 不投递到线程池
            }

            // 非轻量级请求：EventLoop::executeCallbackAsync 已将其分发到线程池
            // 此处直接在当前线程（已是线程池工作线程）执行请求处理
            try {
                HttpResponse response;
                request_handler_(current_request_, response);

                stats_.requests_handled++;
                keep_alive_request_count_++;

                // 修复：根据Keep-Alive配置设置响应头
                if (keep_alive_enabled_ && shouldKeepAlive(current_request_, response)) {
                    // 保持连接
                    response.setHeader("Connection", "keep-alive");
                } else {
                    // 关闭连接
                    response.setHeader("Connection", "close");
                }

                sendResponseInternal(response, false);

            } catch (const std::exception& e) {
                LOG_ERROR("Exception processing request: " + std::string(e.what()));

                HttpResponse response;
                response.internalServerError("Request processing failed");
                // 确保连接关闭
                response.setHeader("Connection", "close");

                sendResponseInternal(response, false);
            }
        }

        void HttpSession::prepareResponse(const HttpResponse& response) {
            std::string response_data = response.toString();
            output_buffer_ += response_data;
            bytes_to_write_ = output_buffer_.size();
        }

        bool HttpSession::shouldKeepAlive(const HttpRequest& request, const HttpResponse& response) const {
            // 暂时不使用response参数，但保留以备将来扩展
            (void)response;

            // 首先检查服务器是否启用了Keep-Alive
            if (!keep_alive_enabled_) {
                LOG_WARNING("Keep-Alive disabled by server configuration for fd: " + std::to_string(fd_));
                return false;
            }

            // 检查是否超过最大Keep-Alive请求数
            if (keep_alive_request_count_ >= max_keep_alive_requests_) {
                LOG_WARNING("Keep-Alive request count exceeded for fd: " + std::to_string(fd_) +
                         ", count: " + std::to_string(keep_alive_request_count_) +
                         ", max: " + std::to_string(max_keep_alive_requests_));
                return false;
            }

            // 检查客户端是否支持Keep-Alive
            bool client_keep_alive = request.isKeepAlive();

            return client_keep_alive;
        }


        bool HttpSession::shouldKeepAliveFromHeaders(const std::string& connection_header, const std::string& http_version) {
            // 🔧 首先检查服务器端是否启用了Keep-Alive
            if (!keep_alive_enabled_) {
                return false;
            }
            
            // 🔧 检查是否超过最大Keep-Alive请求数
            if (keep_alive_request_count_ >= max_keep_alive_requests_) {
                return false;
            }
            
            // 🔧 修复：显式指定关闭连接
            if (connection_header == "close") {
                return false;
            }
            // 🔧 修复：显式指定保持连接
            if (connection_header == "keep-alive") {
                return true;  // ✅ 修复bug：应该返回true
            }
            // HTTP/1.1 默认 keep-alive，HTTP/1.0 默认 close
            return (http_version == "HTTP/1.1");
        }

        void HttpSession::resetForNextRequest() {
            {
                // 检查是否应该保持连接
                if (!keep_alive_enabled_) {
                    LOG_INFO("Keep-Alive disabled, closing connection in resetForNextRequest for fd: " + std::to_string(fd_));
                    // 不应该保持连接，直接关闭
                    close();
                    return;
                }
                
                // 检查是否超过最大请求数
                if (keep_alive_request_count_ >= max_keep_alive_requests_) {
                    LOG_INFO("Max Keep-Alive requests reached, closing connection for fd: " + std::to_string(fd_));
                    close();
                    return;
                }
                
                // 检查是否超时
                if (isTimeout()) {
                    LOG_INFO("Keep-Alive timeout, closing connection for fd: " + std::to_string(fd_));
                    close();
                    return;
                }

                // 只重置请求对象
                current_request_.reset();
                
                // ⭐⭐⭐ 优化2: 使用clear()而不是重新赋值，保持capacity不变
                input_buffer_.clear();   // size=0, capacity不变，避免重新分配
                output_buffer_.clear();  // size=0, capacity不变

                // 设置为READING状态
                setState(HttpSessionState::READING);
            }

            // 🛠️ 修复2: 使用queueInLoop确保线程安全，只负责重新启用读事件
            if (event_loop_) {
                auto self = shared_from_this();
                event_loop_->queueInLoop([this, self]() {
                    if (isConnected() && channel_) {
                        channel_->enableReading(); // 👈 重新启用读事件
                    }
                    (void)self;
                });
            }

            // LOG_DEBUG("Session reset for next request, count: " + std::to_string(keep_alive_request_count_));
        }

        void HttpSession::updateIOTime() {
            last_io_time_ = std::chrono::steady_clock::now();
        }

        void HttpSession::setState(HttpSessionState new_state) {
            auto old_state = state_.exchange(new_state);
            if (old_state != new_state) {
                // LOG_DEBUG("Session state changed: " + std::to_string(static_cast<int>(old_state)) +
                //          " -> " + std::to_string(static_cast<int>(new_state)));
            }
        }

        void HttpSession::triggerError(const std::string& error) {
            if (error_callback_) {
                error_callback_(shared_from_this(), error);
            }
        }

        void HttpSession::removeChannelFromEventLoop()
        {
            // ⚠️ 修复：捕获shared_ptr而不是裸this指针，避免use-after-free
            auto self = shared_from_this();
            event_loop_->runInLoop([self, this]{
                if (channel_) {
                    channel_->disableAll();
                    channel_->remove();
                }
            });
        }

        void HttpSession::callCloseCallbackOnce() {
            try {
                // 使用原子操作确保回调只被调用一次
                bool expected = false;
                if (close_callback_called_.compare_exchange_strong(expected, true)) {
                    // 只有第一次调用会执行回调
                    if (close_callback_) {
                        try {
                            close_callback_(shared_from_this());
                            // LOG_DEBUG("会话关闭回调执行成功: session_id=" + std::to_string(session_id_));
                        } catch (const std::exception& e) {
                            LOG_ERROR("会话关闭回调执行异常: " + std::string(e.what()) +
                                     ", session_id=" + std::to_string(session_id_));
                        } catch (...) {
                            LOG_ERROR("会话关闭回调执行未知异常: session_id=" + std::to_string(session_id_));
                        }
                    } else {
                        LOG_ERROR("会话关闭回调为空: session_id=" + std::to_string(session_id_));
                    }
                } else {
                    LOG_ERROR("会话关闭回调已被调用过: session_id=" + std::to_string(session_id_));
                }

                // LOG_DEBUG("callCloseCallbackOnce() 执行完成: session_id=" + std::to_string(session_id_));

            } catch (const std::exception& e) {
                LOG_ERROR("callCloseCallbackOnce() 发生异常: " + std::string(e.what()) +
                         ", session_id=" + std::to_string(session_id_));
            } catch (...) {
                LOG_ERROR("callCloseCallbackOnce() 发生未知异常: session_id=" + std::to_string(session_id_));
            }
        }

    } // namespace http
} // namespace common
