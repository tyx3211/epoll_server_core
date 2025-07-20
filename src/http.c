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
#include <ctype.h>
#include <strings.h>

#define MAX_PATH_LEN 256

// Helper function to trim leading/trailing whitespace
static char* trim(char* str) {
    if (!str) return NULL;
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}


int parseHttpRequest(char* requestStr, size_t requestLen, HttpRequest* req) {
    // This function is now deprecated and will be replaced by
    // an incremental parser in server.c.
    // Returning -1 to indicate it should not be used.
    return -1;
}

void freeHttpRequest(HttpRequest* req) {
    if (req) {
        free(req->method);
        free(req->uri);
        for (int i = 0; i < req->header_count; i++) {
            free(req->headers[i].key);
            free(req->headers[i].value);
        }
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