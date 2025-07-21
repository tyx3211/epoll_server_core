#define _DEFAULT_SOURCE // For strdup
#include "api.h"
#include "server.h" // For queue_data_for_writing
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h" // for get_query_param

void handle_api_login(Connection* conn, ServerConfig* config, int epollFd) {
    char* username = NULL;
    char* password = NULL;
    const char* response_body;

    if (conn->request.body) {
        // Login still uses POST body
        username = get_query_param(conn->request.body, "username");
        password = get_query_param(conn->request.body, "password");
    }

    printf("username: %s, password: %s\n", username, password);

    if (username && password) {
        log_system(LOG_INFO, "Login attempt: user=%s, pass=%s", username, password);
        // Simple validation logic
        if (strcmp(username, "admin") == 0 && strcmp(password, "123456") == 0) {
            response_body = "{\"status\":\"success\", \"message\":\"Login successful!\"}";
        } else {
            response_body = "{\"status\":\"error\", \"message\":\"Invalid credentials.\"}";
        }
    } else {
        response_body = "{\"status\":\"error\", \"message\":\"Missing username or password.\"}";
    }

    char header[512];
    int headerLen = snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Connection: close\r\n"
                             "Content-Type: application/json\r\n"
                             "Content-Length: %ld\r\n\r\n",
                             strlen(response_body));
    
    queue_data_for_writing(conn, header, headerLen, epollFd);
    queue_data_for_writing(conn, response_body, strlen(response_body), epollFd);

    free(username);
    free(password);
}


void handle_api_search(Connection* conn, ServerConfig* config, int epollFd) {
    char* filename_key = NULL;
    char* search_key = NULL;
    char response_buffer[4096] = {0}; // Buffer for the search results
    char error_msg[256] = {0};

    // SEARCH IS NOW A GET REQUEST, PARSE FROM QUERY STRING
    if (conn->request.raw_query_string) {
        filename_key = get_query_param(conn->request.raw_query_string, "key1");
        search_key = get_query_param(conn->request.raw_query_string, "key2");
    }

    if (filename_key && search_key) {
        char filepath[512];
        // We assume search files are in a 'www/data' directory for security
        snprintf(filepath, sizeof(filepath), "www/data/%s.txt", filename_key);
        
        // Prevent path traversal
        if (strstr(filepath, "..") != NULL) {
             snprintf(error_msg, sizeof(error_msg), "Invalid filename.");
        } else {
            FILE* fp = fopen(filepath, "r");
            if (fp) {
                char line[1024];
                while(fgets(line, sizeof(line), fp)) {
                    if (strstr(line, search_key)) {
                        strncat(response_buffer, line, sizeof(response_buffer) - strlen(response_buffer) - 1);
                    }
                }
                fclose(fp);
            } else {
                 snprintf(error_msg, sizeof(error_msg), "File not found: %s.txt", filename_key);
            }
        }
    } else {
        snprintf(error_msg, sizeof(error_msg), "Missing key1 or key2.");
    }
    
    const char* body = strlen(error_msg) > 0 ? error_msg : (strlen(response_buffer) > 0 ? response_buffer : "No results found.");
    
    char header[512];
    int headerLen = snprintf(header, sizeof(header),
                             "HTTP/1.1 200 OK\r\n"
                             "Connection: close\r\n"
                             "Content-Type: text/plain; charset=utf-8\r\n"
                             "Content-Length: %ld\r\n\r\n",
                             strlen(body));

    queue_data_for_writing(conn, header, headerLen, epollFd);
    queue_data_for_writing(conn, body, strlen(body), epollFd);

    free(filename_key);
    free(search_key);
} 