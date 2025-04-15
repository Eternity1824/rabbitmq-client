#include "core/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

// Static variables
static LogLevel current_level = LOG_LEVEL_INFO;
static FILE* log_output = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Level names for printing
static const char* level_names[] = {
    "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
};

// Initialize log output to stdout if not set
static void init_log_output() {
    if (!log_output) {
        log_output = stdout;
    }
}

// Set log level
void log_set_level(LogLevel level) {
    if (level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_FATAL) {
        current_level = level;
    }
}

// Set log output file
void log_set_output(FILE* output) {
    pthread_mutex_lock(&log_mutex);
    log_output = output ? output : stdout;
    pthread_mutex_unlock(&log_mutex);
}

// Internal log function used by all log levels
static void log_internal(LogLevel level, const char* format, va_list args) {
    if (level < current_level) {
        return;
    }

    pthread_mutex_lock(&log_mutex);
    init_log_output();

    // Get current time
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

    // Get thread ID
    pthread_t tid = pthread_self();

    // Print log header
    fprintf(log_output, "[%s] [%s] [%lu] ", time_str, level_names[level], (unsigned long)tid);

    // Print log message
    vfprintf(log_output, format, args);
    fprintf(log_output, "\n");

    // Flush output to ensure immediate logging
    fflush(log_output);

    pthread_mutex_unlock(&log_mutex);

    // If fatal, exit the program
    if (level == LOG_LEVEL_FATAL) {
        exit(EXIT_FAILURE);
    }
}

// Debug level log
void log_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_DEBUG, format, args);
    va_end(args);
}

// Info level log
void log_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_INFO, format, args);
    va_end(args);
}

// Warning level log
void log_warning(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_WARNING, format, args);
    va_end(args);
}

// Error level log
void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_ERROR, format, args);
    va_end(args);
}

// Fatal level log
void log_fatal(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_internal(LOG_LEVEL_FATAL, format, args);
    va_end(args);
    // Should never reach here because log_internal exits for fatal errors
} 