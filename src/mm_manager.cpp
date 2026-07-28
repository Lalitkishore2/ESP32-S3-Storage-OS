#include "mm_manager.h"
#include "mm_page_table.h"
#include "mm_io.h"
#include "mm_journal.h"
#include "usb_msc.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include <cstring>
#include <cstdio>
#include <sys/stat.h>

// =========================================================================
// Internal State
// =========================================================================

static bool         s_mm_initialized = false;

// SRAM pinned pool
static uint8_t*     s_sram_pool      = nullptr;
static size_t       s_sram_pool_used = 0;

// PSRAM page cache buffer (contiguous 2 MB block)
static uint8_t*     s_psram_cache    = nullptr;

// Page table (SLRU cache engine)
static PageTable    s_page_table;
static uint32_t     s_next_page_id = 1;

// Statistics counters
static mm_stats_t   s_stats;

// Background flush timer
static TimerHandle_t s_flush_timer = NULL;

// Mutex for thread-safe page table access
static SemaphoreHandle_t s_mm_mutex = NULL;

// =========================================================================
// Internal Helpers
// =========================================================================

static void mm_log(int level, const char* fmt, ...) {
    if (level > MM_DEBUG_LOG_LEVEL) return;
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MM] %s\n", buf);
}

/**
 * @brief Compute CRC32 for a data buffer using ESP32 hardware-accelerated ROM.
 */
static uint32_t mm_crc32(const uint8_t* data, size_t len) {
    return esp_rom_crc32_le(0, data, len);
}

/**
 * @brief Create the .mm_cache directory on USB if it doesn't exist.
 */
static bool mm_create_cache_dir() {
    struct stat st;
    if (stat(MM_CACHE_DIR, &st) == 0) {
        mm_log(3, "Cache directory already exists: %s", MM_CACHE_DIR);
        return true;
    }
    if (mkdir(MM_CACHE_DIR, 0777) == 0) {
        mm_log(2, "Created cache directory: %s", MM_CACHE_DIR);
        return true;
    }
    mm_log(1, "ERROR: Failed to create cache directory: %s", MM_CACHE_DIR);
    return false;
}

/**
 * @brief Background flush timer callback — flushes dirty pages periodically.
 */
static void mm_flush_timer_cb(TimerHandle_t timer) {
    if (!s_mm_initialized || !is_usb_mounted()) return;

    if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uint32_t dirty = s_page_table.dirty_count();
        if (dirty > 0) {
            mm_log(2, "Background flush: %u dirty pages", dirty);
            uint32_t flushed = mm_io_flush_dirty(&s_page_table, s_psram_cache);
            s_stats.flushes += flushed;
        }
        xSemaphoreGive(s_mm_mutex);
    }
}

/**
 * @brief Evict a page to make room for a new one.
 * 
 * If the victim is dirty, writes it back to USB first.
 * Returns the slot index of the freed entry.
 */
static mm_page_entry_t* mm_evict_one() {
    mm_page_id_t victim_id = s_page_table.evict_candidate();
    if (victim_id == MM_INVALID_PAGE_ID) {
        mm_log(1, "ERROR: Cannot evict — all pages pinned");
        return nullptr;
    }

    mm_page_entry_t* victim = s_page_table.lookup(victim_id);
    if (!victim) return nullptr;

    // If dirty, write back to USB before evicting
    if (victim->dirty) {
        uint8_t* page_data = s_psram_cache + victim->psram_offset;
        mm_log(2, "Evicting dirty page %u — writing back to USB", victim_id);

        // Synchronous write (we need the slot freed NOW)
        SemaphoreHandle_t done = xSemaphoreCreateBinary();
        if (done) {
            mm_journal_log_write(victim_id, victim->crc32);
            mm_io_write_page(victim_id, page_data, done);
            if (xSemaphoreTake(done, pdMS_TO_TICKS(5000)) == pdTRUE) {
                mm_journal_commit(victim_id);
            }
            vSemaphoreDelete(done);
            s_stats.usb_writes++;
        }
        s_stats.flushes++;
    }

    mm_log(3, "Evicted page %u (%s segment, access=%u)",
           victim_id,
           victim->slru_segment == MM_SLRU_PROTECTED ? "protected" : "probation",
           victim->access_count);

    // Remember the psram_offset before removing
    uint32_t freed_offset = victim->psram_offset;
    s_page_table.remove(victim_id);
    s_stats.evictions++;

    // Return the entry (now free, but psram_offset is preserved)
    // Find the entry that was just freed
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        mm_page_entry_t* e = s_page_table.entry_at(i);
        if (!e->valid && e->psram_offset == freed_offset) {
            return e;
        }
    }
    return nullptr;
}

// =========================================================================
// Public API Implementation
// =========================================================================

bool mm_init() {
    if (s_mm_initialized) {
        mm_log(1, "Already initialized, skipping");
        return true;
    }

    mm_log(2, "=== Multi-Level Memory Manager Initializing ===");

    // --- Step 1: Report PSRAM availability ---
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t sram_free   = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    mm_log(2, "PSRAM: %u KB total, %u KB free", psram_total / 1024, psram_free / 1024);
    mm_log(2, "SRAM:  %u KB free", sram_free / 1024);

    if (psram_total == 0) {
        mm_log(1, "ERROR: No PSRAM detected! Memory manager requires PSRAM.");
        mm_log(1, "Check platformio.ini: board_build.arduino.memory_type = qio_opi");
        return false;
    }

    // --- Step 2: Create thread-safety mutex ---
    s_mm_mutex = xSemaphoreCreateMutex();
    if (!s_mm_mutex) {
        mm_log(1, "ERROR: Failed to create mutex");
        return false;
    }

    // --- Step 3: Allocate SRAM pinned pool ---
    s_sram_pool = (uint8_t*)heap_caps_malloc(MM_SRAM_POOL_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_sram_pool) {
        mm_log(1, "ERROR: Failed to allocate %u KB SRAM pool", MM_SRAM_POOL_SIZE / 1024);
        return false;
    }
    s_sram_pool_used = 0;
    mm_log(2, "SRAM pool allocated: %u KB at %p", MM_SRAM_POOL_SIZE / 1024, s_sram_pool);

    // --- Step 4: Allocate PSRAM page cache ---
    size_t cache_size = (size_t)MM_CACHE_PAGES * MM_PAGE_SIZE;
    s_psram_cache = (uint8_t*)heap_caps_malloc(cache_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_psram_cache) {
        mm_log(1, "ERROR: Failed to allocate %u KB PSRAM cache", cache_size / 1024);
        heap_caps_free(s_sram_pool);
        s_sram_pool = nullptr;
        return false;
    }
    memset(s_psram_cache, 0, cache_size);
    mm_log(2, "PSRAM cache allocated: %u KB (%d pages × %d B) at %p",
           cache_size / 1024, MM_CACHE_PAGES, MM_PAGE_SIZE, s_psram_cache);

    // --- Step 5: Initialize page table (SLRU engine) ---
    s_page_table.init();

    // --- Step 6: Initialize async I/O engine ---
    if (is_usb_mounted()) {
        mm_create_cache_dir();
        if (!mm_io_init()) {
            mm_log(1, "WARNING: Async I/O engine failed to start");
        }
    } else {
        mm_log(2, "USB not mounted yet; I/O engine will start on first use");
    }

    // --- Step 7: Start background flush timer ---
    s_flush_timer = xTimerCreate(
        "mm_flush",
        pdMS_TO_TICKS(MM_FLUSH_INTERVAL_MS),
        pdTRUE,   // Auto-reload
        NULL,
        mm_flush_timer_cb
    );
    if (s_flush_timer) {
        xTimerStart(s_flush_timer, 0);
        mm_log(2, "Background flush timer started: every %dms", MM_FLUSH_INTERVAL_MS);
    }

    // --- Step 8: Initialize statistics ---
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.total_pages = MM_CACHE_PAGES;
    s_stats.psram_total = psram_total;
    s_stats.psram_free  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    s_stats.sram_pool_free = MM_SRAM_POOL_SIZE;

    s_mm_initialized = true;

    mm_log(2, "=== Memory Manager Ready ===");
    mm_log(2, "  Tiers: SRAM(%uKB) → PSRAM(%uKB cache) → USB(FAT32)",
           MM_SRAM_POOL_SIZE / 1024, cache_size / 1024);
    mm_log(2, "  Policy: SLRU eviction, write-back, %dms flush interval",
           MM_FLUSH_INTERVAL_MS);
    mm_log(2, "  Pages: %d slots × %d B, burst I/O: %d pages",
           MM_CACHE_PAGES, MM_PAGE_SIZE, MM_USB_BURST_PAGES);

    return true;
}

void mm_shutdown() {
    if (!s_mm_initialized) return;

    mm_log(2, "Shutting down Memory Manager...");

    // Stop flush timer
    if (s_flush_timer) {
        xTimerStop(s_flush_timer, pdMS_TO_TICKS(100));
        xTimerDelete(s_flush_timer, pdMS_TO_TICKS(100));
        s_flush_timer = NULL;
    }

    // Flush all dirty pages
    mm_flush_all();

    // Shut down I/O engine
    mm_io_shutdown();

    // Free PSRAM cache
    if (s_psram_cache) {
        heap_caps_free(s_psram_cache);
        s_psram_cache = nullptr;
    }

    // Free SRAM pool
    if (s_sram_pool) {
        heap_caps_free(s_sram_pool);
        s_sram_pool = nullptr;
    }

    // Delete mutex
    if (s_mm_mutex) {
        vSemaphoreDelete(s_mm_mutex);
        s_mm_mutex = NULL;
    }

    s_mm_initialized = false;
    mm_log(2, "Memory Manager shut down");
}

// =========================================================================
// Allocation
// =========================================================================

void* mm_alloc(size_t size, mm_alloc_hint_t hint) {
    if (!s_mm_initialized || size == 0) return nullptr;

    // Small allocations → SRAM pool (if fits and preferred)
    if (hint.preferred_tier == MM_TIER_SRAM || size <= 256) {
        if (s_sram_pool && (s_sram_pool_used + size) <= MM_SRAM_POOL_SIZE) {
            void* ptr = s_sram_pool + s_sram_pool_used;
            s_sram_pool_used += size;
            s_stats.sram_pool_free = MM_SRAM_POOL_SIZE - s_sram_pool_used;
            mm_log(3, "Allocated %u bytes from SRAM pool (used: %u/%u)",
                   size, s_sram_pool_used, MM_SRAM_POOL_SIZE);
            return ptr;
        }
    }

    // Larger allocations → PSRAM via heap_caps
    void* ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr) {
        mm_log(3, "Allocated %u bytes from PSRAM at %p", size, ptr);
        s_stats.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    } else {
        mm_log(1, "ERROR: Failed to allocate %u bytes from PSRAM", size);
    }
    return ptr;
}

void mm_free(void* ptr) {
    if (!ptr || !s_mm_initialized) return;

    // Check if pointer is in SRAM pool
    if (s_sram_pool && ptr >= s_sram_pool && 
        ptr < (s_sram_pool + MM_SRAM_POOL_SIZE)) {
        mm_log(3, "SRAM pool free (no-op, bump allocator): %p", ptr);
        return;
    }

    // PSRAM allocation — use heap_caps_free
    heap_caps_free(ptr);
    s_stats.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mm_log(3, "Freed PSRAM allocation at %p", ptr);
}

// =========================================================================
// Pinning
// =========================================================================

bool mm_pin(void* ptr) {
    if (!ptr || !s_mm_initialized) return false;

    // TODO: Find which page(s) this pointer belongs to and pin them
    // For now, just log the intent
    mm_log(3, "Pin requested for %p", ptr);
    return true;
}

bool mm_unpin(void* ptr) {
    if (!ptr || !s_mm_initialized) return false;
    mm_log(3, "Unpin requested for %p", ptr);
    return true;
}

// =========================================================================
// Page I/O — Full Implementation with SLRU + Async
// =========================================================================

size_t mm_read(mm_page_id_t id, void* buf, size_t len) {
    if (!s_mm_initialized || !buf || len == 0) return 0;
    if (len > MM_PAGE_SIZE) len = MM_PAGE_SIZE;

    if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        mm_log(1, "ERROR: Mutex timeout on read page %u", id);
        return 0;
    }

    // Cache lookup
    mm_page_entry_t* entry = s_page_table.lookup(id);

    if (entry) {
        // === CACHE HIT ===
        s_stats.hits++;
        s_page_table.mark_accessed(id);

        uint8_t* page_data = s_psram_cache + entry->psram_offset;

        // CRC32 verification
        uint32_t computed_crc = mm_crc32(page_data, MM_PAGE_SIZE);
        if (entry->crc32 != 0 && computed_crc != entry->crc32) {
            mm_log(1, "WARNING: CRC mismatch page %u (expected: 0x%08X, got: 0x%08X)",
                   id, entry->crc32, computed_crc);
            s_stats.crc_mismatches++;
        }

        memcpy(buf, page_data, len);
        xSemaphoreGive(s_mm_mutex);

        mm_log(3, "mm_page_hit: page=%u tier=PSRAM", id);
        return len;
    }

    // === CACHE MISS — load from USB ===
    s_stats.misses++;
    mm_log(3, "mm_page_miss: page=%u, loading from USB", id);

    // Find or create a cache slot
    mm_page_entry_t* slot = s_page_table.insert(id, MM_TIER_PSRAM);
    if (!slot) {
        // Cache full — evict someone
        mm_log(3, "Cache full, evicting for page %u", id);
        slot = mm_evict_one();
        if (!slot) {
            xSemaphoreGive(s_mm_mutex);
            mm_log(1, "ERROR: Cannot allocate slot for page %u", id);
            return 0;
        }
        // Now insert into the freed slot
        slot = s_page_table.insert(id, MM_TIER_PSRAM);
        if (!slot) {
            xSemaphoreGive(s_mm_mutex);
            return 0;
        }
    }

    uint8_t* page_data = s_psram_cache + slot->psram_offset;

    // Release mutex during I/O (long operation)
    xSemaphoreGive(s_mm_mutex);

    // Synchronous read from USB
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (done) {
        mm_io_read_page(id, page_data, done);
        bool got = xSemaphoreTake(done, pdMS_TO_TICKS(10000));
        vSemaphoreDelete(done);

        if (!got) {
            mm_log(1, "ERROR: Read timeout for page %u", id);
            s_page_table.remove(id);
            return 0;
        }
    } else {
        // Fallback: direct blocking read
        char path[64];
        mm_io_page_path(id, path, sizeof(path));
        FILE* f = fopen(path, "rb");
        if (f) {
            fread(page_data, 1, MM_PAGE_SIZE, f);
            fclose(f);
        }
    }

    s_stats.usb_reads++;

    // Re-acquire mutex to update metadata
    if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        slot->crc32 = mm_crc32(page_data, MM_PAGE_SIZE);
        s_stats.used_pages = s_page_table.used_count();
        xSemaphoreGive(s_mm_mutex);
    }

    memcpy(buf, page_data, len);
    return len;
}

size_t mm_write(mm_page_id_t id, const void* buf, size_t len) {
    if (!s_mm_initialized || !buf || len == 0) return 0;
    if (len > MM_PAGE_SIZE) len = MM_PAGE_SIZE;

    if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        mm_log(1, "ERROR: Mutex timeout on write page %u", id);
        return 0;
    }

    // Find or create cache entry
    mm_page_entry_t* entry = s_page_table.lookup(id);

    if (!entry) {
        // New page — insert into cache
        entry = s_page_table.insert(id, MM_TIER_PSRAM);
        if (!entry) {
            // Cache full — evict
            mm_page_entry_t* freed = mm_evict_one();
            if (!freed) {
                xSemaphoreGive(s_mm_mutex);
                mm_log(1, "ERROR: Cannot allocate slot for write page %u", id);
                return 0;
            }
            entry = s_page_table.insert(id, MM_TIER_PSRAM);
            if (!entry) {
                xSemaphoreGive(s_mm_mutex);
                return 0;
            }
        }
    }

    // Write data to PSRAM cache (write-back: NO immediate USB write)
    uint8_t* page_data = s_psram_cache + entry->psram_offset;
    memcpy(page_data, buf, len);
    if (len < MM_PAGE_SIZE) {
        memset(page_data + len, 0, MM_PAGE_SIZE - len);  // Zero-fill remainder
    }

    // Update metadata
    entry->dirty = true;
    entry->crc32 = mm_crc32(page_data, MM_PAGE_SIZE);
    s_page_table.mark_accessed(id);

    s_stats.used_pages  = s_page_table.used_count();
    s_stats.dirty_pages = s_page_table.dirty_count();

    xSemaphoreGive(s_mm_mutex);

    mm_log(3, "mm_write: page=%u len=%u (dirty, write-back deferred)", id, len);
    return len;
}

// =========================================================================
// Flush
// =========================================================================

bool mm_flush(mm_page_id_t id) {
    if (!s_mm_initialized) return false;

    if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;

    mm_page_entry_t* entry = s_page_table.lookup(id);
    if (!entry || !entry->dirty) {
        xSemaphoreGive(s_mm_mutex);
        return true;  // Already clean or not in cache
    }

    uint8_t* page_data = s_psram_cache + entry->psram_offset;
    xSemaphoreGive(s_mm_mutex);

    // Synchronous write
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    bool success = false;
    if (done) {
        mm_io_write_page(id, page_data, done);
        success = (xSemaphoreTake(done, pdMS_TO_TICKS(10000)) == pdTRUE);
        vSemaphoreDelete(done);
    }

    if (success) {
        if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            s_page_table.mark_clean(id);
            s_stats.flushes++;
            s_stats.usb_writes++;
            s_stats.dirty_pages = s_page_table.dirty_count();
            xSemaphoreGive(s_mm_mutex);
        }
        mm_log(2, "mm_page_flush: page=%u", id);
    }

    return success;
}

bool mm_flush_all() {
    if (!s_mm_initialized) return false;

    if (xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

    uint32_t dirty = s_page_table.dirty_count();
    if (dirty == 0) {
        xSemaphoreGive(s_mm_mutex);
        return true;
    }

    mm_log(2, "Flush all: %u dirty pages", dirty);
    uint32_t flushed = mm_io_flush_dirty(&s_page_table, s_psram_cache);
    s_stats.flushes += flushed;
    s_stats.usb_writes += flushed;
    s_stats.dirty_pages = s_page_table.dirty_count();

    xSemaphoreGive(s_mm_mutex);
    return true;
}

// =========================================================================
// Observability
// =========================================================================

mm_stats_t mm_get_stats() {
    if (s_mm_initialized) {
        // Update volatile fields atomically if possible
        if (s_mm_mutex && xSemaphoreTake(s_mm_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            s_stats.used_pages   = s_page_table.used_count();
            s_stats.dirty_pages  = s_page_table.dirty_count();
            s_stats.pinned_pages = s_page_table.pinned_count();
            xSemaphoreGive(s_mm_mutex);
        }
        s_stats.psram_free     = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        s_stats.sram_pool_free = MM_SRAM_POOL_SIZE - s_sram_pool_used;
    }
    return s_stats;
}

void mm_print_stats() {
    if (!s_mm_initialized) {
        Serial.println("[MM] Not initialized");
        return;
    }

    mm_stats_t st = mm_get_stats();
    uint32_t total_accesses = st.hits + st.misses;
    float hit_rate = total_accesses > 0 ? (100.0f * st.hits / total_accesses) : 0.0f;

    Serial.println("[MM] ═══════════ Memory Manager Stats ═══════════");
    Serial.printf("[MM] Cache: %u/%u pages used | %u dirty | %u pinned\n",
                  st.used_pages, st.total_pages, st.dirty_pages, st.pinned_pages);
    Serial.printf("[MM] Performance: %u hits, %u misses (%.1f%% hit rate)\n",
                  st.hits, st.misses, hit_rate);
    Serial.printf("[MM] Evictions: %u | Promotions: %u | Flushes: %u\n",
                  st.evictions, st.promotions, st.flushes);
    Serial.printf("[MM] USB I/O: %u reads, %u writes\n",
                  st.usb_reads, st.usb_writes);
    Serial.printf("[MM] Integrity: %u CRC mismatches | %u journal replays\n",
                  st.crc_mismatches, st.journal_replays);
    Serial.printf("[MM] SRAM pool: %u/%u KB free\n",
                  st.sram_pool_free / 1024, MM_SRAM_POOL_SIZE / 1024);
    Serial.printf("[MM] PSRAM: %u/%u KB free\n",
                  st.psram_free / 1024, st.psram_total / 1024);
    Serial.println("[MM] ═══════════════════════════════════════════");
}

bool mm_is_ready() {
    return s_mm_initialized;
}
