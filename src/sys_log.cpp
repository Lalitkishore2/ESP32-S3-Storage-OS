#include "sys_log.h"
#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static sys_log_entry_t s_log_buffer[MAX_LOG_LINES];
static int s_head = 0;
static int s_count = 0;
static SemaphoreHandle_t s_log_mutex = NULL;

void sys_log_init() {
    s_log_mutex = xSemaphoreCreateMutex();
    s_head = 0;
    s_count = 0;
    memset(s_log_buffer, 0, sizeof(s_log_buffer));
}

void sys_log(const char* fmt, ...) {
    char line[MAX_LOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    // Print to physical hardware serial port
    Serial.println(line);

    // Push to circular buffer for web serial monitor
    if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        int idx = (s_head + s_count) % MAX_LOG_LINES;
        if (s_count == MAX_LOG_LINES) {
            // Overwrite oldest entry
            s_head = (s_head + 1) % MAX_LOG_LINES;
            idx = (s_head + MAX_LOG_LINES - 1) % MAX_LOG_LINES;
        } else {
            s_count++;
        }

        s_log_buffer[idx].timestamp_ms = millis();
        strncpy(s_log_buffer[idx].text, line, MAX_LOG_LINE_LEN - 1);
        s_log_buffer[idx].text[MAX_LOG_LINE_LEN - 1] = '\0';

        xSemaphoreGive(s_log_mutex);
    }
}

void sys_log_get_json(char* buf, size_t max_len) {
    if (!buf || max_len == 0) return;

    if (!s_log_mutex || xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        snprintf(buf, max_len, "[]");
        return;
    }

    size_t pos = 0;
    pos += snprintf(buf + pos, max_len - pos, "[");

    for (int i = 0; i < s_count; i++) {
        int idx = (s_head + i) % MAX_LOG_LINES;
        
        // Escape quotes & backslashes for JSON safety
        char escaped[MAX_LOG_LINE_LEN * 2];
        size_t e = 0;
        for (const char* p = s_log_buffer[idx].text; *p && e < sizeof(escaped) - 2; p++) {
            if (*p == '"' || *p == '\\') escaped[e++] = '\\';
            escaped[e++] = *p;
        }
        escaped[e] = '\0';

        pos += snprintf(buf + pos, max_len - pos,
            "%s{\"ts\":%u,\"text\":\"%s\"}",
            (i == 0) ? "" : ",",
            s_log_buffer[idx].timestamp_ms,
            escaped
        );

        if (pos >= max_len - 10) break; // Avoid overflow
    }

    snprintf(buf + pos, max_len - pos, "]");
    xSemaphoreGive(s_log_mutex);
}

void sys_log_clear() {
    if (s_log_mutex && xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_head = 0;
        s_count = 0;
        xSemaphoreGive(s_log_mutex);
    }
}
