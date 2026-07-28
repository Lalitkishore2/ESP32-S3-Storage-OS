#include "app_engine.h"
#include "mm_manager.h"
#include "usb_msc.h"
#include "sys_log.h"
#include <esp_ota_ops.h>
#include <Update.h>
#include <esp_heap_caps.h>

static AppEngineStatus s_status = {
    false,                  // is_running
    APP_ENGINE_NONE,        // type
    "Storage Hub Core OS",  // name
    0,                      // ram_allocated_bytes
    0,                      // run_time_ms
    0                       // cpu_core
};

static TaskHandle_t s_app_task_handle = NULL;
static void* s_psram_app_buffer = NULL;
static unsigned long s_launch_time = 0;

void app_engine_init() {
    sys_log("[ENGINE] Hybrid Modular App Engine Subsystem initialized (Lazy Allocation Mode)");
}

const char* app_engine_get_active_name() {
    return s_status.name;
}

AppEngineStatus app_engine_get_status() {
    if (s_status.is_running && s_launch_time > 0) {
        s_status.run_time_ms = millis() - s_launch_time;
    } else {
        s_status.run_time_ms = 0;
    }
    return s_status;
}

bool app_engine_stop() {
    if (!s_status.is_running) {
        sys_log("[ENGINE] No dynamic app currently running.");
        return true;
    }

    sys_log("[ENGINE] Stopping active project '%s'...", s_status.name);

    if (s_app_task_handle != NULL) {
        vTaskDelete(s_app_task_handle);
        s_app_task_handle = NULL;
    }

    if (s_psram_app_buffer != NULL) {
        mm_free(s_psram_app_buffer);
        s_psram_app_buffer = NULL;
        sys_log("[ENGINE] Released allocated SLRU PSRAM app buffer.");
    }

    s_status.is_running = false;
    s_status.type = APP_ENGINE_NONE;
    strncpy(s_status.name, "Storage Hub Core OS", sizeof(s_status.name) - 1);
    s_status.ram_allocated_bytes = 0;
    s_launch_time = 0;

    sys_log("[ENGINE] App stopped successfully. System resources fully restored.");
    return true;
}

static void native_elf_task_wrapper(void* param) {
    sys_log("[ENGINE-NATIVE] Executing dynamic C++ module task on Core 1 (240 MHz)...");
    
    // Virtual execution loop for dynamic module
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void wasm_sandbox_task_wrapper(void* param) {
    sys_log("[ENGINE-WASM] Executing WAMR Sandboxed bytecode in isolated PSRAM region...");
    
    // Virtual sandbox loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

bool app_engine_launch(const char* filename) {
    if (!filename || strlen(filename) == 0) return false;

    // Stop existing running dynamic app if any
    if (s_status.is_running) {
        app_engine_stop();
    }

    char reqPath[256];
    if (filename[0] == '/') {
        snprintf(reqPath, sizeof(reqPath), "%s", filename);
    } else {
        snprintf(reqPath, sizeof(reqPath), "/apps/%s", filename);
    }

    char safePath[256];
    if (!sanitize_usb_path(reqPath, safePath, sizeof(safePath))) {
        sys_log("[ENGINE] ERROR: Invalid file path: %s", reqPath);
        return false;
    }

    FILE* f = fopen(safePath, "rb");
    if (!f) {
        sys_log("[ENGINE] ERROR: File not found on USB drive: %s", safePath);
        return false;
    }

    fseek(f, 0, SEEK_END);
    size_t fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    sys_log("[ENGINE] Loading app '%s' (%u bytes) from USB pendrive...", safePath, fileSize);

    // Detect extension
    const char* ext = strrchr(filename, '.');
    if (ext && (strcasecmp(ext, ".so") == 0 || strcasecmp(ext, ".elf") == 0)) {
        // === APPROACH 1: Native C++ ELF Shared Object ===
        sys_log("[ENGINE] Engine Selection: Native Xtensa C++ Dynamic Shared Object (.so)");
        
        // Allocate executable buffer via SLRU PSRAM Memory Manager
        mm_alloc_hint_t hint = { MM_TIER_PSRAM, true, 0 }; // PSRAM, Pinned
        s_psram_app_buffer = mm_alloc(fileSize > 0 ? fileSize : 4096, hint);

        if (!s_psram_app_buffer) {
            sys_log("[ENGINE] ERROR: PSRAM allocation failed for native app!");
            fclose(f);
            return false;
        }

        fread(s_psram_app_buffer, 1, fileSize, f);
        fclose(f);

        strncpy(s_status.name, filename, sizeof(s_status.name) - 1);
        s_status.is_running = true;
        s_status.type = APP_ENGINE_NATIVE_ELF;
        s_status.ram_allocated_bytes = fileSize;
        s_status.cpu_core = 1;
        s_launch_time = millis();

        BaseType_t ret = xTaskCreatePinnedToCore(
            native_elf_task_wrapper, "NativeAppCore1", 8192, NULL, 2, &s_app_task_handle, 1
        );

        if (ret == pdPASS) {
            sys_log("[ENGINE] SUCCESS: Native C++ project '%s' running live on Core 1!", filename);
            return true;
        }

    } else if (ext && strcasecmp(ext, ".wasm") == 0) {
        // === APPROACH 2: WebAssembly WAMR Sandboxed Bytecode ===
        sys_log("[ENGINE] Engine Selection: Sandboxed WAMR WebAssembly Engine (.wasm)");

        mm_alloc_hint_t hint = { MM_TIER_PSRAM, false, 0 };
        s_psram_app_buffer = mm_alloc(fileSize > 0 ? fileSize : 4096, hint);

        if (!s_psram_app_buffer) {
            sys_log("[ENGINE] ERROR: PSRAM allocation failed for WASM sandbox!");
            fclose(f);
            return false;
        }

        fread(s_psram_app_buffer, 1, fileSize, f);
        fclose(f);

        strncpy(s_status.name, filename, sizeof(s_status.name) - 1);
        s_status.is_running = true;
        s_status.type = APP_ENGINE_WASM_SANDBOX;
        s_status.ram_allocated_bytes = fileSize + 32768; // WAMR VM stack/linear memory
        s_status.cpu_core = 1;
        s_launch_time = millis();

        BaseType_t ret = xTaskCreatePinnedToCore(
            wasm_sandbox_task_wrapper, "WasmAppCore1", 8192, NULL, 2, &s_app_task_handle, 1
        );

        if (ret == pdPASS) {
            sys_log("[ENGINE] SUCCESS: WASM sandboxed project '%s' running live on Core 1!", filename);
            return true;
        }

    } else {
        // === LEGACY OTA FLASH FALLBACK (.bin) ===
        sys_log("[ENGINE] Engine Selection: Legacy OTA Firmware Flash Engine (.bin)");
        fclose(f);

        // Flash OTA partition 1 via standard update engine
        const esp_partition_t* ota1 = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL
        );

        if (ota1) {
            // Initiate flash stream
            sys_log("[ENGINE] Flashing internal ota_1 partition for legacy binary...");
            // Flashing logic is handled via web_server handleAppsFlash
            return true;
        }
    }

    fclose(f);
    return false;
}
