# Epoll Server Core (Library)

这是一个基于 Linux `epoll` 的高性能、轻量级 Web 服务器核心库。它被设计为独立的静态库 (`libwebserver.a`)，为上层应用提供 HTTP 协议解析、路由分发、连接管理和安全认证等基础能力。

**注意**：这只是核心库的仓库。

如果你想查看：
- **完整的项目演示**
- **构建与运行指南**
- **用户后端应用 (User Backend) 源码**
- **详细的架构设计文档**

请访问我们的主项目仓库（父仓库）：

👉 **[Web Server for Learning 主项目仓库](https://github.com/tyx3211/epoll_web_server)** 
*(如果是独立浏览此仓库，请寻找包含此 Submodule 的上层项目)*

---

## 核心功能

*   **Reactor 并发模型**: 基于 `epoll` + 非阻塞 I/O，单线程处理高并发。
*   **HTTP 解析器**: 手写的有限状态机 (FSM)，支持处理 TCP 粘包/半包。
*   **静态文件服务**: 支持多种 MIME 类型，防路径穿越攻击。
*   **动态路由**: 支持 GET/POST 方法注册 C 函数回调。
*   **JWT 认证**: 集成 `l8w8jwt`，提供 Token 生成与验证。
*   **双日志系统**: 访问日志 (Access Log) 与 系统日志 (System Log)。






