#ifndef APP_ENGINE_H
#define APP_ENGINE_H

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

enum AppEngineType {
    APP_ENGINE_NONE = 0,
    APP_ENGINE_NATIVE_ELF = 1,
    APP_ENGINE_WASM_SANDBOX = 2,
    APP_ENGINE_LEGACY_BIN = 3
};

struct AppEngineStatus {
    bool is_running;
    AppEngineType type;
    char name[64];
    size_t ram_allocated_bytes;
    unsigned long run_time_ms;
    uint32_t cpu_core;
};

/**
 * @brief Initialize the Hybrid App Engine subsystem.
 */
void app_engine_init();

/**
 * @brief Launch a dynamic project module from USB storage.
 * Detects file extension (.so/.elf for Native, .wasm for WASM Sandbox, .bin for Legacy OTA).
 * @param filename File name relative to /usb/apps/ or absolute USB path.
 * @return true if launched successfully, false on error.
 */
bool app_engine_launch(const char* filename);

/**
 * @brief Stop the currently running project app and release allocated PSRAM.
 * @return true if stopped successfully.
 */
bool app_engine_stop();

/**
 * @brief Get current telemetry status of the Hybrid App Engine.
 * @return AppEngineStatus structure.
 */
AppEngineStatus app_engine_get_status();

/**
 * @brief Get active project name string.
 */
const char* app_engine_get_active_name();

#endif // APP_ENGINE_H
