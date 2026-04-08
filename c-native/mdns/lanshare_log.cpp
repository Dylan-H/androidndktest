/*
    LAN-Share Native Logging Implementation
    
    Copyright (C) 2026 LAN-Share Project
*/

#include "lanshare_log.h"

#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

// Default buffer size for log messages
constexpr size_t LOG_BUFFER_SIZE = 4096;

// Global log state
struct LogState {
    lanshare_log_callback_t callback = nullptr;
    lanshare_log_level_t min_level = LOG_LEVEL_DEBUG;
    std::mutex mutex;
};

LogState g_log_state;

const char* level_to_string(lanshare_log_level_t level) {
    switch (level) {
        case LOG_LEVEL_VERBOSE: return "V";
        case LOG_LEVEL_DEBUG:   return "D";
        case LOG_LEVEL_INFO:    return "I";
        case LOG_LEVEL_WARN:    return "W";
        case LOG_LEVEL_ERROR:   return "E";
        case LOG_LEVEL_FATAL:   return "F";
        default:                return "?";
    }
}

// Default logger using printf
void default_logger(lanshare_log_level_t level, const char* tag, const char* message) {
    const char* level_str = level_to_string(level);
    printf("[%s/%s] %s\n", level_str, tag, message);
}

} // anonymous namespace

extern "C" {

void lanshare_log_init(lanshare_log_callback_t callback) {
    std::lock_guard<std::mutex> lock(g_log_state.mutex);
    g_log_state.callback = callback;
    g_log_state.min_level = LOG_LEVEL_DEBUG;
}

void lanshare_log_set_callback(lanshare_log_callback_t callback) {
    std::lock_guard<std::mutex> lock(g_log_state.mutex);
    g_log_state.callback = callback;
}

void lanshare_log_set_level(lanshare_log_level_t level) {
    std::lock_guard<std::mutex> lock(g_log_state.mutex);
    g_log_state.min_level = level;
}

lanshare_log_level_t lanshare_log_get_level(void) {
    std::lock_guard<std::mutex> lock(g_log_state.mutex);
    return g_log_state.min_level;
}

void lanshare_log_v(lanshare_log_level_t level, const char* tag, const char* format, va_list args) {
    {
        std::lock_guard<std::mutex> lock(g_log_state.mutex);
        if (level < g_log_state.min_level) {
            return;
        }
    }
    
    char buffer[LOG_BUFFER_SIZE];
    buffer[0] = '\0';
    
    // Format the message
    int ret = vsnprintf(buffer, sizeof(buffer), format, args);
    if (ret < 0) {
        strncpy(buffer, "<log format error>", sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';
    } else if ((size_t)ret >= sizeof(buffer)) {
        // Truncated, ensure null termination
        buffer[sizeof(buffer) - 1] = '\0';
    }
    
    // Call the appropriate logger
    lanshare_log_callback_t callback;
    {
        std::lock_guard<std::mutex> lock(g_log_state.mutex);
        callback = g_log_state.callback;
    }
    
    if (callback) {
        callback(level, tag, buffer);
    } else {
        default_logger(level, tag, buffer);
    }
}

void lanshare_log(lanshare_log_level_t level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    lanshare_log_v(level, tag, format, args);
    va_end(args);
}

} // extern "C"
