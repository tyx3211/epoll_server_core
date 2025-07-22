#define _GNU_SOURCE // For vasprintf
#include "logger.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h> // For bool type

// --- Pre-initialization Buffer ---
typedef struct {
    LogLevel level;
    char* message;
    time_t timestamp;
} BufferedLog;

static BufferedLog* log_buffer = NULL;
static size_t buffer_count = 0;
static size_t buffer_capacity = 0;
#define INITIAL_BUFFER_CAPACITY 32

// --- Logger State ---
static struct {
    LogLevel level;
    LogTarget target;
    FILE* system_log_fp;
    FILE* access_log_fp;
    bool is_initialized;
} L;

static const char* level_strings[] = {
    "DEBUG", "INFO", "WARNING", "ERROR"
};

static void flush_and_free_buffer() {
    if (!log_buffer) {
        return;
    }

    // Now that the real logger is initialized, replay buffered messages
    // We set the flag to true temporarily to allow log_system to work directly
    bool original_state = L.is_initialized;
    L.is_initialized = true;

    for (size_t i = 0; i < buffer_count; i++) {
        // We reuse the main log_system logic, but need to reconstruct the prefix
        FILE* out = (L.target == LOG_TARGET_STDOUT) ? stdout : L.system_log_fp;
        if (!out) continue;
        
        if (log_buffer[i].level >= L.level) {
             struct tm* tm_info = localtime(&log_buffer[i].timestamp);
             char time_buf[26];
             strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
             fprintf(out, "[%s] [%s] %s\n", time_buf, level_strings[log_buffer[i].level], log_buffer[i].message);
             fflush(out);
        }
        
        free(log_buffer[i].message); // Free the message string
    }

    free(log_buffer); // Free the buffer array itself
    log_buffer = NULL;
    buffer_count = 0;
    buffer_capacity = 0;

    L.is_initialized = original_state; // Restore state
}


int logger_init(LogLevel level, LogTarget target, const char* log_path) {
    // If logger was already initialized, shut it down first to reconfigure
    if (L.is_initialized) {
        logger_shutdown();
    }
    
    L.level = level;
    L.target = target;
    L.system_log_fp = NULL;
    L.access_log_fp = NULL;
    L.is_initialized = false; // Set to false until setup is complete

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
    
    L.is_initialized = true;
    
    // Now that the logger is configured, flush any buffered logs
    flush_and_free_buffer();

    return 0;
}

void logger_shutdown() {
    // Before shutting down, make sure any buffered logs are flushed.
    // This will only work if the logger was at least partially initialized.
    if (L.is_initialized) {
        flush_and_free_buffer();
    }
    
    if (L.system_log_fp) {
        fclose(L.system_log_fp);
        L.system_log_fp = NULL;
    }
    if (L.access_log_fp) {
        fclose(L.access_log_fp);
        L.access_log_fp = NULL;
    }
    L.is_initialized = false;
}

void log_system(LogLevel level, const char* format, ...) {
    // If the logger is not yet initialized, buffer the message.
    if (!L.is_initialized) {
        if (buffer_count >= buffer_capacity) {
            size_t new_capacity = (buffer_capacity == 0) ? INITIAL_BUFFER_CAPACITY : buffer_capacity * 2;
            BufferedLog* new_buffer = realloc(log_buffer, new_capacity * sizeof(BufferedLog));
            if (!new_buffer) {
                // Cannot allocate buffer, message is lost.
                return;
            }
            log_buffer = new_buffer;
            buffer_capacity = new_capacity;
        }

        va_list args;
        va_start(args, format);
        char* message;
        // vasprintf is a GNU extension that allocates the string for us.
        if (vasprintf(&message, format, args) == -1) {
            va_end(args);
            return; // Allocation failed
        }
        va_end(args);

        log_buffer[buffer_count].level = level;
        log_buffer[buffer_count].message = message;
        log_buffer[buffer_count].timestamp = time(NULL);
        buffer_count++;

        return;
    }
    
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
    // Access logs are not buffered as they are tied to live requests
    // which only happen after the server is fully started.
    if (!L.is_initialized) return;
    
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