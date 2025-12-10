#define _POSIX_C_SOURCE 200809L
#include "response.h"
#include "http.h"    // For Connection struct
#include "server.h"  // For queue_data_for_writing
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // For strcasecmp

// ============================================================================
// Status Code to Text Mapping
// ============================================================================

static const char* get_status_text(int status_code) {
    switch (status_code) {
        // 2xx Success
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        
        // 3xx Redirection
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        
        // 4xx Client Errors
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        
        // 5xx Server Errors
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        
        default: return "Unknown";
    }
}

// ============================================================================
// Core Response API Implementation
// ============================================================================

void http_response_init(HttpResponse* res, int status_code) {
    if (!res) return;
    
    memset(res, 0, sizeof(HttpResponse));
    res->status_code = status_code;
    strncpy(res->status_text, get_status_text(status_code), sizeof(res->status_text) - 1);
    res->header_count = 0;
    res->body = NULL;
    res->body_len = 0;
    res->content_type[0] = '\0';
}

void http_response_set_header(HttpResponse* res, const char* key, const char* value) {
    if (!res || !key || !value) return;
    
    // Check if header already exists (overwrite)
    for (int i = 0; i < res->header_count; i++) {
        if (strcasecmp(res->headers[i].key, key) == 0) {
            free(res->headers[i].value);
            res->headers[i].value = strdup(value);
            return;
        }
    }
    
    // Add new header
    if (res->header_count < MAX_RESPONSE_HEADERS) {
        res->headers[res->header_count].key = strdup(key);
        res->headers[res->header_count].value = strdup(value);
        res->header_count++;
    } else {
        log_system(LOG_WARNING, "Response: Max headers reached, cannot add '%s'", key);
    }
}

void http_response_set_content_type(HttpResponse* res, const char* content_type) {
    if (!res || !content_type) return;
    
    strncpy(res->content_type, content_type, sizeof(res->content_type) - 1);
    http_response_set_header(res, "Content-Type", content_type);
}

void http_response_set_body(HttpResponse* res, const char* body, size_t len) {
    if (!res) return;
    
    // Free existing body if any
    if (res->body) {
        free(res->body);
        res->body = NULL;
    }
    
    if (body && len > 0) {
        res->body = (char*)malloc(len + 1); // +1 for null terminator
        if (res->body) {
            memcpy(res->body, body, len);
            res->body[len] = '\0';
            res->body_len = len;
        }
    } else {
        res->body_len = 0;
    }
}

void http_response_set_body_str(HttpResponse* res, const char* body) {
    if (!res) return;
    
    if (body) {
        http_response_set_body(res, body, strlen(body));
    } else {
        http_response_set_body(res, NULL, 0);
    }
}

void http_response_send(struct Connection* conn, HttpResponse* res, int epollFd) {
    if (!conn || !res) return;
    
    // Build HTTP response string
    // Estimate size: status line + headers + body
    size_t header_buf_size = 1024;
    for (int i = 0; i < res->header_count; i++) {
        header_buf_size += strlen(res->headers[i].key) + strlen(res->headers[i].value) + 4;
    }
    
    char* header_buf = (char*)malloc(header_buf_size);
    if (!header_buf) {
        log_system(LOG_ERROR, "Response: Failed to allocate header buffer");
        return;
    }
    
    // Build status line
    int offset = snprintf(header_buf, header_buf_size,
                          "HTTP/1.1 %d %s\r\n",
                          res->status_code, res->status_text);
    
    // Add Connection: close (we don't support keep-alive yet)
    offset += snprintf(header_buf + offset, header_buf_size - offset,
                       "Connection: close\r\n");
    
    // Add Content-Length
    offset += snprintf(header_buf + offset, header_buf_size - offset,
                       "Content-Length: %zu\r\n", res->body_len);
    
    // Add custom headers
    for (int i = 0; i < res->header_count; i++) {
        offset += snprintf(header_buf + offset, header_buf_size - offset,
                           "%s: %s\r\n",
                           res->headers[i].key, res->headers[i].value);
    }
    
    // End of headers
    offset += snprintf(header_buf + offset, header_buf_size - offset, "\r\n");
    
    // Queue header for writing
    queue_data_for_writing(conn, header_buf, offset, epollFd);
    
    // Queue body for writing (if any)
    if (res->body && res->body_len > 0) {
        queue_data_for_writing(conn, res->body, res->body_len, epollFd);
    }
    
    log_system(LOG_DEBUG, "Response: Sent %d %s with %zu bytes body",
               res->status_code, res->status_text, res->body_len);
    
    free(header_buf);
}

void http_response_free(HttpResponse* res) {
    if (!res) return;
    
    // Free headers
    for (int i = 0; i < res->header_count; i++) {
        free(res->headers[i].key);
        free(res->headers[i].value);
    }
    
    // Free body
    if (res->body) {
        free(res->body);
    }
    
    // Reset struct
    memset(res, 0, sizeof(HttpResponse));
}

// ============================================================================
// Quick Response Helpers
// ============================================================================

void http_send_json(struct Connection* conn, int status_code, const char* json_body, int epollFd) {
    HttpResponse res;
    http_response_init(&res, status_code);
    http_response_set_content_type(&res, "application/json");
    http_response_set_body_str(&res, json_body);
    http_response_send(conn, &res, epollFd);
    http_response_free(&res);
}

void http_send_text(struct Connection* conn, int status_code, const char* text_body, int epollFd) {
    HttpResponse res;
    http_response_init(&res, status_code);
    http_response_set_content_type(&res, "text/plain; charset=utf-8");
    http_response_set_body_str(&res, text_body);
    http_response_send(conn, &res, epollFd);
    http_response_free(&res);
}

void http_send_error(struct Connection* conn, int status_code, const char* message, int epollFd) {
    const char* error_message = message;
    
    // Use default message if not provided
    if (!error_message) {
        error_message = get_status_text(status_code);
    }
    
    HttpResponse res;
    http_response_init(&res, status_code);
    http_response_set_content_type(&res, "text/plain; charset=utf-8");
    http_response_set_body_str(&res, error_message);
    http_response_send(conn, &res, epollFd);
    http_response_free(&res);
}

// ============================================================================
// Phase 3: JSON Document Response
// ============================================================================

void http_send_json_doc(struct Connection* conn, int status_code, yyjson_mut_doc* doc, int epollFd) {
    if (!doc) {
        log_system(LOG_WARNING, "Response: http_send_json_doc called with NULL document");
        http_send_error(conn, 500, "Internal Server Error: NULL JSON document", epollFd);
        return;
    }
    
    // Serialize the mutable document to string
    size_t json_len = 0;
    char* json_str = yyjson_mut_write(doc, 0, &json_len);
    
    if (!json_str) {
        log_system(LOG_ERROR, "Response: Failed to serialize JSON document");
        http_send_error(conn, 500, "Internal Server Error: JSON serialization failed", epollFd);
        return;
    }
    
    // Send the JSON response
    HttpResponse res;
    http_response_init(&res, status_code);
    http_response_set_content_type(&res, "application/json");
    http_response_set_body(&res, json_str, json_len);
    http_response_send(conn, &res, epollFd);
    http_response_free(&res);
    
    // Free the serialized string (allocated by yyjson)
    free(json_str);
    
    log_system(LOG_DEBUG, "Response: Sent JSON document (%zu bytes)", json_len);
}





