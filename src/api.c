#define _DEFAULT_SOURCE // For strdup
#include "api.h"
#include "server.h" // For queue_data_for_writing
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h" // for get_query_param
#include <stdbool.h> // For bool
#include <l8w8jwt/encode.h>
#include <time.h>
#include "auth.h" // Include our new auth module

// IMPORTANT: This should be loaded from a secure configuration, not hardcoded!
// const char JWT_SECRET[] = "a-very-secret-and-long-key-that-is-at-least-32-bytes";

void handle_api_login(Connection* conn, ServerConfig* config, int epollFd) {
    char* username = NULL;
    char* password = NULL;
    const char* response_body;
    bool credentials_valid = false; // Moved declaration to top of function scope

    if (conn->request.body) {
        // Login still uses POST body
        username = get_query_param(conn->request.body, "username");
        password = get_query_param(conn->request.body, "password");
    }

    // printf("username: %s, password: %s\n", username, password);

    if (username && password) {
        log_system(LOG_INFO, "Login attempt: user=%s", username);

        // --- CSV-based user validation ---
        FILE* fp = fopen("www/data/users.csv", "r");
        if (fp) {
            char line[256];
            char file_user[128], file_pass[128];
            
            // Skip header
            if (fgets(line, sizeof(line), fp)) {
                while (fgets(line, sizeof(line), fp)) {
                    // Very simple CSV parsing: expects "user,pass\n"
                    if (sscanf(line, "%127[^,],%127s", file_user, file_pass) == 2) {
                        if (strcmp(file_user, username) == 0 && strcmp(file_pass, password) == 0) {
                            credentials_valid = true;
                            break;
                        }
                    }
                }
            }
            fclose(fp);
        } else {
            log_system(LOG_ERROR, "Could not open users.csv");
        }
        
        if (credentials_valid) {
            // --- JWT Generation or Mock Token ---
            if (config->jwt_enabled) {
                char* jwt = NULL;
                size_t jwt_length;
                struct l8w8jwt_encoding_params params;
                l8w8jwt_encoding_params_init(&params);

                params.alg = L8W8JWT_ALG_HS256;

                params.sub = username; // Subject of the token is the user
                params.iss = "my-web-server";

                params.iat = time(NULL);
                params.exp = time(NULL) + (15 * 60); // Expires in 15 minutes

                params.secret_key = (unsigned char*)config->jwt_secret;
                params.secret_key_length = strlen(config->jwt_secret);

                params.out = &jwt;
                params.out_length = &jwt_length;
                
                int r = l8w8jwt_encode(&params);
                if (r == L8W8JWT_SUCCESS && jwt) {
                    char json_response[1024];
                    snprintf(json_response, sizeof(json_response),
                             "{\"status\":\"success\", \"token\":\"%s\"}", jwt);
                    response_body = strdup(json_response); // Duplicate it as it's on the stack
                } else {
                    log_system(LOG_ERROR, "Failed to create JWT token: %d", r);
                    response_body = "{\"status\":\"error\", \"message\":\"Internal server error: could not create token.\"}";
                }

                l8w8jwt_free(jwt); // Free the token created by the library
            } else {
                // JWT is disabled, return username as a mock token
                char json_response[1024];
                snprintf(json_response, sizeof(json_response),
                         "{\"status\":\"success\", \"token\":\"%s\"}", username);
                response_body = strdup(json_response);
            }
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

    // If we duplicated the response body, we need to free it
    if (credentials_valid) { // This condition is fine, strdup was used in both jwt and mock paths
        free((void*)response_body);
    }

    free(username);
    free(password);
}


void handle_api_search(Connection* conn, ServerConfig* config, int epollFd) {
    (void)config;  // Mark as unused
    (void)epollFd; // Mark as unused

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

void handle_api_me(Connection* conn, ServerConfig* config, int epollFd) {
    char* authed_user = authenticate_request(conn, config);

    if (authed_user) {
        char response_body[256];
        snprintf(response_body, sizeof(response_body), "{\"status\":\"success\", \"user\":{\"username\":\"%s\"}}", authed_user);

        char header[512];
        int headerLen = snprintf(header, sizeof(header),
                                 "HTTP/1.1 200 OK\r\n"
                                 "Connection: close\r\n"
                                 "Content-Type: application/json\r\n"
                                 "Content-Length: %ld\r\n\r\n",
                                 strlen(response_body));
        
        queue_data_for_writing(conn, header, headerLen, epollFd);
        queue_data_for_writing(conn, response_body, strlen(response_body), epollFd);
        
        free(authed_user); // Free the username string returned by the auth function
    } else {
        // Authentication failed
        log_access(conn->client_ip, conn->request.method, conn->request.raw_uri, 401);
        char response[] = "HTTP/1.1 401 Unauthorized\r\nConnection: close\r\n\r\nUnauthorized";
        queue_data_for_writing(conn, response, sizeof(response) - 1, epollFd);
    }
} 