#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include "config.h" // For ServerConfig

typedef enum {
    PARSE_STATE_REQ_LINE,
    PARSE_STATE_HEADERS,
    PARSE_STATE_BODY,
    PARSE_STATE_COMPLETE
} ParsingState;

#define MAX_HEADERS 32

typedef struct {
    char* key;
    char* value;
} HttpHeader;

// Represents a parsed HTTP request
typedef struct {
    char* method;
    char* uri;
    HttpHeader headers[MAX_HEADERS];
    int header_count;
    char* body;
    size_t content_length;
} HttpRequest;

// Represents a single client connection
typedef struct Connection {
    int fd;
    char* read_buf;      // Dynamically allocated buffer for the request
    size_t read_buf_size; // Current allocated size of read_buf
    size_t read_len;      // Current length of data in read_buf
    
    // Parsing state
    ParsingState parsing_state;
    size_t parsed_offset; // How much of read_buf has been processed
    HttpRequest request;    // The request being built
} Connection;


// Parses a raw request string into an HttpRequest struct.
// Returns the total length of the parsed request (headers + body) on success,
// or -1 on failure/incomplete request.
int parseHttpRequest(char* requestStr, size_t requestLen, HttpRequest* req);

// Frees the memory allocated for an HttpRequest.
void freeHttpRequest(HttpRequest* req);

// Returns the MIME type for a given file path.
const char* getMimeType(const char* path);

// Handles a request for a static file.
void handleStaticRequest(int fd, const char* uri, const ServerConfig* config);

#endif // HTTP_H 