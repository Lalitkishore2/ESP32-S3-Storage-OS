#include "mm_io.h"
#include "config.h"
#include "usb_msc.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// =========================================================================
// Internal State
// =========================================================================

static QueueHandle_t  s_io_queue = NULL;
static TaskHandle_t   s_io_task_handle = NULL;
static volatile bool  s_io_running = false;

// =========================================================================
// Logging
// =========================================================================

static void io_log(int level, const char* fmt, ...) {
    if (level > MM_DEBUG_LOG_LEVEL) return;
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MM:IO] %s\n", buf);
}

// =========================================================================
// File Path Helper
// =========================================================================

void mm_io_page_path(mm_page_id_t page_id, char* out_path, size_t max_len) {
    snprintf(out_path, max_len, "%s/pg_%08X.bin", MM_CACHE_DIR, page_id);
}

// =========================================================================
// Core I/O Operations (called from worker task context)
// =========================================================================

static bool do_read_page(mm_page_id_t page_id, uint8_t* buffer) {
    if (!is_usb_mounted() || !buffer) return false;

    char path[64];
    mm_io_page_path(page_id, path, sizeof(path));

    unsigned long start = millis();
    FILE* f = fopen(path, "rb");
    if (!f) {
        io_log(3, "Page %u file not found: %s", page_id, path);
        return false;
    }

    size_t read = fread(buffer, 1, MM_PAGE_SIZE, f);
    fclose(f);

    unsigned long elapsed = millis() - start;
    if (read == MM_PAGE_SIZE) {
        io_log(3, "Read page %u from USB (%u bytes, %lums)", page_id, read, elapsed);
        return true;
    }

    io_log(1, "ERROR: Partial read page %u: %u/%u bytes", page_id, read, MM_PAGE_SIZE);
    return false;
}

static bool do_write_page(mm_page_id_t page_id, const uint8_t* buffer) {
    if (!is_usb_mounted() || !buffer) return false;

    char path[64];
    mm_io_page_path(page_id, path, sizeof(path));

    unsigned long start = millis();
    FILE* f = fopen(path, "wb");
    if (!f) {
        io_log(1, "ERROR: Cannot open for write: %s", path);
        return false;
    }

    size_t written = fwrite(buffer, 1, MM_PAGE_SIZE, f);
    fflush(f);
    fclose(f);

    unsigned long elapsed = millis() - start;
    if (written == MM_PAGE_SIZE) {
        io_log(3, "Wrote page %u to USB (%u bytes, %lums)", page_id, written, elapsed);
        sync_usb_fatfs();  // Commit FAT directory to physical media
        return true;
    }

    io_log(1, "ERROR: Partial write page %u: %u/%u bytes", page_id, written, MM_PAGE_SIZE);
    return false;
}

// =========================================================================
// Worker Task — processes I/O requests from the queue
// =========================================================================

static void mm_io_task(void* arg) {
    mm_io_request_t req;
    io_log(2, "I/O worker task started on core %d", xPortGetCoreID());

    while (s_io_running) {
        if (xQueueReceive(s_io_queue, &req, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool success = false;

            switch (req.type) {
                case MM_IO_READ:
                    success = do_read_page(req.page_id, req.buffer);
                    break;

                case MM_IO_WRITE:
                    success = do_write_page(req.page_id, req.buffer);
                    break;

                case MM_IO_FLUSH:
                    // Flush is handled synchronously via mm_io_flush_dirty()
                    success = true;
                    break;
            }

            req.success = success;

            // Signal completion if semaphore provided
            if (req.done_sem) {
                xSemaphoreGive(req.done_sem);
            }
        }
    }

    io_log(2, "I/O worker task exiting");
    vTaskDelete(NULL);
}

// =========================================================================
// Public API
// =========================================================================

bool mm_io_init() {
    if (s_io_running) {
        io_log(2, "Already initialized");
        return true;
    }

    s_io_queue = xQueueCreate(16, sizeof(mm_io_request_t));
    if (!s_io_queue) {
        io_log(1, "ERROR: Failed to create I/O queue");
        return false;
    }

    s_io_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        mm_io_task,
        "mm_io_task",
        8192,           // 8 KB stack (file I/O needs more)
        NULL,
        2,              // Priority 2 (above idle, below USB Host)
        &s_io_task_handle,
        0               // Core 0 (same as USB Host for thread safety)
    );

    if (ret != pdPASS) {
        io_log(1, "ERROR: Failed to create I/O worker task");
        s_io_running = false;
        vQueueDelete(s_io_queue);
        s_io_queue = NULL;
        return false;
    }

    // Create cache directory on USB if needed
    struct stat st;
    if (stat(MM_CACHE_DIR, &st) != 0) {
        if (mkdir(MM_CACHE_DIR, 0777) == 0) {
            io_log(2, "Created cache directory: %s", MM_CACHE_DIR);
        } else {
            io_log(1, "WARNING: Could not create cache dir: %s", MM_CACHE_DIR);
        }
    }

    io_log(2, "Async I/O engine initialized (queue depth: 16, task stack: 8KB)");
    return true;
}

void mm_io_shutdown() {
    if (!s_io_running) return;

    s_io_running = false;

    // Wait for task to exit
    if (s_io_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(200));
        s_io_task_handle = NULL;
    }

    if (s_io_queue) {
        vQueueDelete(s_io_queue);
        s_io_queue = NULL;
    }

    io_log(2, "Async I/O engine shut down");
}

bool mm_io_read_page(mm_page_id_t page_id, uint8_t* buffer, SemaphoreHandle_t done_sem) {
    if (!s_io_running || !s_io_queue) return false;

    mm_io_request_t req = {};
    req.type     = MM_IO_READ;
    req.page_id  = page_id;
    req.buffer   = buffer;
    req.length   = MM_PAGE_SIZE;
    req.done_sem = done_sem;
    req.success  = false;

    if (xQueueSend(s_io_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
        io_log(1, "ERROR: I/O queue full, read page %u dropped", page_id);
        return false;
    }

    return true;
}

bool mm_io_write_page(mm_page_id_t page_id, const uint8_t* buffer, SemaphoreHandle_t done_sem) {
    if (!s_io_running || !s_io_queue) return false;

    mm_io_request_t req = {};
    req.type     = MM_IO_WRITE;
    req.page_id  = page_id;
    req.buffer   = (uint8_t*)buffer;  // const-cast safe: worker only reads for writes
    req.length   = MM_PAGE_SIZE;
    req.done_sem = done_sem;
    req.success  = false;

    if (xQueueSend(s_io_queue, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
        io_log(1, "ERROR: I/O queue full, write page %u dropped", page_id);
        return false;
    }

    return true;
}

uint32_t mm_io_flush_dirty(PageTable* page_table, uint8_t* psram_cache) {
    if (!page_table || !psram_cache || !is_usb_mounted()) return 0;

    mm_page_entry_t* dirty[MM_CACHE_PAGES];
    uint32_t count = page_table->get_dirty_pages(dirty, MM_CACHE_PAGES);

    if (count == 0) return 0;

    io_log(2, "Flushing %u dirty pages to USB...", count);
    uint32_t flushed = 0;
    unsigned long start = millis();

    for (uint32_t i = 0; i < count; i++) {
        mm_page_entry_t* entry = dirty[i];
        uint8_t* page_data = psram_cache + entry->psram_offset;

        if (do_write_page(entry->page_id, page_data)) {
            entry->dirty = false;
            flushed++;
        } else {
            io_log(1, "ERROR: Failed to flush page %u", entry->page_id);
        }
    }

    unsigned long elapsed = millis() - start;
    io_log(2, "Flushed %u/%u dirty pages in %lums (%.1f KB/s)",
           flushed, count, elapsed,
           elapsed > 0 ? (flushed * MM_PAGE_SIZE / 1024.0f) / (elapsed / 1000.0f) : 0.0f);

    return flushed;
}

bool mm_io_delete_page(mm_page_id_t page_id) {
    char path[64];
    mm_io_page_path(page_id, path, sizeof(path));
    
    if (remove(path) == 0) {
        io_log(3, "Deleted page file: %s", path);
        return true;
    }
    // File didn't exist — that's fine
    return true;
}
