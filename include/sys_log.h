#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <Arduino.h>
#include <cstdarg>

#define MAX_LOG_LINES 80
#define MAX_LOG_LINE_LEN 160

typedef struct {
    uint32_t timestamp_ms;
    char text[MAX_LOG_LINE_LEN];
} sys_log_entry_t;

/**
 * @brief Initialize system circular log buffer.
 */
void sys_log_init();

/**
 * @brief Log a message to both Serial and the web circular buffer.
 */
void sys_log(const char* fmt, ...);

/**
 * @brief Get JSON representation of all log entries for web serial monitor.
 * @param buf Output buffer for JSON string
 * @param max_len Size of buffer
 */
void sys_log_get_json(char* buf, size_t max_len);

/**
 * @brief Clear all log entries.
 */
void sys_log_clear();

#endif // SYS_LOG_H
