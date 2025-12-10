#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h> // Required for errno
#include <unistd.h> // Required for write
#include "logger.h" // Include logger for debug messages

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
                log_system(LOG_DEBUG, "Utils: Invalid hex sequence '%%%c%c' in urlDecode.", str[i+1], str[i+2]);
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
        log_system(LOG_DEBUG, "Utils: No file extension found for '%s', defaulting to octet-stream.", path);
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
    log_system(LOG_DEBUG, "Utils: Unknown file extension '%s' for path '%s', defaulting to octet-stream.", dot, path);
    return "application/octet-stream"; // Default binary type
}

char* get_query_param(const char* str, const char* key) {
    if (!str || !key) return NULL;
    log_system(LOG_DEBUG, "Utils: Parsing query string for key '%s'.", key);
    // We duplicate the string because strtok_r modifies it.
    char* str_copy = strdup(str);
    if (!str_copy) return NULL;

    char* value = NULL;
    char* saveptr;

    char* token = strtok_r(str_copy, "&", &saveptr);
    while (token != NULL) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0'; // Split key and value
            char* decoded_key = urlDecode(token);
            if (strcmp(decoded_key, key) == 0) {
                // Key matches, decode the value and store it
                char* decoded_value = urlDecode(eq + 1);
                value = strdup(decoded_value);
                log_system(LOG_DEBUG, "Utils: Found key '%s' with value '%s'.", key, value);
                free(decoded_value);
            }
            free(decoded_key);
            if (value) break; // Found, no need to continue
        }
        token = strtok_r(NULL, "&", &saveptr);
    }

    free(str_copy);
    return value;
}

// ============================================================================
// Phase 2: Pre-parsed Parameters API
// ============================================================================

#include "http.h"  // For HttpRequest, QueryParam
#include <strings.h> // For strcasecmp

int parse_params(const char* str, void* params_ptr, int max_params) {
    if (!str || !params_ptr || max_params <= 0) return 0;
    
    QueryParam* params = (QueryParam*)params_ptr;
    int count = 0;
    
    // Duplicate the string because we'll modify it
    char* str_copy = strdup(str);
    if (!str_copy) return 0;
    
    char* saveptr;
    char* token = strtok_r(str_copy, "&", &saveptr);
    
    while (token != NULL && count < max_params) {
        char* eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            char* decoded_key = urlDecode(token);
            char* decoded_value = urlDecode(eq + 1);
            
            if (decoded_key && decoded_value) {
                params[count].key = decoded_key;
                params[count].value = decoded_value;
                count++;
                log_system(LOG_DEBUG, "Utils: Parsed param[%d]: %s = %s", count - 1, decoded_key, decoded_value);
            } else {
                free(decoded_key);
                free(decoded_value);
            }
        }
        token = strtok_r(NULL, "&", &saveptr);
    }
    
    free(str_copy);
    return count;
}

// Helper to get Content-Type header value
static const char* get_content_type(const HttpRequest* req) {
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].key, "Content-Type") == 0) {
            return req->headers[i].value;
        }
    }
    return NULL;
}

void http_parse_all_params(HttpRequest* req) {
    if (!req) return;
    
    // Parse query string parameters
    if (req->raw_query_string && strlen(req->raw_query_string) > 0) {
        req->query_param_count = parse_params(
            req->raw_query_string, 
            req->query_params, 
            MAX_PARAMS
        );
        log_system(LOG_DEBUG, "Utils: Parsed %d query parameters.", req->query_param_count);
    }
    
    // Parse body based on Content-Type
    if (req->body && req->content_length > 0) {
        const char* content_type = get_content_type(req);
        if (content_type) {
            if (strstr(content_type, "application/x-www-form-urlencoded")) {
                // Parse form body parameters
                req->body_param_count = parse_params(
                    req->body, 
                    req->body_params, 
                    MAX_PARAMS
                );
                log_system(LOG_DEBUG, "Utils: Parsed %d body parameters (x-www-form-urlencoded).", req->body_param_count);
            } else if (strstr(content_type, "application/json")) {
                // Phase 3: Parse JSON body
                req->json_doc = yyjson_read(req->body, req->content_length, 0);
                if (req->json_doc) {
                    req->json_root = yyjson_doc_get_root(req->json_doc);
                    log_system(LOG_DEBUG, "Utils: Parsed JSON body successfully.");
                } else {
                    log_system(LOG_WARNING, "Utils: Failed to parse JSON body.");
                }
            }
        }
    }
}

const char* http_get_param(const HttpRequest* req, const char* key) {
    if (!req || !key) return NULL;
    
    // Search query params first
    const char* result = http_get_query_param(req, key);
    if (result) return result;
    
    // Then search body params
    return http_get_body_param(req, key);
}

const char* http_get_query_param(const HttpRequest* req, const char* key) {
    if (!req || !key) return NULL;
    
    for (int i = 0; i < req->query_param_count; i++) {
        if (strcmp(req->query_params[i].key, key) == 0) {
            return req->query_params[i].value;
        }
    }
    return NULL;
}

const char* http_get_body_param(const HttpRequest* req, const char* key) {
    if (!req || !key) return NULL;
    
    for (int i = 0; i < req->body_param_count; i++) {
        if (strcmp(req->body_params[i].key, key) == 0) {
            return req->body_params[i].value;
        }
    }
    return NULL;
}