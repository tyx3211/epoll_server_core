#define _POSIX_C_SOURCE 200809L
#include "server.h"
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include "http.h"
#include "logger.h"
#include "config.h"
#include <stdlib.h>
#include <strings.h> // For strcasecmp
#include "utils.h"
#include <stdbool.h>
#include "router.h" // Include our new router

#define MAX_EVENTS 64
#define INITIAL_BUF_SIZE 4096

// Forward declarations
static void handleConnection(Connection* conn, ServerConfig* config, int epollFd);
static void handleWrite(Connection* conn, ServerConfig* config, int epollFd);
static void closeConnection(Connection* conn, int epollFd);
void queue_data_for_writing(struct Connection* conn, const char* data, size_t len, int epollFd);

static int setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        return -1;
    }
    return 0;
}

static int createAndBind(int port) {
    int listenFd;
    struct sockaddr_in servAddr;

    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == -1) {
        perror("socket error");
        return -1;
    }

    int optval = 1;
    if (setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(listenFd);
        return -1;
    }

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(port);

    if (bind(listenFd, (struct sockaddr*)&servAddr, sizeof(servAddr)) == -1) {
        perror("bind error");
        close(listenFd);
        return -1;
    }

    if (setNonBlocking(listenFd) == -1) {
        close(listenFd);
        return -1;
    }

    return listenFd;
}

void startServer(const char* configFilePath) {
    ServerConfig config;
    loadConfig(configFilePath, &config);

    if (logger_init(config.log_level, config.log_target, config.log_path) != 0) {
        fprintf(stderr, "Failed to initialize logger.\n");
        return;
    }

    log_system(LOG_INFO, "Server starting with configuration:");
    log_system(LOG_INFO, "  - Port: %d", config.listen_port);
    log_system(LOG_INFO, "  - DocumentRoot: %s", config.document_root);
    
    // We need to pass the DocumentRoot to the http module.
    // For now, let's assume http.c can access it.
    // A better way would be to pass config to http functions.

    int listenFd = createAndBind(config.listen_port);
    if (listenFd == -1) {
        log_system(LOG_ERROR, "Failed to create and bind socket.");
        return;
    }

    if (listen(listenFd, SOMAXCONN) == -1) {
        log_system(LOG_ERROR, "listen error: %s", strerror(errno));
        close(listenFd);
        return;
    }

    log_system(LOG_INFO, "Server listening on port %d...", config.listen_port);

    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        log_system(LOG_ERROR, "epoll_create1: %s", strerror(errno));
        close(listenFd);
        return;
    }

    struct epoll_event event;
    event.data.fd = listenFd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &event) == -1) {
        log_system(LOG_ERROR, "epoll_ctl: listenFd: %s", strerror(errno));
        close(epollFd);
        close(listenFd);
        return;
    }

    struct epoll_event events[MAX_EVENTS];

    log_system(LOG_INFO, "Server is running...");
    while (1) {
        int n = epoll_wait(epollFd, events, MAX_EVENTS, -1);
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == listenFd) {
                while (1) {
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int connFd = accept(listenFd, (struct sockaddr*)&client_addr, &client_len);

                    if (connFd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        log_system(LOG_ERROR, "accept: %s", strerror(errno));
                        break;
                    }
                    setNonBlocking(connFd);
                    
                    Connection* conn = (Connection*)malloc(sizeof(Connection));
                    conn->fd = connFd;
                    inet_ntop(AF_INET, &client_addr.sin_addr, conn->client_ip, sizeof(conn->client_ip)); // 网络序二进制转点分十进制字符串
                    log_system(LOG_DEBUG, "Server: Accepted new connection fd=%d from %s", connFd, conn->client_ip);
                    conn->read_buf_size = INITIAL_BUF_SIZE;
                    conn->read_buf = (char*)malloc(conn->read_buf_size);
                    conn->read_len = 0;
                    conn->write_buf = (char*)malloc(INITIAL_BUF_SIZE);
                    conn->write_buf_size = INITIAL_BUF_SIZE;
                    conn->write_len = 0;
                    conn->write_pos = 0;
                    conn->parsing_state = PARSE_STATE_REQ_LINE;
                    conn->parsed_offset = 0;
                    memset(&conn->request, 0, sizeof(HttpRequest));
                    
                    struct epoll_event client_event;
                    client_event.data.ptr = conn;
                    client_event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // EPOLLRDHUP 代表 对端（客户端）关闭了连接，或者半关闭了写端 的事件，可更早资源回收。
                    epoll_ctl(epollFd, EPOLL_CTL_ADD, connFd, &client_event);
                }
            } else if (events[i].events & EPOLLIN) {
                Connection* conn = (Connection*)events[i].data.ptr;
                handleConnection(conn, &config, epollFd);
            } else if (events[i].events & EPOLLOUT) {
                Connection* conn = (Connection*)events[i].data.ptr;
                handleWrite(conn, &config, epollFd);
            } else {
                // Handle other events like EPOLLRDHUP, EPOLLERR
                Connection* conn = (Connection*)events[i].data.ptr;
                log_system(LOG_DEBUG, "Server: Event %d on fd %d triggered close", events[i].events, conn->fd);
                closeConnection(conn, epollFd);
            }
        }
    }
    log_system(LOG_INFO, "Server shutting down.");
    close(epollFd);
    close(listenFd);
    logger_shutdown();
}

static void closeConnection(Connection* conn, int epollFd) {
    if (conn) {
        log_system(LOG_DEBUG, "Server: Closing connection fd=%d", conn->fd);
        // It's good practice to unregister from epoll before closing the fd
        epoll_ctl(epollFd, EPOLL_CTL_DEL, conn->fd, NULL);
        close(conn->fd);
        freeHttpRequest(&conn->request);
        free(conn->read_buf);
        free(conn->write_buf);
        free(conn);
    }
}

// Reset connection state for Keep-Alive: compact buffer, reset parser, prepare for next request
static void resetConnectionForNextRequest(Connection* conn) {
    log_system(LOG_DEBUG, "Server: Resetting connection fd=%d for next request. parsed_offset=%zu, read_len=%zu",
               conn->fd, conn->parsed_offset, conn->read_len);
    
    // 1. Free the old request's dynamically allocated data
    freeHttpRequest(&conn->request);
    
    // 2. Compact read buffer: move remaining data (next request's partial data) to the front
    size_t remaining = conn->read_len - conn->parsed_offset;
    if (remaining > 0) {
        memmove(conn->read_buf, conn->read_buf + conn->parsed_offset, remaining);
        log_system(LOG_DEBUG, "Server: Moved %zu bytes of remaining data to buffer front.", remaining);
    }
    conn->read_len = remaining;
    conn->parsed_offset = 0;
    
    // 3. Reset write buffer (already sent, so just clear pointers)
    conn->write_len = 0;
    conn->write_pos = 0;
    
    // 4. Reset parser state
    conn->parsing_state = PARSE_STATE_REQ_LINE;
    memset(&conn->request, 0, sizeof(HttpRequest));
    
    log_system(LOG_DEBUG, "Server: Connection fd=%d reset complete. Remaining buffer: %zu bytes.", conn->fd, conn->read_len);
}

static void handleWrite(Connection* conn, ServerConfig* config, int epollFd) {
    if (conn->write_len == 0) {
        // Nothing to write, weird. Unregister interest in EPOLLOUT.
        log_system(LOG_DEBUG, "Server: handleWrite called on fd %d with empty write buffer.", conn->fd);
        struct epoll_event event;
        event.data.ptr = conn;
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        epoll_ctl(epollFd, EPOLL_CTL_MOD, conn->fd, &event);
        return;
    }

    ssize_t nwritten = write(conn->fd, conn->write_buf + conn->write_pos, conn->write_len - conn->write_pos);
    log_system(LOG_DEBUG, "Server: Wrote %zd bytes to fd %d", nwritten, conn->fd);

    if (nwritten > 0) {
        conn->write_pos += nwritten;
        if (conn->write_pos == conn->write_len) {
            // ============================================================
            // All data sent successfully - THIS IS THE KEY DECISION POINT
            // ============================================================
            log_system(LOG_DEBUG, "Server: Finished writing all data to fd %d. keep_alive=%d", 
                       conn->fd, conn->request.keep_alive);
            
            // Check if we should keep the connection alive
            if (conn->request.keep_alive) {
                // === KEEP-ALIVE PATH ===
                log_system(LOG_INFO, "Server: Keep-Alive enabled for fd %d, preparing for next request.", conn->fd);
                
                // Reset connection state for next request (compacts buffer, clears request struct)
                resetConnectionForNextRequest(conn);
                
                // Unregister EPOLLOUT, keep listening for EPOLLIN
                struct epoll_event event;
                event.data.ptr = conn;
                event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                epoll_ctl(epollFd, EPOLL_CTL_MOD, conn->fd, &event);
                
                // PIPELINE HANDLING: If there's already data in the buffer from the next request,
                // we must process it now. In ET mode, if we don't, we might never get woken up
                // because the "data arrival" edge already happened.
                if (conn->read_len > 0) {
                    log_system(LOG_DEBUG, "Server: Pipeline detected! %zu bytes in buffer, processing next request.", conn->read_len);
                    handleConnection(conn, config, epollFd);
                }
            } else {
                // === CLOSE PATH ===
                log_system(LOG_DEBUG, "Server: Connection: close for fd %d, closing.", conn->fd);
                closeConnection(conn, epollFd);
            }
        }
        // If not all data was sent, we do nothing and wait for the next EPOLLOUT
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_system(LOG_ERROR, "write error on fd %d: %s", conn->fd, strerror(errno));
            closeConnection(conn, epollFd);
        }
        // If EAGAIN or EWOULDBLOCK, just wait for the next EPOLLOUT
    }
}

void queue_data_for_writing(struct Connection* conn, const char* data, size_t len, int epollFd) {
    // Check if buffer needs to be expanded
    if (conn->write_len + len > conn->write_buf_size) {
        size_t new_size = conn->write_buf_size;
        while (conn->write_len + len > new_size) {
            new_size *= 2;
        }
        conn->write_buf = (char*)realloc(conn->write_buf, new_size);
        conn->write_buf_size = new_size;
    }

    // Append new data to the write buffer
    memcpy(conn->write_buf + conn->write_len, data, len);
    conn->write_len += len;
    log_system(LOG_DEBUG, "Server: Queued %zu bytes for writing to fd %d (total_queued=%zu)", len, conn->fd, conn->write_len);

    // Register interest in EPOLLOUT to start sending
    struct epoll_event event;
    event.data.ptr = conn;
    event.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    epoll_ctl(epollFd, EPOLL_CTL_MOD, conn->fd, &event);
}

static void handleConnection(Connection* conn, ServerConfig* config, int epollFd) {
    // 1. Read data from socket into connection buffer
    char temp_buf[4096];
    ssize_t bytesRead;
    size_t total_bytes_read_this_call = 0;
    // need space to add '\0'
    while ((bytesRead = read(conn->fd, temp_buf, sizeof(temp_buf))) > 0) {
        if (conn->read_len + bytesRead >= conn->read_buf_size) {
            conn->read_buf_size *= 2;
            conn->read_buf = (char*)realloc(conn->read_buf, conn->read_buf_size);
        }
        memcpy(conn->read_buf + conn->read_len, temp_buf, bytesRead);
        conn->read_len += bytesRead;
        total_bytes_read_this_call += bytesRead;
    }
    log_system(LOG_DEBUG, "Server: Read %zu bytes from fd %d. Total buffer size is now %zu.", total_bytes_read_this_call, conn->fd, conn->read_len);

    if (bytesRead == 0 || (bytesRead < 0 && errno != EAGAIN)) {
        log_system(LOG_DEBUG, "Server: Connection closed by peer or read error on fd %d.", conn->fd);
        closeConnection(conn, epollFd);
        return;
    }

    // 2. Try to parse the request incrementally
    // All state is now in the conn struct, no local HttpRequest needed.
    
    // State: PARSE_REQ_LINE
    if (conn->parsing_state == PARSE_STATE_REQ_LINE) {
        // We search from the start of the unprocessed part of the buffer
        char* line_end = strstr(conn->read_buf + conn->parsed_offset, "\r\n");
        if (line_end) {
            size_t line_len = line_end - (conn->read_buf + conn->parsed_offset);
            char line_buf[line_len + 1];
            memcpy(line_buf, conn->read_buf + conn->parsed_offset, line_len);
            line_buf[line_len] = '\0';
            
            char* saveptr;
            // 1. strtok_r (Reentrant): 线程安全版的分割函数。
            // 第一次调用：传入 line_buf，以空格 " " 切割，提取 Method (如 "GET")。
            // saveptr 参数用于在函数内部保存“切到了哪里”的状态。
            // 注意：strtok_r 是具有破坏性的。它会把原字符串里的分隔符（空格）直接替换成 \0
            char* method_token = strtok_r(line_buf, " ", &saveptr);
            
            // 2. strdup: 在堆(Heap)上申请内存并拷贝字符串。
            // 必须拷贝，因为 line_buf 是栈上的局部变量，出了作用域就会失效。
            // 注意：strdup 返回的指针后续需要 free。
            conn->request.method = method_token ? strdup(method_token) : NULL;
            
            // 3. 第二次调用：传入 NULL，告诉函数“接着上次 saveptr 记录的位置继续切”，提取 URI。
            char* full_uri = strtok_r(NULL, " ", &saveptr);
            
            // 4. 第三次调用：提取 HTTP 版本 (如 "HTTP/1.1")
            char* http_version = strtok_r(NULL, " ", &saveptr);
            
            if (conn->request.method && full_uri && http_version) {
                // Parse HTTP version to determine default keep-alive behavior
                // HTTP/1.1 defaults to keep-alive, HTTP/1.0 defaults to close
                if (http_version && strstr(http_version, "HTTP/1.1")) {
                    conn->request.minor_version = 1;
                    conn->request.keep_alive = true; // Default for HTTP/1.1
                } else {
                    conn->request.minor_version = 0;
                    conn->request.keep_alive = false; // Default for HTTP/1.0
                }
                log_system(LOG_DEBUG, "Parser (fd=%d): HTTP version: 1.%d, default keep_alive=%d",
                           conn->fd, conn->request.minor_version, conn->request.keep_alive);
                
                // Separate URI path and query string
                char* query_start = strchr(full_uri, '?');
                if (query_start) {
                    *query_start = '\0'; // Split the string
                    conn->request.raw_uri = strdup(full_uri);
                    conn->request.uri = urlDecode(full_uri); // 堆分配，需要free
                    conn->request.raw_query_string = strdup(query_start + 1);
                    conn->request.query_string = urlDecode(query_start + 1);
                } else {
                    conn->request.raw_uri = strdup(full_uri);
                    conn->request.uri = urlDecode(full_uri);
                    conn->request.raw_query_string = NULL;
                    conn->request.query_string = NULL;
                }
                
                log_system(LOG_DEBUG, "Parser (fd=%d): Parsed request line: %s %s", conn->fd, conn->request.method, conn->request.raw_uri);
                conn->parsing_state = PARSE_STATE_HEADERS;
                conn->parsed_offset += line_len + 2; // +2 for \r\n
            } else { // Malformed
                 log_system(LOG_WARNING, "Parser (fd=%d): Malformed request line.", conn->fd);
                 free(conn->request.method);
                 // error handling...
                 closeConnection(conn, epollFd);
            }
        }
    }

    // State: PARSE_HEADERS
    if (conn->parsing_state == PARSE_STATE_HEADERS) {
        char* start = conn->read_buf + conn->parsed_offset;
        char* end = conn->read_buf + conn->read_len;
        
        // Fix: Removed '&& conn->request.header_count < MAX_HEADERS' to allow consuming excess headers
        while (start < end) {
            char* line_end = strstr(start, "\r\n");
            if (!line_end) break; // Incomplete line

            if (line_end == start) { // Empty line, marks end of headers
                conn->parsed_offset = (line_end - conn->read_buf) + 2;
                log_system(LOG_DEBUG, "Parser (fd=%d): Finished parsing headers. Content-Length=%zu", conn->fd, conn->request.content_length);
                conn->parsing_state = (conn->request.content_length > 0) ? PARSE_STATE_BODY : PARSE_STATE_COMPLETE;
                break; // <-- THE FIX: Exit header parsing loop
            }

            char* colon = memchr(start, ':', line_end - start); // 就是接受开始指针 ptr 和 最大长度 n 的另一版的 strchr
            if (colon) {
                // Only store header if we have space
                if (conn->request.header_count < MAX_HEADERS) {
                    // Parse Key
                    char key_buf[colon - start + 1];
                    memcpy(key_buf, start, colon - start);
                    key_buf[colon - start] = '\0';
                    conn->request.headers[conn->request.header_count].key = strdup(key_buf);

                    // Parse Value
                    char* value_start = colon + 1;
                    while (*value_start == ' ') value_start++; // Trim leading spaces
                    char value_buf[line_end - value_start + 1];
                    memcpy(value_buf, value_start, line_end - value_start);
                    value_buf[line_end - value_start] = '\0';
                    conn->request.headers[conn->request.header_count].value = strdup(value_buf);
                    log_system(LOG_DEBUG, "Parser (fd=%d): Parsed header: %s: %s", conn->fd, key_buf, value_buf);

                    if (strcasecmp(key_buf, "Content-Length") == 0) {
                        conn->request.content_length = atol(value_buf);
                    }
                    // Check for Connection header to override default keep-alive
                    else if (strcasecmp(key_buf, "Connection") == 0) {
                        if (strcasecmp(value_buf, "close") == 0) {
                            conn->request.keep_alive = false;
                        } else if (strcasecmp(value_buf, "keep-alive") == 0) {
                            conn->request.keep_alive = true;
                        }
                        log_system(LOG_DEBUG, "Parser (fd=%d): Connection header detected, keep_alive=%d",
                                   conn->fd, conn->request.keep_alive);
                    }
                    conn->request.header_count++;
                } else {
                    log_system(LOG_WARNING, "Parser (fd=%d): Max headers reached, ignoring header.", conn->fd);
                    // We still parse Content-Length even if we don't store the header, strictly speaking
                    // But for simplicity, we just ignore everything else.
                }
            }
            start = line_end + 2; // Move to the next line
            conn->parsed_offset = start - conn->read_buf; // Also update offset here
        }
    }

    // Use a direct check instead of goto to simplify flow
    if (conn->parsing_state == PARSE_STATE_BODY) {
        // Here we would handle reading the request body.
        // For now, we will assume body is fully read if content_length is met
        log_system(LOG_DEBUG, "Parser (fd=%d): In body parsing state. Buffer has %zu bytes, need %zu for body.", conn->fd, conn->read_len - conn->parsed_offset, conn->request.content_length);
        if (conn->read_len >= conn->parsed_offset + conn->request.content_length) {
            conn->request.body = conn->read_buf + conn->parsed_offset;
            // REMOVED: conn->request.body[conn->request.content_length] = '\0'; 
            // We cannot simply assume it's safe to write '\0' here as it might overwrite the next request's data.
            
            log_system(LOG_DEBUG, "Parser (fd=%d): Body parsed completely.", conn->fd);
            
            // IMPORTANT: Update parsed_offset to point to the end of this request
            // This is crucial for Pipeline/Keep-Alive to calculate remaining data correctly.
            conn->parsed_offset += conn->request.content_length;
            
            conn->parsing_state = PARSE_STATE_COMPLETE;
        }
    }

    // 3. If a full request is parsed, handle it
    if (conn->parsing_state == PARSE_STATE_COMPLETE) {
        log_system(LOG_INFO, "Handling complete request: %s %s (keep_alive=%d)", 
                   conn->request.method, conn->request.uri, conn->request.keep_alive);
        
        // --- SAVE & RESTORE LOGIC ---
        // To safely support Pipeline, we must not permanently destruct the next request's data.
        // However, we need the body to be null-terminated for string functions in handlers.
        char saved_char = 0;
        bool need_restore = false;
        // conn->parsed_offset now points to the byte AFTER the body (start of next req or free space)
        size_t body_end_idx = conn->parsed_offset;

        if (conn->read_len > body_end_idx) {
            // There is data after the body (the next request), save it!
            saved_char = conn->read_buf[body_end_idx];
            need_restore = true;
        } else {
            // No next request data yet. 
            // Ensure we don't write OOB. In practice, our buffer growth strategy (doubling) 
            // usually leaves space. Strictly we should check capacity, but for now assuming safety.
            // If read_len == capacity, we might need a spare byte. 
        }
        
        // Temporarily null-terminate
        // Note: conn->request.body[content_length] is exactly conn->read_buf[body_end_idx]
        conn->read_buf[body_end_idx] = '\0';
        
        // Pre-parse all parameters (Phase 2)
        http_parse_all_params(&conn->request);
        
        // --- Routing Logic ---
        RouteHandler handler = router_find_handler(conn->request.method, conn->request.uri);
        if (handler) {
            // Found a matching API handler
            log_system(LOG_DEBUG, "Routing to API handler for %s %s", conn->request.method, conn->request.uri);
            handler(conn, config, epollFd);
        } else {
            // No API handler found, fall back to static file serving
            handleStaticRequest(conn, config, epollFd);
        }
        
        // --- RESTORE ---
        if (need_restore) {
            conn->read_buf[body_end_idx] = saved_char;
        }
        
        // CRITICAL: Transition to SENDING state to prevent re-entry
        // This blocks the parser from processing any more requests until
        // the current response is fully sent (handleWrite completes).
        conn->parsing_state = PARSE_STATE_SENDING;
        log_system(LOG_DEBUG, "Parser (fd=%d): State -> SENDING. Waiting for response to complete.", conn->fd);
    }
} 