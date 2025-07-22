#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Helper function to trim leading/trailing whitespace
static char* trim(char* str) {
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

void loadConfig(const char* filepath, ServerConfig* config) {
    // 1. Set default values
    config->listen_port = 8080;
    strcpy(config->document_root, "www");
    strcpy(config->log_path, "log");
    config->log_level = LOG_INFO;
    config->log_target = LOG_TARGET_FILE;
    // New defaults
    config->jwt_enabled = 1;
    strcpy(config->jwt_secret, "a-very-secret-and-long-key-that-is-at-least-32-bytes");
    config->mime_enabled = 1;

    if (!filepath) {
        log_system(LOG_INFO, "Config: No config file provided, using default settings.");
        return; // Use defaults if no file path is provided
    }

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        // This is not a fatal error, just means we use defaults.
        // We might want to log this at a WARNING level once the logger is initialized.
        log_system(LOG_WARNING, "Config: Could not open config file '%s'. Using default settings.", filepath);
        return;
    }

    char line[512];
    log_system(LOG_DEBUG, "Config: Reading configuration from '%s'.", filepath);
    while (fgets(line, sizeof(line), fp)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        char key[256], value[256];
        if (sscanf(line, "%255s = %255s", key, value) != 2) {
            continue;
        }

        char* trimmed_value = trim(value);

        if (strcmp(key, "ListenPort") == 0) {
            config->listen_port = atoi(trimmed_value);
            log_system(LOG_DEBUG, "Config: Set %s = %d", key, config->listen_port);
        } else if (strcmp(key, "DocumentRoot") == 0) {
            strcpy(config->document_root, trimmed_value);
            log_system(LOG_DEBUG, "Config: Set %s = %s", key, config->document_root);
        } else if (strcmp(key, "LogPath") == 0) {
            strcpy(config->log_path, trimmed_value);
            log_system(LOG_DEBUG, "Config: Set %s = %s", key, config->log_path);
        } else if (strcmp(key, "LogLevel") == 0) {
            if (strcmp(trimmed_value, "DEBUG") == 0) config->log_level = LOG_DEBUG;
            else if (strcmp(trimmed_value, "INFO") == 0) config->log_level = LOG_INFO;
            else if (strcmp(trimmed_value, "WARNING") == 0) config->log_level = LOG_WARNING;
            else if (strcmp(trimmed_value, "ERROR") == 0) config->log_level = LOG_ERROR;
            log_system(LOG_DEBUG, "Config: Set %s = %s", key, trimmed_value);
        } else if (strcmp(key, "LogTarget") == 0) {
            if (strcmp(trimmed_value, "stdout") == 0) config->log_target = LOG_TARGET_STDOUT;
            else if (strcmp(trimmed_value, "file") == 0) config->log_target = LOG_TARGET_FILE;
            log_system(LOG_DEBUG, "Config: Set %s = %s", key, trimmed_value);
        } else if (strcmp(key, "JwtEnabled") == 0) {
            config->jwt_enabled = atoi(trimmed_value);
            log_system(LOG_DEBUG, "Config: Set %s = %d", key, config->jwt_enabled);
        } else if (strcmp(key, "JwtSecret") == 0) {
            strcpy(config->jwt_secret, trimmed_value);
            log_system(LOG_DEBUG, "Config: Set %s = [SECRET]", key);
        } else if (strcmp(key, "MimeEnabled") == 0) {
            config->mime_enabled = atoi(trimmed_value);
            log_system(LOG_DEBUG, "Config: Set %s = %d", key, config->mime_enabled);
        }
    }

    fclose(fp);
} 