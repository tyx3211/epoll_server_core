#ifndef ROUTER_H
#define ROUTER_H

#include "http.h"
#include "config.h"

// Handler function pointer type for API routes
typedef void (*RouteHandler)(Connection* conn, ServerConfig* config, int epollFd);

/**
 * @brief Initializes the router system. Should be called once at startup.
 */
void router_init();

/**
 * @brief Adds a new route to the routing table.
 * 
 * This function should be called before starting the server to register all
 * API endpoints. This function is not thread-safe.
 * 
 * @param method The HTTP method (e.g., "GET", "POST").
 * @param path The URL path (e.g., "/api/login").
 * @param handler The function pointer to handle requests for this route.
 */
void router_add_route(const char* method, const char* path, RouteHandler handler);

/**
 * @brief Finds a handler for a given method and path.
 * 
 * @param method The HTTP method of the incoming request.
 * @param path The URL path of the incoming request.
 * @return A function pointer to the matched handler, or NULL if no route matches.
 */
RouteHandler router_find_handler(const char* method, const char* path);

#endif // ROUTER_H 