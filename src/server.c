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

#define MAX_EVENTS 64
#define INITIAL_BUF_SIZE 4096

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
                    int connFd = accept(listenFd, NULL, NULL);
                    if (connFd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        log_system(LOG_ERROR, "accept: %s", strerror(errno));
                        break;
                    }
                    setNonBlocking(connFd);
                    
                    Connection* conn = (Connection*)malloc(sizeof(Connection));
                    conn->fd = connFd;
                    conn->read_buf_size = INITIAL_BUF_SIZE;
                    conn->read_buf = (char*)malloc(conn->read_buf_size);
                    conn->read_len = 0;
                    
                    struct epoll_event client_event;
                    client_event.data.ptr = conn;
                    client_event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
                    epoll_ctl(epollFd, EPOLL_CTL_ADD, connFd, &client_event);
                }
            } else if (events[i].events & EPOLLIN) {
                Connection* conn = (Connection*)events[i].data.ptr;
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
                    close(conn->fd);
                    free(conn->read_buf);
                    free(conn);
                    continue;
                }
                
                // Check for end of headers
                char* header_end = strstr(conn->read_buf, "\r\n\r\n");
                if (header_end) {
                    HttpRequest req;
                    if (parseHttpRequest(conn->read_buf, conn->read_len, &req) == 0) {
                        log_system(LOG_INFO, "Parsed Request: Method=%s, URI=%s", req.method, req.uri);
                        handleStaticRequest(conn->fd, req.uri, &config);
                        freeHttpRequest(&req);
                    }
                    // For simplicity, we close connection after one request.
                    close(conn->fd);
                    free(conn->read_buf);
                    free(conn);
                }
            } else {
                // Handle other events like EPOLLRDHUP, EPOLLERR
                Connection* conn = (Connection*)events[i].data.ptr;
                close(conn->fd);
                free(conn->read_buf);
                free(conn);
            }
        }
    }
    log_system(LOG_INFO, "Server shutting down.");
    close(epollFd);
    close(listenFd);
    logger_shutdown();
} 