#ifndef RESPONSE_H
#define RESPONSE_H

#include <stddef.h>
#include "yyjson.h" // Phase 3: JSON support

// Forward declaration to avoid circular dependency
struct Connection;

// Maximum number of custom headers in a response
#define MAX_RESPONSE_HEADERS 16

// HTTP Response Header
typedef struct {
    char* key;
    char* value;
} ResponseHeader;

// HTTP Response Structure
typedef struct {
    int status_code;
    char status_text[64];         // e.g., "OK", "Not Found", "Internal Server Error"
    
    ResponseHeader headers[MAX_RESPONSE_HEADERS];
    int header_count;
    
    char* body;                   // Response body (dynamically allocated)
    size_t body_len;
    
    // Content-Type shortcut (also stored in headers, but cached for convenience)
    char content_type[128];
} HttpResponse;

// ============================================================================
// Core Response API
// ============================================================================

/**
 * Initialize an HttpResponse with a status code.
 * Sets default status text based on code.
 * 
 * @param res Pointer to HttpResponse to initialize
 * @param status_code HTTP status code (e.g., 200, 404, 500)
 */
void http_response_init(HttpResponse* res, int status_code);

/**
 * Set a custom header in the response.
 * If the key already exists, it will be overwritten.
 * 
 * @param res Pointer to HttpResponse
 * @param key Header name (e.g., "X-Custom-Header")
 * @param value Header value
 */
void http_response_set_header(HttpResponse* res, const char* key, const char* value);

/**
 * Set the Content-Type header (convenience function).
 * 
 * @param res Pointer to HttpResponse
 * @param content_type MIME type (e.g., "application/json", "text/html")
 */
void http_response_set_content_type(HttpResponse* res, const char* content_type);

/**
 * Set the response body. The data is copied internally.
 * 
 * @param res Pointer to HttpResponse
 * @param body Pointer to body data
 * @param len Length of body data
 */
void http_response_set_body(HttpResponse* res, const char* body, size_t len);

/**
 * Set the response body from a null-terminated string.
 * Convenience wrapper around http_response_set_body.
 * 
 * @param res Pointer to HttpResponse
 * @param body Null-terminated string
 */
void http_response_set_body_str(HttpResponse* res, const char* body);

/**
 * Finalize and send the response to the client.
 * This builds the HTTP response string and queues it for writing.
 * After calling this, the response should be freed.
 * 
 * @param conn Pointer to Connection
 * @param res Pointer to HttpResponse
 * @param epollFd The epoll file descriptor
 */
void http_response_send(struct Connection* conn, HttpResponse* res, int epollFd);

/**
 * Free any dynamically allocated memory in the response.
 * Should be called after http_response_send.
 * 
 * @param res Pointer to HttpResponse
 */
void http_response_free(HttpResponse* res);

// ============================================================================
// Quick Response Helpers (One-liners for common cases)
// ============================================================================

/**
 * Send a JSON response.
 * Sets Content-Type to application/json automatically.
 * 
 * @param conn Pointer to Connection
 * @param status_code HTTP status code
 * @param json_body JSON string (null-terminated)
 * @param epollFd The epoll file descriptor
 */
void http_send_json(struct Connection* conn, int status_code, const char* json_body, int epollFd);

/**
 * Send a plain text response.
 * Sets Content-Type to text/plain automatically.
 * 
 * @param conn Pointer to Connection
 * @param status_code HTTP status code
 * @param text_body Plain text string (null-terminated)
 * @param epollFd The epoll file descriptor
 */
void http_send_text(struct Connection* conn, int status_code, const char* text_body, int epollFd);

/**
 * Send a simple error response.
 * If message is NULL, a default message based on status_code is used.
 * Content-Type is set to text/plain.
 * 
 * @param conn Pointer to Connection
 * @param status_code HTTP status code (e.g., 400, 401, 404, 500)
 * @param message Optional error message (can be NULL)
 * @param epollFd The epoll file descriptor
 */
void http_send_error(struct Connection* conn, int status_code, const char* message, int epollFd);

// ============================================================================
// Phase 3: JSON Document Response API
// ============================================================================

/**
 * Send a JSON response from a yyjson mutable document.
 * The document is serialized to string and sent with Content-Type: application/json.
 * The document is NOT freed by this function - caller is responsible for cleanup.
 * 
 * @param conn Pointer to Connection
 * @param status_code HTTP status code
 * @param doc The yyjson mutable document to serialize and send
 * @param epollFd The epoll file descriptor
 */
void http_send_json_doc(struct Connection* conn, int status_code, yyjson_mut_doc* doc, int epollFd);

#endif // RESPONSE_H





