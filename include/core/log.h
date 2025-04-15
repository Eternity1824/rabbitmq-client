#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log levels
 */
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} LogLevel;

/**
 * Set global log level
 */
void log_set_level(LogLevel level);

/**
 * Set log output file (NULL for stdout)
 */
void log_set_output(FILE* output);

/**
 * Log functions for different levels
 */
void log_debug(const char* format, ...);
void log_info(const char* format, ...);
void log_warning(const char* format, ...);
void log_error(const char* format, ...);
void log_fatal(const char* format, ...);

/**
 * Macro helpers for convenient logging
 */
#define LOG_DEBUG(...) log_debug(__VA_ARGS__)
#define LOG_INFO(...) log_info(__VA_ARGS__)
#define LOG_WARNING(...) log_warning(__VA_ARGS__)
#define LOG_ERROR(...) log_error(__VA_ARGS__)
#define LOG_FATAL(...) log_fatal(__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */ 