#define _DEFAULT_SOURCE // For strdup
#include "router.h"
#include <string.h>
#include <stdlib.h>
#include "logger.h" // Add logger for debug messages

#define MAX_ROUTES 64

// Simple structure to hold a single route
typedef struct {
    char* method;
    char* path;
    RouteHandler handler;
} Route;

// Global routing table
static struct {
    Route routes[MAX_ROUTES];
    int count;
} R;

void router_init() {
    R.count = 0;
    // We can use memset for a safer initialization
    memset(&R, 0, sizeof(R));
}

void router_add_route(const char* method, const char* path, RouteHandler handler) {
    if (R.count < MAX_ROUTES) {
        // We must duplicate the strings, as the originals may not be persistent
        R.routes[R.count].method = strdup(method);
        R.routes[R.count].path = strdup(path);
        R.routes[R.count].handler = handler;
        R.count++;
        log_system(LOG_DEBUG, "Router: Registered route [%s] %s", method, path);
    } else {
        log_system(LOG_ERROR, "Router: Could not add route [%s] %s, routing table full.", method, path);
    }
}

RouteHandler router_find_handler(const char* method, const char* path) {
    for (int i = 0; i < R.count; i++) {
        // Simple string comparison for now.
        // A more advanced router might support wildcards or regex.
        if (strcmp(R.routes[i].method, method) == 0 && strcmp(R.routes[i].path, path) == 0) {
            log_system(LOG_DEBUG, "Router: Matched request to handler for [%s] %s", method, path);
            return R.routes[i].handler;
        }
    }
    log_system(LOG_DEBUG, "Router: No matching handler found for [%s] %s", method, path);
    return NULL;
} 