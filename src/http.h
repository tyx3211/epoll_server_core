#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>
#include <netinet/in.h> // For INET_ADDRSTRLEN
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
    char* raw_uri; // The original, undecoded URI
    char* uri;     // The URL-decoded URI path (without query string)
    char* raw_query_string; // The original, undecoded query string
    char* query_string; // The URL-decoded query string

    HttpHeader headers[MAX_HEADERS];
    int header_count;
    char* body;
    size_t content_length;
} HttpRequest;

// Represents a single client connection
typedef struct Connection {
    int fd;
    char client_ip[INET_ADDRSTRLEN];

    // Buffer for reading data
    char* read_buf;
    size_t read_buf_size;
    size_t read_len;

    // Buffer for writing data
    char* write_buf;
    size_t write_buf_size;
    size_t write_len; // How much data is in the buffer
    size_t write_pos; // How much has been sent

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
// const char* getMimeType(const char* path); // This is now in utils.h

// Handles a request for a static file.
void handleStaticRequest(Connection* conn, const ServerConfig* config, int epollFd);

#endif // HTTP_H 