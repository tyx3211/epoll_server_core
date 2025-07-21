#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h> // Required for errno
#include <unistd.h> // Required for write

static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

char* urlDecode(const char* str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    char* decoded = (char*)malloc(len + 1);
    if (!decoded) return NULL;

    size_t decoded_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '+') {
            decoded[decoded_len++] = ' ';
        } else if (str[i] == '%' && i + 2 < len) {
            int hi = hex_to_int(str[i + 1]);
            int lo = hex_to_int(str[i + 2]);
            if (hi != -1 && lo != -1) {
                decoded[decoded_len++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                // Invalid hex sequence, copy as is
                decoded[decoded_len++] = str[i];
            }
        } else {
            decoded[decoded_len++] = str[i];
        }
    }
    decoded[decoded_len] = '\0';

    return decoded;
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
    if (strcmp(dot, ".ico") == 0) {
        return "image/x-icon";
    }
    return "application/octet-stream";
}