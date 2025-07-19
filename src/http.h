#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include "config.h" // For ServerConfig

// Represents a single client connection
typedef struct Connection {
    int fd;
    char* read_buf;      // Dynamically allocated buffer for the request
    size_t read_buf_size; // Current allocated size of read_buf
    size_t read_len;      // Current length of data in read_buf
} Connection;

// Represents a parsed HTTP request
typedef struct {
    char* method;
    char* uri;
    // We will add headers later
} HttpRequest;

// Parses a raw request string into an HttpRequest struct.
// Returns 0 on success, -1 on failure.
int parseHttpRequest(char* requestStr, size_t requestLen, HttpRequest* req);

// Frees the memory allocated for an HttpRequest.
void freeHttpRequest(HttpRequest* req);

// Returns the MIME type for a given file path.
const char* getMimeType(const char* path);

// Handles a request for a static file.
void handleStaticRequest(int fd, const char* uri, const ServerConfig* config);

#endif // HTTP_H 