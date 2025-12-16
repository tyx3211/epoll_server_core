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
#include "utils.h"
#include "server.h" // For queue_data_for_writing

#define MAX_PATH_LEN 256

// Helper function to trim leading/trailing whitespace - NO LONGER USED
/*
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
*/

int parseHttpRequest(char* requestStr, size_t requestLen, HttpRequest* req) {
    (void)requestStr;
    (void)requestLen;
    (void)req;
    // This function is now deprecated and will be replaced by
    // an incremental parser in server.c.
    // Returning -1 to indicate it should not be used.
    return -1;
}

void freeHttpRequest(HttpRequest* req) {
    if (req) {
        free(req->method);
        free(req->raw_uri);
        free(req->uri);
        free(req->raw_query_string);
        free(req->query_string);
        free(req->authed_user);
        for (int i = 0; i < req->header_count; i++) {
            free(req->headers[i].key);
            free(req->headers[i].value);
        }
        // Free pre-parsed query parameters (Phase 2)
        for (int i = 0; i < req->query_param_count; i++) {
            free(req->query_params[i].key);
            free(req->query_params[i].value);
        }
        // Free pre-parsed body parameters (Phase 2)
        for (int i = 0; i < req->body_param_count; i++) {
            free(req->body_params[i].key);
            free(req->body_params[i].value);
        }
        // Free JSON document (Phase 3)
        if (req->json_doc) {
            yyjson_doc_free(req->json_doc);
        }
        // Use memset to be safe, especially since this struct is part of another struct
        memset(req, 0, sizeof(HttpRequest));
    }
}

void handleStaticRequest(Connection* conn, const ServerConfig* config, int epollFd) {
    const char* method = conn->request.method;
    const char* uri = conn->request.uri;
    
    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
        log_system(LOG_DEBUG, "Static: Received unsupported method '%s' for URI '%s'", method, uri);
        char response[] = "HTTP/1.1 501 Not Implemented\r\n\r\nNot Implemented";
        queue_data_for_writing(conn, response, sizeof(response) - 1, epollFd);
        log_access(conn->client_ip, method, conn->request.raw_uri, 501);
        return;
    }
    log_system(LOG_DEBUG, "Static: Handling %s request for URI '%s'", method, uri);

    char path[MAX_PATH_LEN];

    if (strcmp(uri, "/") == 0) {
        snprintf(path, sizeof(path), "%s/index.html", config->document_root);
    } else {
        snprintf(path, sizeof(path), "%s%s", config->document_root, uri);
    }
    log_system(LOG_DEBUG, "Static: Resolved file path to '%s'", path);

    // Security check: simple but effective check for path traversal.
    if (strstr(path, "../") != NULL) {
        log_system(LOG_WARNING, "Static: Path traversal attempt blocked for URI '%s'", uri);
        log_access(conn->client_ip, method, uri, 403);
        char response[] = "HTTP/1.1 403 Forbidden\r\n\r\nForbidden";
        queue_data_for_writing(conn, response, sizeof(response) - 1, epollFd);
        return;
    }

    int fileFd = open(path, O_RDONLY);
    if (fileFd == -1) {
        log_system(LOG_DEBUG, "Static: Failed to open file '%s'. errno: %d (%s)", path, errno, strerror(errno));
        if (errno == ENOENT) {
            log_access(conn->client_ip, method, uri, 404);
            char response[] = "HTTP/1.1 404 Not Found\r\n\r\nNot Found";
            queue_data_for_writing(conn, response, sizeof(response) - 1, epollFd);
        } else {
            log_access(conn->client_ip, method, uri, 403);
            char response[] = "HTTP/1.1 403 Forbidden\r\n\r\nForbidden";
            queue_data_for_writing(conn, response, sizeof(response) - 1, epollFd);
        }
        return;
    }

    struct stat fileStat;
    if (fstat(fileFd, &fileStat) == -1) {
        log_system(LOG_ERROR, "fstat error on %s: %s", path, strerror(errno));
        close(fileFd);
        // Let's send a 500 error to the client
        char response[] = "HTTP/1.1 500 Internal Server Error\r\n\r\nInternal Server Error";
        queue_data_for_writing(conn, response, sizeof(response) - 1, epollFd);
        log_access(conn->client_ip, method, uri, 500);
        return;
    }

    log_access(conn->client_ip, method, uri, 200);

    const char* mime_type = config->mime_enabled ? getMimeType(path) : "application/octet-stream";
    log_system(LOG_DEBUG, "Static: Serving file '%s' (%ld bytes) with MIME type '%s'", path, fileStat.st_size, mime_type);

    char header[512];
    int headerLen = snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             ""
                             "Content-Type: %s\r\n"
                             "Content-Length: %ld\r\n\r\n",
                             mime_type, 
                             fileStat.st_size);
    queue_data_for_writing(conn, header, headerLen, epollFd);

    // For HEAD requests, we only send the header.
    if (strcasecmp(method, "GET") == 0) {
        // Send file content
        char buffer[4096];
        ssize_t bytesRead;
        while ((bytesRead = read(fileFd, buffer, sizeof(buffer))) > 0) {
            queue_data_for_writing(conn, buffer, bytesRead, epollFd);
        }
    }

    close(fileFd);
} 