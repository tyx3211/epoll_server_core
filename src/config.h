#ifndef CONFIG_H
#define CONFIG_H

#include "logger.h" // For LogLevel and LogTarget

typedef struct {
    int listen_port;
    char document_root[256];
    char log_path[256];
    LogLevel log_level;
    LogTarget log_target;
} ServerConfig;

/**
 * Loads configuration from the given file path into the config struct.
 * Sets default values first, then overwrites with values from the file.
 * @param filepath Path to the config file. Can be NULL, in which case only defaults are used.
 * @param config Pointer to the ServerConfig struct to fill.
 */
void loadConfig(const char* filepath, ServerConfig* config);

#endif // CONFIG_H 