#ifndef API_H
#define API_H

#include "router.h" // Includes http.h and config.h

/**
 * @brief Handles the /api/login POST request.
 * A simple example to show how to handle form data.
 */
void handle_api_login(Connection* conn, ServerConfig* config, int epollFd);

/**
 * @brief Handles the /api/search POST request.
 * Implements the search logic described in the requirements.
 */
void handle_api_search(Connection* conn, ServerConfig* config, int epollFd);

#endif // API_H 