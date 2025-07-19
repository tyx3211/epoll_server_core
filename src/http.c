#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// #define _POSIX_C_SOURCE 200809L
#include "http.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h> // Required for errno
#include "logger.h"
#include "config.h"

#define MAX_PATH_LEN 256

int parseHttpRequest(char* requestStr, size_t requestLen, HttpRequest* req) {
    if (!requestStr || requestLen == 0 || !req) {
        return -1;
    }

    char* saveptr1;
    char* line = strtok_r(requestStr, "\r\n", &saveptr1);

    if (!line) {
        fprintf(stderr, "PARSE_DEBUG: line is NULL\n");
        fprintf(stderr, "Failed to parse request line\n");
        return -1;
    }

    char* saveptr2;
    char* method = strtok_r(line, " ", &saveptr2);
    char* uri = strtok_r(NULL, " ", &saveptr2);
    char* version = strtok_r(NULL, " ", &saveptr2);

    if (!method || !uri || !version) {
        fprintf(stderr, "PARSE_DEBUG: method, uri, or version is NULL\n");
        fprintf(stderr, "Malformed request line\n");
        return -1;
    }

    req->method = strdup(method);
    req->uri = strdup(uri);

    if (!req->method || !req->uri) {
        // strdup failed (out of memory)
        free(req->method);
        free(req->uri);
        req->method = NULL;
        req->uri = NULL;
        return -1;
    }

    return 0;
}

void freeHttpRequest(HttpRequest* req) {
    if (req) {
        free(req->method);
        free(req->uri);
        req->method = NULL;
        req->uri = NULL;
    }
}

const char* getMimeType(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (strcmp(dot, ".html") == 0) {
        return "text/html";
    }
    if (strcmp(dot, ".css") == 0) {
        return "text/css";
    }
    if (strcmp(dot, ".js") == 0) {
        return "application/javascript";
    }
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcmp(dot, ".png") == 0) {
        return "image/png";
    }
    if (strcmp(dot, ".gif") == 0) {
        return "image/gif";
    }
    return "application/octet-stream";
}

void handleStaticRequest(int fd, const char* uri, const ServerConfig* config) {
    char path[MAX_PATH_LEN];
    if (strcmp(uri, "/") == 0) {
        snprintf(path, sizeof(path), "%s/index.html", config->document_root);
    } else {
        snprintf(path, sizeof(path), "%s%s", config->document_root, uri);
    }

    // Security check: simple but effective check for path traversal.
    if (strstr(path, "../") != NULL) {
        log_access(NULL, uri, "GET", 403);
        char response[] = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\nForbidden";
        write(fd, response, sizeof(response) - 1);
        return;
    }

    int fileFd = open(path, O_RDONLY);
    if (fileFd == -1) {
        if (errno == ENOENT) {
            log_access(NULL, uri, "GET", 404);
            // Send 404 Not Found
            char response[] = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\nNot Found";
            write(fd, response, sizeof(response) - 1);
        } else {
            log_access(NULL, uri, "GET", 403);
            // Send 403 Forbidden for other errors like permission denied
            char response[] = "HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\nForbidden";
            write(fd, response, sizeof(response) - 1);
        }
        return;
    }

    struct stat fileStat;
    if (fstat(fileFd, &fileStat) == -1) {
        log_system(LOG_ERROR, "fstat error on %s: %s", path, strerror(errno));
        close(fileFd);
        return;
    }

    log_access(NULL, uri, "GET", 200);

    char header[512];
    int headerLen = snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Connection: close\r\n"
                             "Content-Type: %s\r\n"
                             "Content-Length: %ld\r\n\r\n",
                             getMimeType(path), fileStat.st_size);
    write(fd, header, headerLen);

    // Send file content
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(fileFd, buffer, sizeof(buffer))) > 0) {
        write(fd, buffer, bytesRead);
    }

    close(fileFd);
} 