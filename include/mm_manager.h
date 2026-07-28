#ifndef MM_MANAGER_H
#define MM_MANAGER_H

#include <Arduino.h>
#include "mm_types.h"
#include "config.h"

// =========================================================================
// Multi-Level Memory Manager — Public API
// =========================================================================
//
// Architecture:  SRAM (hot) → PSRAM (working) → USB Pendrive (cold)
// Page Size:     4 KB fixed pages internally
// Eviction:      SLRU (Segmented LRU)
// Write Policy:  Write-back with periodic flush
// I/O Model:     Async FreeRTOS worker task (non-blocking)
// Integrity:     CRC32 per page + Write-Ahead Log journal
//
// Usage:
//   mm_init();                          // Call once after USB mounted
//   void* buf = mm_alloc(64 * 1024);    // 64 KB → 16 pages managed transparently
//   mm_pin(buf);                        // Lock in fast memory
//   mm_flush_all();                     // Force write-back before unmount
//   mm_free(buf);                       // Release pages
//
// =========================================================================

/**
 * @brief Initialize the Multi-Level Memory Manager.
 * 
 * Allocates SRAM pool, PSRAM cache area, initializes page table,
 * creates async I/O worker task, and recovers journal if needed.
 * 
 * Must be called AFTER usb_msc_init() and Wi-Fi setup.
 * 
 * @return true if all tiers initialized successfully
 */
bool mm_init();

/**
 * @brief Shut down the memory manager gracefully.
 * 
 * Flushes all dirty pages, closes journal, frees pools.
 * Call before USB unmount or system shutdown.
 */
void mm_shutdown();

// =========================================================================
// Allocation API
// =========================================================================

/**
 * @brief Allocate memory from the tiered hierarchy.
 * 
 * Small allocations (< 4 KB) go to SRAM pool by default.
 * Larger allocations are paged into PSRAM cache.
 * The hint parameter allows overriding tier preference.
 * 
 * @param size  Number of bytes to allocate
 * @param hint  Allocation preferences (default: PSRAM, not pinned)
 * @return Pointer to allocated memory, or NULL on failure
 */
void* mm_alloc(size_t size, mm_alloc_hint_t hint = {MM_TIER_PSRAM, false, 0});

/**
 * @brief Free memory previously allocated by mm_alloc().
 * 
 * If the allocation spans cached pages, they are marked free
 * and their dirty data is flushed before release.
 * 
 * @param ptr  Pointer returned by mm_alloc()
 */
void mm_free(void* ptr);

// =========================================================================
// Pinning API — Lock data in fast memory
// =========================================================================

/**
 * @brief Pin a memory region to prevent eviction.
 * 
 * Pinned pages stay in their current tier (SRAM or PSRAM)
 * and will NOT be evicted by the SLRU engine.
 * Use for DMA buffers, ISR data, or time-critical structures.
 * 
 * @param ptr  Pointer to pin
 * @return true if pinned successfully
 */
bool mm_pin(void* ptr);

/**
 * @brief Unpin a memory region, allowing eviction.
 * 
 * @param ptr  Pointer to unpin
 * @return true if unpinned successfully
 */
bool mm_unpin(void* ptr);

// =========================================================================
// Page I/O API — Read/Write through the cache hierarchy
// =========================================================================

/**
 * @brief Read data from a managed page.
 * 
 * Lookup cascade: SRAM → PSRAM → USB (async load on miss).
 * On cache miss, the page is loaded from USB into PSRAM.
 * On cache hit, data is served directly from PSRAM.
 * 
 * @param id   Page identifier
 * @param buf  Destination buffer
 * @param len  Bytes to read (up to MM_PAGE_SIZE)
 * @return Bytes actually read, or 0 on error
 */
size_t mm_read(mm_page_id_t id, void* buf, size_t len);

/**
 * @brief Write data to a managed page.
 * 
 * Uses write-back policy: data is written to PSRAM cache
 * and marked dirty. Physical USB write is deferred until
 * eviction, explicit flush, or periodic background flush.
 * 
 * @param id   Page identifier
 * @param buf  Source data
 * @param len  Bytes to write (up to MM_PAGE_SIZE)
 * @return Bytes actually written, or 0 on error
 */
size_t mm_write(mm_page_id_t id, const void* buf, size_t len);

// =========================================================================
// Flush API — Force write-back
// =========================================================================

/**
 * @brief Flush a specific page to USB storage.
 * 
 * Forces immediate write-back of a dirty page with journal logging.
 * Blocks until the write completes.
 * 
 * @param id  Page identifier to flush
 * @return true if flushed (or page was already clean)
 */
bool mm_flush(mm_page_id_t id);

/**
 * @brief Flush ALL dirty pages to USB storage.
 * 
 * Call before USB unmount or system shutdown.
 * Blocks until all writes complete.
 * 
 * @return true if all pages flushed successfully
 */
bool mm_flush_all();

// =========================================================================
// Observability API
// =========================================================================

/**
 * @brief Get current memory manager statistics.
 * 
 * Returns a snapshot of all counters: hits, misses, evictions,
 * dirty pages, memory usage, CRC mismatches, etc.
 * 
 * @return mm_stats_t structure with current values
 */
mm_stats_t mm_get_stats();

/**
 * @brief Print formatted stats to Serial output.
 * 
 * Structured log format with [MM] prefix for all fields.
 */
void mm_print_stats();

/**
 * @brief Check if the memory manager is initialized and ready.
 * @return true if mm_init() completed successfully
 */
bool mm_is_ready();

#endif // MM_MANAGER_H
