#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// #define _POSIX_C_SOURCE 200809L
#include "http.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

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