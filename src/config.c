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

    if (!filepath) {
        return; // Use defaults if no file path is provided
    }

    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        // This is not a fatal error, just means we use defaults.
        // We might want to log this at a WARNING level once the logger is initialized.
        return;
    }

    char line[512];
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
        } else if (strcmp(key, "DocumentRoot") == 0) {
            strcpy(config->document_root, trimmed_value);
        } else if (strcmp(key, "LogPath") == 0) {
            strcpy(config->log_path, trimmed_value);
        } else if (strcmp(key, "LogLevel") == 0) {
            if (strcmp(trimmed_value, "DEBUG") == 0) config->log_level = LOG_DEBUG;
            else if (strcmp(trimmed_value, "INFO") == 0) config->log_level = LOG_INFO;
            else if (strcmp(trimmed_value, "WARNING") == 0) config->log_level = LOG_WARNING;
            else if (strcmp(trimmed_value, "ERROR") == 0) config->log_level = LOG_ERROR;
        } else if (strcmp(key, "LogTarget") == 0) {
            if (strcmp(trimmed_value, "stdout") == 0) config->log_target = LOG_TARGET_STDOUT;
            else if (strcmp(trimmed_value, "file") == 0) config->log_target = LOG_TARGET_FILE;
        }
    }

    fclose(fp);
} 