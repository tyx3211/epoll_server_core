#include "server.h"
#include <stddef.h> // For NULL

int main() {
    // Pass NULL to use the default configuration for now.
    // In the future, we could pass a path like "conf/server.conf".
    startServer(NULL);
    return 0;
} 