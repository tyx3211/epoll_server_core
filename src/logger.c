#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

// Global logger state
static struct {
    LogLevel level;
    LogTarget target;
    FILE* system_log_fp;
    FILE* access_log_fp;
} L;

static const char* level_strings[] = {
    "DEBUG", "INFO", "WARNING", "ERROR"
};

int logger_init(LogLevel level, LogTarget target, const char* log_path) {
    L.level = level;
    L.target = target;
    L.system_log_fp = NULL;
    L.access_log_fp = NULL;

    if (L.target == LOG_TARGET_FILE) {
        char path_buf[256];
        
        snprintf(path_buf, sizeof(path_buf), "%s/system.log", log_path);
        L.system_log_fp = fopen(path_buf, "a");
        if (!L.system_log_fp) {
            perror("fopen system.log");
            return -1;
        }

        snprintf(path_buf, sizeof(path_buf), "%s/access.log", log_path);
        L.access_log_fp = fopen(path_buf, "a");
        if (!L.access_log_fp) {
            perror("fopen access.log");
            fclose(L.system_log_fp);
            return -1;
        }
    }
    return 0;
}

void logger_shutdown() {
    if (L.system_log_fp) {
        fclose(L.system_log_fp);
    }
    if (L.access_log_fp) {
        fclose(L.access_log_fp);
    }
}

void log_system(LogLevel level, const char* format, ...) {
    if (level < L.level) {
        return;
    }

    FILE* out = (L.target == LOG_TARGET_STDOUT) ? stdout : L.system_log_fp;
    if (!out) return;

    // Get current time
    time_t timer = time(NULL);
    struct tm* tm_info = localtime(&timer);
    char time_buf[26];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Print log prefix
    fprintf(out, "[%s] [%s] ", time_buf, level_strings[level]);

    // Print user message
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);

    // Print newline
    fprintf(out, "\n");
    fflush(out);
}

void log_access(const char* remote_addr, const char* method, const char* uri, int status_code) {
    FILE* out = (L.target == LOG_TARGET_STDOUT) ? stdout : L.access_log_fp;
    if (!out) return;

    // Get current time
    time_t timer = time(NULL);
    struct tm* tm_info = localtime(&timer);
    char time_buf[26];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Format: [Time] IP "METHOD URI HTTP/1.1" STATUS
    // We assume HTTP/1.1 for now.
    fprintf(out, "[%s] %s \"%s %s HTTP/1.1\" %d\n", time_buf, remote_addr ? remote_addr : "-", method, uri, status_code);
    fflush(out);
} 