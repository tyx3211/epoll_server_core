# HTTP/1.1 Keep-Alive 与 Pipeline 实现设计文档

## 概述

本文档记录了为 Epoll Web Server 框架添加 HTTP/1.1 Keep-Alive (持久连接) 和 Pipeline (管线化) 支持的设计思路与实现细节。

## 背景问题

### 短连接 (Short-Lived Connection) 的局限性

原始设计采用"一次请求-响应后立即关闭连接"的模式：
```
Client -> [TCP Handshake] -> Request -> Response -> [TCP Close] -> repeat...
```

**缺点**：
- 每个请求都需要 TCP 三次握手和四次挥手，延迟高。
- 在高并发场景下，大量 TIME_WAIT 状态的 socket 会耗尽端口资源。

### Keep-Alive 的价值

HTTP/1.1 默认启用 Keep-Alive，允许在单个 TCP 连接上发送多个请求：
```
Client -> [TCP Handshake] -> Req1 -> Resp1 -> Req2 -> Resp2 -> ... -> [TCP Close]
```

**优点**：
- 减少连接建立开销。
- 更高的吞吐量。
- 对客户端更友好（浏览器默认行为）。

---

## 核心设计思想

### 1. 状态机驱动一切 (State Machine Driven)

我们引入了新的解析状态 `PARSE_STATE_SENDING`，用于标识"当前请求已处理完毕，正在发送响应"的阶段。

**完整状态机流转**：
```
                        +--------- 新连接 ---------+
                        ▼                          |
                  +-----------+                    |
               +->| REQ_LINE  |<-------------------+
               |  +-----------+                    |
               |       | (解析到 \r\n)             |
               |       ▼                           |
               |  +-----------+                    |
               |  | HEADERS   |                    |
               |  +-----------+                    |
               |       | (遇到空行 \r\n\r\n)       |
               |       ▼                           |
               |  +-----------+                    |
               |  |   BODY    | (如果 Content-Length > 0)
               |  +-----------+                    |
               |       | (Body 接收完整)           |
               |       ▼                           |
               |  +-----------+                    |
               |  | COMPLETE  | -----> 调用业务 Handler
               |  +-----------+                    |
               |       |                           |
               |       ▼                           |
               |  +-----------+                    |
               |  | SENDING   | <------ 阻止解析新请求
               |  +-----------+                    |
               |       |                           |
               |       | (Response 全部发送完毕)   |
               |       ▼                           |
               |   keep_alive?                     |
               |     /    \                        |
               |   Yes     No                      |
               |    |       |                      |
               |    |       +---> closeConnection()
               |    |                              |
               +----+ (重置状态，检查 Buffer 残留)
```

### 2. IO 与逻辑解耦 (Decoupling I/O from Logic)

**关键洞见**：`EPOLLIN` 事件（收数据）和解析逻辑必须解耦。

- **Producer (EPOLLIN)**: 只负责把数据 `read()` 进 `read_buf`，不关心当前状态。
- **Consumer (Parser)**: 只有在 `state != SENDING` 时才消费 buffer 数据。

这保证了即使在发送响应阶段，我们也能继续接收客户端发来的下一个请求（Pipeline），而不丢失任何数据。

---

## 实现细节

### 1. HTTP 版本检测与默认 Keep-Alive

在请求行解析阶段，我们提取 HTTP 版本：

```c
// server.c (请求行解析)
char* http_version = strtok_r(NULL, " ", &saveptr);
if (http_version && strstr(http_version, "HTTP/1.1")) {
    conn->request.minor_version = 1;
    conn->request.keep_alive = true;  // HTTP/1.1 默认 Keep-Alive
} else {
    conn->request.minor_version = 0;
    conn->request.keep_alive = false; // HTTP/1.0 默认 Close
}
```

### 2. Connection 头覆盖

在 Header 解析阶段，检测 `Connection` 头以允许客户端显式覆盖默认行为：

```c
// server.c (Header 解析)
if (strcasecmp(key_buf, "Connection") == 0) {
    if (strcasecmp(value_buf, "close") == 0) {
        conn->request.keep_alive = false;
    } else if (strcasecmp(value_buf, "keep-alive") == 0) {
        conn->request.keep_alive = true;
    }
}
```

### 3. SENDING 状态阻止重入与 Pipeline 边界保护

**关键挑战**：在 C 语言中，字符串通常需要以 `\0` 结尾。但在 Pipeline 模式下，Buffer 中紧挨着当前请求 Body 的就是下一个请求的 Method。如果我们直接在 Body 后面写 `\0`，就会覆盖掉下一个请求的第一个字节，导致下一个请求解析失败。

**解决方案：Save & Restore (保存与还原)**

在调用业务 Handler 之前，我们执行以下操作：

```c
// 1. 检查是否有后续数据 (Pipeline)
char saved_char = 0;
bool need_restore = false;
size_t body_end_idx = conn->parsed_offset;

if (conn->read_len > body_end_idx) {
    saved_char = conn->read_buf[body_end_idx]; // 保存下一个请求的字节
    need_restore = true;
}

// 2. 临时写入 \0 供 Handler 使用
conn->read_buf[body_end_idx] = '\0';

// 3. 调用 Handler
handler(conn, config, epollFd);

// 4. 还原现场
if (need_restore) {
    conn->read_buf[body_end_idx] = saved_char;
}

// 5. 切换状态
conn->parsing_state = PARSE_STATE_SENDING;
```

这一机制确保了无论 Pipeline 也就是 Buffer 中堆积了多少请求，我们都能安全地处理，互不干扰。

**为什么需要这个？**

在非阻塞 IO 下，如果响应较大，可能无法一次性发送完毕。此时 Epoll 会再次触发 `EPOLLIN`（如果有新数据）。如果没有 SENDING 状态保护，程序会再次进入 `COMPLETE` 分支，导致业务 Handler 被重复调用。

### 4. handleWrite 的 Keep-Alive 分支

这是本次改造的核心：

```c
// server.c (handleWrite)
if (conn->write_pos == conn->write_len) {
    // 所有数据发送完毕
    
    if (conn->request.keep_alive) {
        // 1. 重置连接状态
        resetConnectionForNextRequest(conn);
        
        // 2. 取消 EPOLLOUT 监听
        epoll_ctl(epollFd, EPOLL_CTL_MOD, conn->fd, &event_in_only);
        
        // 3. Pipeline 处理：如果 buffer 里还有数据，立即处理
        //    这是 ET 模式的必要操作，否则会丢失边沿触发
        if (conn->read_len > 0) {
            handleConnection(conn, config, epollFd);
        }
    } else {
        closeConnection(conn, epollFd);
    }
}
```

### 5. Buffer 重置与 Pipeline 触发

`resetConnectionForNextRequest()` 负责：

1. **释放旧请求的动态内存** (`freeHttpRequest`)
2. **Buffer 压缩** (`memmove` 将剩余数据移到开头)
3. **状态重置** (`parsing_state = REQ_LINE`)

```c
static void resetConnectionForNextRequest(Connection* conn) {
    freeHttpRequest(&conn->request);
    
    size_t remaining = conn->read_len - conn->parsed_offset;
    if (remaining > 0) {
        memmove(conn->read_buf, conn->read_buf + conn->parsed_offset, remaining);
    }
    conn->read_len = remaining;
    conn->parsed_offset = 0;
    conn->write_len = 0;
    conn->write_pos = 0;
    conn->parsing_state = PARSE_STATE_REQ_LINE;
    memset(&conn->request, 0, sizeof(HttpRequest));
}
```

---

## ET 模式下的 Pipeline 正确性证明

### 场景：Req1 发送响应时，Req2/Req3 已到达

```
时间轴:
T0: 收到 Req1，解析完成，调用 Handler，状态 -> SENDING
T1: 尝试发送 Resp1，socket 缓冲区满，返回 EAGAIN
T2: 同时收到 Req2 (存入 read_buf，但 state=SENDING，不解析)
T3: Epoll 触发 EPOLLOUT，继续发送 Resp1
T4: Resp1 发送完毕
T5: 检查 keep_alive=true
T6: 调用 resetConnectionForNextRequest()
T7: 检查 read_len > 0 (Req2 数据在 buffer 里!)
T8: 调用 handleConnection() 处理 Req2
T9: ... 循环继续 ...
```

**关键点**：
- T2 时刻，即使 `EPOLLIN` 触发，因为 `state=SENDING`，我们只是把数据存入 buffer，不会提前处理。
- T7 时刻，我们**主动检查** buffer 剩余数据。这是 ET 模式的必要操作——如果不检查，由于 Req2 的数据在 T2 时刻已经"边沿触发"过了，之后不会再触发，Req2 就会被永久遗忘。


**详解 + 点评**：

其实难度主要就在ET模式：  
在我们之前那个情况下，如果有多余的req2和req3，由于是ET模式，此时如果客户端不来数据，那么我们如果不在handleWrite中调用handleConnection，我们就会对于这个连接阻塞在epoll_wait，那两个请求没法处理了。

而如果我们在handleWrite处调用一次handleConnection，注意：只需要一次判断下的一次调用。  
那么我们恰好就能满足这个时序：（从头模拟）  

parsing req1 -> （置epoll_out，并置sending状态，此时依旧不惧新req来，我只放到read_buf，绝不parsing，也就不会提前响应） send res1,   
然后三种情况：
1. readbuf已经读完，那么自然的回到初始状态，有请求来了就复刻req1的流程。
2. readbuf不足一个请求，那么我handleConnection只是尽早parse了相应部分，接下来继续到epoll主循环中，等待数据发来，继续parsing req2的状态机。此时依旧严格保持时序： send res1 -> parse req2 -> send req2
3. readbuf中有一个或者多个完整请求，那么此时我handleConnection一次，“再度进入了sending状态”，此时立马串行再置epoll_out后：我们回到epoll_wait。此时即使再来请求req4内容又如何？我只是把你加到readbuf中，不会parse你，何谈发送？直到我epollout事件触发，我再次到了res2的handleWrite，此时 必须完整到 send res2这个时序后。我们才能进入-> parsing req3这条路线。

严格保证时序，并且巧妙地，在多么极端的情况下，我们也从来来者不拒的将新的请求放到buf中，不错失ET模式每一次请求数据。

这样就算你倾轧式的连续给我发请求。  
我也能做到：  
parse 1-> send 1 -> parse 2 -> send 2 -> ...

---

## 响应头处理

`response.c` 中的 `http_response_send()` 根据 `keep_alive` 标志动态设置响应头：

```c
if (conn->request.keep_alive) {
    snprintf(header_buf + offset, ..., "Connection: keep-alive\r\n");
} else {
    snprintf(header_buf + offset, ..., "");
}
```

---

## 测试建议

### 1. 基础 Keep-Alive 测试

使用 `curl` 的 `-v` 选项观察 `Connection` 头：

```bash
curl -v http://localhost:8080/
```

预期：响应头包含 `Connection: keep-alive`。

### 2. Pipeline 测试

使用 `telnet` 或 `nc` 手动发送多个请求：

```bash
(echo -e "GET / HTTP/1.1\r\nHost: localhost\r\n\r\nGET /api/time HTTP/1.1\r\nHost: localhost\r\n\r\n"; sleep 2) | nc localhost 8080
```

预期：在同一个 TCP 连接上收到两个响应。

### 3. 压力测试

使用 `wrk` 或 `ab` 进行基准测试：

```bash
wrk -t4 -c100 -d10s http://localhost:8080/
```

对比启用 Keep-Alive 前后的 RPS (Requests Per Second)。

---

## 未来改进

1. **Keep-Alive 超时**: 目前未实现。应引入 Timer Wheel 定时关闭空闲连接。
2. **最大请求数限制**: HTTP/1.1 允许服务器限制单个连接上的最大请求数，超过后发送 `Connection: close`。
3. **Chunked Transfer Encoding**: 当前不支持客户端发送 Chunked 请求体。

---

## 总结

本次升级的核心思想是：**状态机驱动 + IO/逻辑解耦**。

通过引入 `PARSE_STATE_SENDING` 状态，我们实现了：
- 安全的异步发送（防止 Handler 重入）
- 正确的 Pipeline 处理（在 ET 模式下不丢失任何请求）
- 严格的时序保证（Req1 -> Resp1 -> Req2 -> Resp2）

这使得我们的微型 Web 框架在功能上更接近生产级服务器（如 Nginx），同时保持了代码的简洁性和可读性。
