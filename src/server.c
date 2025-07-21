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
static void closeConnection(Connection* conn);
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
                    inet_ntop(AF_INET, &client_addr.sin_addr, conn->client_ip, sizeof(conn->client_ip));
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
                    client_event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
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
                closeConnection(conn);
            }
        }
    }
    log_system(LOG_INFO, "Server shutting down.");
    close(epollFd);
    close(listenFd);
    logger_shutdown();
}

static void closeConnection(Connection* conn) {
    if (conn) {
        close(conn->fd);
        freeHttpRequest(&conn->request);
        free(conn->read_buf);
        free(conn->write_buf);
        free(conn);
    }
}

static void handleWrite(Connection* conn, ServerConfig* config, int epollFd) {
    if (conn->write_len == 0) {
        // Nothing to write, weird. Unregister interest in EPOLLOUT.
        struct epoll_event event;
        event.data.ptr = conn;
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        epoll_ctl(epollFd, EPOLL_CTL_MOD, conn->fd, &event);
        return;
    }

    ssize_t nwritten = write(conn->fd, conn->write_buf + conn->write_pos, conn->write_len - conn->write_pos);

    if (nwritten > 0) {
        conn->write_pos += nwritten;
        if (conn->write_pos == conn->write_len) {
            // All data sent successfully
            conn->write_pos = 0;
            conn->write_len = 0;
            // Unregister interest in EPOLLOUT and close the connection as we don't support keep-alive
            closeConnection(conn); 
        }
        // If not all data was sent, we do nothing and wait for the next EPOLLOUT
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_system(LOG_ERROR, "write error on fd %d: %s", conn->fd, strerror(errno));
            closeConnection(conn);
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
    while ((bytesRead = read(conn->fd, temp_buf, sizeof(temp_buf))) > 0) {
        if (conn->read_len + bytesRead > conn->read_buf_size) {
            conn->read_buf_size *= 2;
            conn->read_buf = (char*)realloc(conn->read_buf, conn->read_buf_size);
        }
        memcpy(conn->read_buf + conn->read_len, temp_buf, bytesRead);
        conn->read_len += bytesRead;
    }

    if (bytesRead == 0 || (bytesRead < 0 && errno != EAGAIN)) {
        closeConnection(conn);
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
            conn->request.method = strdup(strtok_r(line_buf, " ", &saveptr));
            char* full_uri = strtok_r(NULL, " ", &saveptr);
            
            if (conn->request.method && full_uri) {
                // conn->request.raw_uri = strdup(full_uri);
                
                // Separate URI path and query string
                char* query_start = strchr(full_uri, '?');
                if (query_start) {
                    *query_start = '\0'; // Split the string
                    conn->request.raw_uri = strdup(full_uri);
                    conn->request.uri = urlDecode(full_uri);
                    conn->request.raw_query_string = strdup(query_start + 1);
                    conn->request.query_string = urlDecode(query_start + 1);
                } else {
                    conn->request.raw_uri = strdup(full_uri);
                    conn->request.uri = urlDecode(full_uri);
                    conn->request.raw_query_string = NULL;
                    conn->request.query_string = NULL;
                }
                
                log_system(LOG_DEBUG, "Parsed request line: %s %s", conn->request.method, conn->request.uri);
                conn->parsing_state = PARSE_STATE_HEADERS;
                conn->parsed_offset += line_len + 2; // +2 for \r\n
            } else { // Malformed
                 free(conn->request.method);
                 // error handling...
            }
        }
    }

    // State: PARSE_HEADERS
    if (conn->parsing_state == PARSE_STATE_HEADERS) {
        char* start = conn->read_buf + conn->parsed_offset;
        char* end = conn->read_buf + conn->read_len;
        
        while (start < end && conn->request.header_count < MAX_HEADERS) {
            char* line_end = strstr(start, "\r\n");
            if (!line_end) break; // Incomplete line

            if (line_end == start) { // Empty line, marks end of headers
                conn->parsed_offset = (line_end - conn->read_buf) + 2;
                log_system(LOG_DEBUG, "Parsed headers. Content-Length: %zu", conn->request.content_length);
                conn->parsing_state = (conn->request.content_length > 0) ? PARSE_STATE_BODY : PARSE_STATE_COMPLETE;
                break; // <-- THE FIX: Exit header parsing loop
            }

            char* colon = memchr(start, ':', line_end - start);
            if (colon) {
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

                if (strcasecmp(conn->request.headers[conn->request.header_count].key, "Content-Length") == 0) {
                    conn->request.content_length = atol(conn->request.headers[conn->request.header_count].value);
                }
                conn->request.header_count++;
            }
            start = line_end + 2; // Move to the next line
            conn->parsed_offset = start - conn->read_buf; // Also update offset here
        }
    }

    // Use a direct check instead of goto to simplify flow
    if (conn->parsing_state == PARSE_STATE_BODY) {
        // Here we would handle reading the request body.
        // For now, we will assume body is fully read if content_length is met
        if (conn->read_len >= conn->parsed_offset + conn->request.content_length) {
            conn->request.body = conn->read_buf + conn->parsed_offset;
            conn->parsing_state = PARSE_STATE_COMPLETE;
        }
    }

    // 3. If a full request is parsed, handle it
    if (conn->parsing_state == PARSE_STATE_COMPLETE) {
        log_system(LOG_INFO, "Handling complete request: %s %s", conn->request.method, conn->request.uri);
        
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
        // The connection will be closed by handleWrite after all data is sent
    }
} 