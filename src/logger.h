#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} LogLevel;

typedef enum {
    LOG_TARGET_STDOUT,
    LOG_TARGET_FILE
} LogTarget;

/**
 * Initializes the logger.
 * @param level The minimum level to log.
 * @param target Where to output logs.
 * @param log_path The directory for log files (used if target is LOG_TARGET_FILE).
 * @return 0 on success, -1 on failure.
 */
int logger_init(LogLevel level, LogTarget target, const char* log_path);

/**
 * Shuts down the logger, closing any open files.
 */
void logger_shutdown();

/**
 * Logs a system message with a given level.
 * It works like printf.
 */
void log_system(LogLevel level, const char* format, ...);

/**
 * Logs an access message.
 */
void log_access(const char* remote_addr, const char* method, const char* uri, int status_code);

#endif // LOGGER_H 