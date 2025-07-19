#include "server.h"
#include <stdio.h>

int main(int argc, char* argv[]) {
    const char* config_path = NULL;
    if (argc > 1) {
        config_path = argv[1];
        printf("Using configuration file: %s\n", config_path);
    } else {
        printf("No configuration file specified, using default settings.\n");
    }

    startServer(config_path);
    return 0;
} 