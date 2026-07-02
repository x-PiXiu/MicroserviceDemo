# 一个 HTTP 请求的完整调用链

> 以 auth_service 的登录接口 `POST /api/v1/auth/login` 为例
> 从客户端发起 TCP 连接到收到 HTTP 响应，精确到 epoll 系统调用和源码行号

---

## 前置：服务器启动（只执行一次）

```
main.cpp:526                   g_auth_service->start()
auth_service.cpp:936           initializeHttpServer()
  ├─ socket.cpp:240              ::bind(fd, addr)           ← 系统调用：绑定端口 8083
  ├─ socket.cpp:258              ::listen(fd, 65535)        ← 系统调用：开始监听
  ├─ epoll.cpp:47                epoll_create1()            ← 系统调用：创建 epoll 实例
  ├─ epoll.cpp:268               epoll_ctl(ADD, listen_fd)  ← 系统调用：注册监听 socket
  └─ event_loop.cpp:217          EventLoop::loop()          ← 进入事件循环（阻塞）
       └─ epoll.cpp:163          ::epoll_wait(...)          ← 系统调用：等待事件（阻塞在此）
```

---

## Phase 1：客户端建立 TCP 连接

```
客户端发起 connect() → 三次握手完成 → 监听 socket 变为可读
```

### 调用链

```
epoll.cpp:163     ::epoll_wait() 返回，events 中包含 listen_fd 的 EPOLLIN 事件
  ↓
epoll.cpp:250     fillActiveChannels()
                  → channel->setRevents(EPOLLIN)    设置触发的事件类型
                  → activeChannels.push_back(channel) 放入活跃列表
  ↓
event_loop.cpp:238    channel->handleEvent()
  ↓
channel.cpp:94    检查 revents_ & EPOLLIN 为真 → 调用 readCallback_()
  ↓
http_server.cpp:645   handleNewConnection()
  ↓
socket.cpp:291    int client_fd = ::accept4(listen_fd, &addr, SOCK_NONBLOCK)
                  ← 系统调用：accept4() 接受新连接，返回客户端 fd
  ↓
http_server.cpp:680   fcntl(client_fd, F_SETFL, O_NONBLOCK)   设置非阻塞
  ↓
http_server.cpp:684   创建 HttpSession(client_fd, client_addr, event_loop)
                      → 设置 readCallback = handleRead
                      → 设置 writeCallback = handleWrite
                      → 设置 closeCallback = cleanupSession
  ↓
http_server.cpp:701   session->setRequestHandler(handleHttpRequest)
                      → 绑定业务处理回调
  ↓
http_session.cpp:85   session->start()
  ↓
channel.cpp:55    channel_->enableReading()  → events_ |= EPOLLIN → update()
  ↓
epoll.cpp:268     ::epoll_ctl(EPOLL_CTL_ADD, client_fd, EPOLLIN)
                  ← 系统调用：将客户端 fd 注册到 epoll，监听可读事件
```

**此时状态**：客户端 fd 已注册到 epoll，等待客户端发送数据。EventLoop 回到 `epoll_wait()` 阻塞。

---

## Phase 2：客户端发送 HTTP 请求，读取数据

```
客户端发送 "POST /api/v1/auth/login HTTP/1.1\r\n..." → 网络 → 客户端 fd 变为可读
```

### 调用链

```
epoll.cpp:163     ::epoll_wait() 返回，events 中包含 client_fd 的 EPOLLIN 事件
  ↓
event_loop.cpp:238    channel->handleEvent()
  ↓
channel.cpp:97    readCallback_() → 触发 HttpSession::handleRead()
  ↓
http_session.cpp:255   ssize_t n = ::read(fd_, buffer, 65536)
                       ← 系统调用：read() 从 socket 读取数据
                       ← ET 模式：循环 read 直到返回 EAGAIN
  ↓
http_session.cpp:265   input_buffer_.append(data, n)
                       ← 将读到的数据追加到 input_buffer_
  ↓
http_session.cpp:298   parseHttpRequest()
                       ← 尝试从 input_buffer_ 解析完整 HTTP 请求
```

### HTTP 解析细节

```
http_session.cpp:461   查找 "\r\n" → 提取请求行
http_session.cpp:470   查找 "\r\n\r\n" → 确定头部结束位置
http_session.cpp:518   读取 Content-Length 或 Transfer-Encoding: chunked
                       → 判断请求体是否完整
  ↓
http_request.cpp:151   parseRequestLine("POST /api/v1/auth/login HTTP/1.1")
                       → 解析出 method=POST, path=/api/v1/auth/login, version=HTTP/1.1
  ↓
http_request.cpp:161   parseHeaders()
                       → 解析 Content-Type: application/json
                       → 解析 Content-Length: 45
                       → 解析 Connection: keep-alive
  ↓
http_request.cpp:169   parseQueryParams()  (本例无 query string)
http_request.cpp:176   parseCookies()      (本例无 Cookie)
  ↓
http_request.cpp:187   提取 body: {"username":"cdp","password":"123456"}
```

**此时状态**：HTTP 请求已完整解析到 `current_request_` 对象中。

---

## Phase 3：分发到线程池

### 调用链

```
http_session.cpp:303   event_loop_->executeCallbackAsync([self, this]{
                           this->processHttpRequest();
                       });
  ↓
event_loop.cpp:1474    executeCallbackAsync()
  ↓
event_loop.cpp:1475    检查 config_.enable_thread_pool && thread_pool_
  ↓
event_loop.cpp:1477    thread_pool_->submit(callback)
                       ← 将 processHttpRequest 提交到线程池
                       ← IO 线程立即返回，继续 epoll_wait() 处理其他连接
```

**此时状态**：IO 线程回到 `epoll_wait()` 继续处理其他连接。Worker 线程接管后续业务逻辑。

---

## Phase 4：Worker 线程执行业务逻辑

```
[Worker 线程上下文，非 IO 线程]
```

### 调用链

```
http_session.cpp:742   processHttpRequest()
                      → 检查是否为轻量级请求（health check 等）
                      → 登录请求不是轻量级，继续走业务流程
  ↓
http_session.cpp:776   request_handler_(current_request_, response)
                      → 调用 HttpServer::handleHttpRequest()
  ↓
http_server.cpp:746    handleHttpRequest()
  ↓
http_server.cpp:762    middleware_manager_->execute(request, response)
                      → 执行中间件链：
                        1. CORS 中间件：添加 Access-Control-Allow-Origin
                        2. JWT 认证中间件：检查路由是否需要认证（login 不需要）
                        3. 请求日志中间件：记录请求方法和路径
  ↓
http_server.cpp:800    processRequest(request, response)
  ↓
http_server.cpp:802    router_->match("POST", "/api/v1/auth/login")
                      → 路由匹配，找到注册的 handler
                      → 提取路径参数（本例无参数）
  ↓
http_server.cpp:811    match.handler->handle(request, response)
                      → 调用 AuthService::handleLoginRequest()
```

### 登录业务逻辑

```
auth_service.cpp:1469   handleLoginRequest()
  ↓
auth_service.cpp:1469   解析 JSON body → 提取 username="cdp", password="123456"
  ↓
auth_service.cpp:1573   user_service_client_->getUserByUsername("cdp")
                       → HTTP GET http://127.0.0.1:8082/api/v1/user/cdp
                       → [跨服务调用] 请求 user_service 查询用户信息
                       → 返回: {userId: "usr_xxx", passwordHash: "$2a$10$xxx"}
  ↓
auth_service.cpp:1582   password_manager_->verifyPassword("123456", "$2a$10$xxx")
                       → BCrypt 哈希比对（CPU 密集，~100ms）
  ↓
[密码验证通过]
  ↓
auth_service.cpp:1614   jwt_manager_->generateAccessToken(userId, role)
                       → 签发 JWT Token（HS256 算法）
                       → payload: {userId, role, exp, iat, jti}
  ↓
auth_service.cpp:1624   response.setStatus(200)
                       response.setHeader("Content-Type", "application/json")
                       response.setBody({"accessToken": "eyJ...", "refreshToken": "..."})
```

**此时状态**：业务逻辑执行完毕，HttpResponse 对象已填充好。

---

## Phase 5：发送响应

### 调用链

```
http_session.cpp:172   sendResponse(response)
  ↓
http_session.cpp:176   sendResponseInternal(response, true)
  ↓
http_session.cpp:187   prepareResponse(response)
  ↓
http_session.cpp:805   std::string data = response.toString()
                       → 序列化为原始 HTTP 响应报文：

                         HTTP/1.1 200 OK\r\n
                         Content-Type: application/json\r\n
                         Content-Length: 256\r\n
                         Connection: keep-alive\r\n
                         \r\n
                         {"accessToken":"eyJ...","refreshToken":"..."}
  ↓
http_session.cpp:806   output_buffer_ += data
                       ← 序列化后的响应追加到输出缓冲区
  ↓
http_session.cpp:193   event_loop_->runInLoop([...]{
                           channel_->enableWriting();
                       })
                       ← 向 IO 线程提交任务：注册可写事件
                       ← 注意：这里从 Worker 线程跨到 IO 线程
  ↓
event_loop.cpp:        runInLoop() → queueInLoop() → wakeup()
                       → 写入 eventfd 唤醒 IO 线程
```

### IO 线程被唤醒，注册写事件

```
[IO 线程上下文]
  ↓
event_loop.cpp:258   doPendingFunctors()
                     → 执行刚才 runInLoop 提交的 lambda
  ↓
channel.cpp:60       channel_->enableWriting()
                     → events_ |= EPOLLOUT → update()
  ↓
epoll.cpp:268        ::epoll_ctl(EPOLL_CTL_MOD, client_fd, EPOLLIN|EPOLLOUT)
                     ← 系统调用：修改 epoll 监听，新增可写事件
  ↓
event_loop.cpp:217   回到 epoll_wait()
```

### 写事件触发，发送数据

```
epoll.cpp:163        ::epoll_wait() 返回，events 中包含 client_fd 的 EPOLLOUT 事件
  ↓
event_loop.cpp:238   channel->handleEvent()
  ↓
channel.cpp:100      writeCallback_() → HttpSession::handleWrite()
  ↓
http_session.cpp:364 ssize_t n = ::write(fd_, output_buffer_.data(), len)
                     ← 系统调用：write() 将响应数据写入 socket
                     ← 内核 TCP 栈将数据发送给客户端
  ↓
http_session.cpp:368 output_buffer_.clear()
                     ← 发送完毕，清空输出缓冲区
  ↓
channel.cpp:62       channel_->disableWriting()
                     → events_ &= ~EPOLLOUT → update()
  ↓
epoll.cpp:268        ::epoll_ctl(EPOLL_CTL_MOD, client_fd, EPOLLIN)
                     ← 系统调用：移除 EPOLLOUT，只保留 EPOLLIN
```

**此时状态**：HTTP 响应已发送给客户端。客户端收到 `200 OK`。

---

## Phase 6：Keep-Alive 或关闭连接

### 调用链

```
http_session.cpp:810   shouldKeepAlive(request, response)
  ↓
http_session.cpp:815   检查 keep_alive_enabled_ == true          ← 服务端启用
http_session.cpp:821   检查 keep_alive_request_count_ < max       ← 未超过上限
http_session.cpp:829   检查 request.isKeepAlive() == true         ← 客户端请求 keep-alive
  ↓
[Keep-Alive = true，保持连接]
  ↓
http_session.cpp:858   resetForNextRequest()
  ├─ current_request_.reset()         清空请求对象
  ├─ input_buffer_.clear()            清空输入缓冲区
  ├─ output_buffer_.clear()           清空输出缓冲区
  ├─ setState(HttpSessionState::READING)   状态回到 READING
  └─ channel_->enableReading()        确保 epoll 监听可读事件
  ↓
回到 Phase 2，等待客户端在这个连接上发送下一个请求
```

### 如果 Keep-Alive = false

```
http_session.cpp:90    close()
  ├─ setState(HttpSessionState::CLOSING)
  ├─ callCloseCallbackOnce() → HttpServer::cleanupSession()
  │   → 从 session_shards_ 中移除 session
  ├─ channel_->remove() → epoll_ctl(EPOLL_CTL_DEL, client_fd)  ← 从 epoll 注销
  └─ ::close(client_fd)                                        ← 关闭 socket
```

---

## 全链路系统调用汇总

| 阶段 | 系统调用 | 触发时机 |
|------|---------|---------|
| Phase 1 | `accept4()` | 监听 socket 可读（新连接到达） |
| Phase 1 | `epoll_ctl(ADD)` | 将新 client_fd 注册到 epoll |
| Phase 2 | `read()` | client_fd 可读（数据到达） |
| Phase 5 | `write(eventfd)` | Worker 线程唤醒 IO 线程 |
| Phase 5 | `epoll_ctl(MOD)` | 注册 EPOLLOUT（准备写响应） |
| Phase 5 | `write(fd)` | client_fd 可写（发送响应） |
| Phase 5 | `epoll_ctl(MOD)` | 移除 EPOLLOUT（发送完毕） |
| Phase 6 | `epoll_ctl(DEL)` + `close()` | 连接关闭 |

## 线程切换点

```
IO 线程 ──submit()──→ Worker 线程     Phase 3（线程池分发）
Worker 线程 ──runInLoop()──→ IO 线程  Phase 5（注册写事件）
```

整个请求只发生 **2 次线程切换**，IO 线程在分发任务后立即回到 `epoll_wait()` 处理其他连接，不阻塞等待业务逻辑完成。
