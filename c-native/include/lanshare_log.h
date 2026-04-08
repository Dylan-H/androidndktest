/*
    LAN-Share Native Logging Interface
    Cross-platform logging abstraction
    
    Copyright (C) 2026 LAN-Share Project
*/

#ifndef LANSHARE_LOG_H
#define LANSHARE_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log levels */
typedef enum {
    LOG_LEVEL_VERBOSE = 0,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} lanshare_log_level_t;

/* Log callback function type */
typedef void (*lanshare_log_callback_t)(lanshare_log_level_t level, const char* tag, const char* message);

/**
 * Initialize logging system
 * @param callback Log callback function, NULL to use default (printf)
 */
void lanshare_log_init(lanshare_log_callback_t callback);

/**
 * Set log callback at runtime
 * @param callback Log callback function
 */
void lanshare_log_set_callback(lanshare_log_callback_t callback);

/**
 * Set minimum log level
 * @param level Minimum log level to output
 */
void lanshare_log_set_level(lanshare_log_level_t level);

/**
 * Get current log level
 * @return Current minimum log level
 */
lanshare_log_level_t lanshare_log_get_level(void);

/**
 * Core logging function
 * @param level Log level
 * @param tag Log tag/category
 * @param format printf-style format string
 * @param ... Variable arguments
 */
void lanshare_log(lanshare_log_level_t level, const char* tag, const char* format, ...);

/**
 * Log with va_list
 * @param level Log level
 * @param tag Log tag/category
 * @param format printf-style format string
 * @param args Variable argument list
 */
void lanshare_log_v(lanshare_log_level_t level, const char* tag, const char* format, va_list args);

/* Convenience macros */
#define LOGV(tag, ...) lanshare_log(LOG_LEVEL_VERBOSE, tag, __VA_ARGS__)
#define LOGD(tag, ...) lanshare_log(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOGI(tag, ...) lanshare_log(LOG_LEVEL_INFO, tag, __VA_ARGS__)
#define LOGW(tag, ...) lanshare_log(LOG_LEVEL_WARN, tag, __VA_ARGS__)
#define LOGE(tag, ...) lanshare_log(LOG_LEVEL_ERROR, tag, __VA_ARGS__)
#define LOGF(tag, ...) lanshare_log(LOG_LEVEL_FATAL, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* LANSHARE_LOG_H */
