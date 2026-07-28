#include "usb_msc.h"
#include "config.h"
#include "mm_journal.h"
#include "mm_manager.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_err.h"
#include "ff.h"
#include "diskio.h"
#include <cstring>

static volatile bool s_usb_mounted = false;
static msc_host_device_handle_t s_msc_device = NULL;
static msc_host_vfs_handle_t s_vfs_handle = NULL;
static TaskHandle_t s_usb_host_task_handle = NULL;
static TaskHandle_t s_msc_app_task_handle = NULL;
static QueueHandle_t s_msc_event_queue = NULL;

static void msc_event_cb(const msc_host_event_t *event, void *arg) {
    if (s_msc_event_queue) {
        xQueueSend(s_msc_event_queue, event, 0);
    }
}

static void msc_app_task(void *arg) {
    msc_host_event_t event;
    while (true) {
        if (xQueueReceive(s_msc_event_queue, &event, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (event.event == msc_host_event_t::MSC_DEVICE_CONNECTED) {
                Serial.printf("[USB] Event: MSC Device Connected (Address: %d)\n", event.device.address);
                if (s_msc_device == NULL) {
                    Serial.println("[USB] Installing MSC device...");
                    esp_err_t err = msc_host_install_device(event.device.address, &s_msc_device);
                    if (err != ESP_OK) {
                        Serial.printf("[USB] Install device failed: %s (0x%X)\n", esp_err_to_name(err), err);
                        s_msc_device = NULL;
                        continue;
                    }

                    Serial.println("[USB] Device installed. Descriptors:");
                    msc_host_print_descriptors(s_msc_device);

                    esp_vfs_fat_mount_config_t mount_config = {
                        .format_if_mount_failed = false,
                        .max_files = MAX_OPEN_FILES,
                        .allocation_unit_size = 1024
                    };

                    Serial.printf("[USB] Mounting FatFs VFS at '%s'...\n", VFS_MOUNT_PATH);
                    err = msc_host_vfs_register(s_msc_device, VFS_MOUNT_PATH, &mount_config, &s_vfs_handle);
                    if (err != ESP_OK) {
                        Serial.printf("[USB] VFS register failed: %s (0x%X)\n", esp_err_to_name(err), err);
                        msc_host_uninstall_device(s_msc_device);
                        s_msc_device = NULL;
                        continue;
                    }

                    s_usb_mounted = true;
                    Serial.printf("[USB] SUCCESS: USB Storage Drive mounted at '%s'\n", VFS_MOUNT_PATH);

                    // Initialize WAL journal and run crash recovery scan on mount
                    if (mm_journal_init()) {
                        uint32_t recovered = mm_journal_recover();
                        if (recovered > 0) {
                            Serial.printf("[USB] Journal Recovery: %u page writes restored/cleaned.\n", recovered);
                        }
                    }

                    FATFS *fs = NULL;
                    DWORD fre_clust = 0;
                    if (f_getfree("0:", &fre_clust, &fs) == FR_OK && fs) {
                        uint64_t total = (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
                        uint64_t free_sp = (uint64_t)fre_clust * fs->csize * 512;
                        Serial.printf("[USB] Capacity: %llu MB Total | %llu MB Free\n",
                                      total / (1024 * 1024), free_sp / (1024 * 1024));
                    }

                    // Auto-create standard USB system directories
                    mkdir("/usb/www", 0777);
                    mkdir("/usb/apps", 0777);
                    mkdir("/usb/logs", 0777);
                    Serial.println("[USB] Verified system directories: /usb/www, /usb/apps, /usb/logs");
                }
            } else if (event.event == msc_host_event_t::MSC_DEVICE_DISCONNECTED) {
                Serial.println("[USB] Event: MSC Device Disconnected");
                // Force flush all dirty cache pages before unmounting
                if (mm_is_ready()) {
                    mm_flush_all();
                }
                s_usb_mounted = false;
                if (s_vfs_handle != NULL) {
                    msc_host_vfs_unregister(s_vfs_handle);
                    s_vfs_handle = NULL;
                }
                if (s_msc_device != NULL) {
                    msc_host_uninstall_device(s_msc_device);
                    s_msc_device = NULL;
                }
                Serial.println("[USB] Drive unmounted and cleaned up.");
            }
        }
    }
}

static void usb_host_lib_task(void *arg) {
    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(pdMS_TO_TICKS(500), &event_flags);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool usb_msc_init() {
    Serial.println("[USB] Installing USB Host library...");
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        Serial.printf("[USB] USB Host install failed: %s (0x%X)\n", esp_err_to_name(err), err);
        return false;
    }
    Serial.println("[USB] USB Host library installed.");

    s_msc_event_queue = xQueueCreate(5, sizeof(msc_host_event_t));
    if (!s_msc_event_queue) {
        Serial.println("[USB] Failed to create MSC event queue");
        return false;
    }

    BaseType_t ret1 = xTaskCreatePinnedToCore(
        usb_host_lib_task, "usb_host_lib", 4096, NULL, 2, &s_usb_host_task_handle, 0
    );
    BaseType_t ret2 = xTaskCreatePinnedToCore(
        msc_app_task, "msc_app_task", 4096, NULL, 3, &s_msc_app_task_handle, 0
    );
    if (ret1 != pdPASS || ret2 != pdPASS) {
        Serial.println("[USB] Failed to create USB tasks");
        return false;
    }

    Serial.println("[USB] Installing MSC Driver...");
    msc_host_driver_config_t msc_config = {
        .create_backround_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = msc_event_cb,
        .callback_arg = NULL
    };
    err = msc_host_install(&msc_config);
    if (err != ESP_OK) {
        Serial.printf("[USB] MSC Host install failed: %s (0x%X)\n", esp_err_to_name(err), err);
        return false;
    }

    Serial.println("[USB] USB MSC subsystem ready. Waiting for USB storage device...");
    return true;
}

bool is_usb_mounted() {
    return s_usb_mounted;
}

uint64_t get_usb_total_bytes() {
    if (!s_usb_mounted) return 0;
    FATFS *fs = NULL;
    DWORD fre_clust = 0;
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    if (res != FR_OK || !fs) {
        res = f_getfree("", &fre_clust, &fs);
    }
    if (res == FR_OK && fs) {
        return (uint64_t)(fs->n_fatent - 2) * fs->csize * 512;
    }
    return 0;
}

uint64_t get_usb_free_bytes() {
    if (!s_usb_mounted) return 0;
    FATFS *fs = NULL;
    DWORD fre_clust = 0;
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    if (res != FR_OK || !fs) {
        res = f_getfree("", &fre_clust, &fs);
    }
    if (res == FR_OK && fs) {
        return (uint64_t)fre_clust * fs->csize * 512;
    }
    return 0;
}

void sync_usb_fatfs() {
    if (!s_usb_mounted) return;
    FATFS *fs = NULL;
    DWORD fre_clust = 0;
    if (f_getfree("0:", &fre_clust, &fs) == FR_OK && fs) {
        if (fs->wflag) {
            DRESULT dr = disk_write(fs->pdrv, fs->win, fs->winsect, 1);
            if (dr == RES_OK) {
                fs->wflag = 0;
                Serial.printf("[USB] Physically committed FAT directory sector %lu to USB flash media.\n", (unsigned long)fs->winsect);
            } else {
                Serial.printf("[USB] disk_write sync failed: %d\n", dr);
            }
        }
    }
}

bool sanitize_usb_path(const char* path, char* safePath, size_t maxLen) {
    if (!path || !safePath || maxLen == 0) return false;
    
    if (strstr(path, "..")) {
        Serial.printf("[SECURITY] Blocked traversal attempt: %s\n", path);
        return false;
    }

    if (path[0] == '/') {
        snprintf(safePath, maxLen, "%s%s", VFS_MOUNT_PATH, path);
    } else {
        snprintf(safePath, maxLen, "%s/%s", VFS_MOUNT_PATH, path);
    }

    size_t len = strlen(safePath);
    if (len > strlen(VFS_MOUNT_PATH) && safePath[len - 1] == '/') {
        safePath[len - 1] = '\0';
    }

    return true;
}
