#ifndef MM_TYPES_H
#define MM_TYPES_H

#include <cstdint>
#include <cstddef>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// =========================================================================
// Page Identifier — unique 32-bit ID for each managed page
// =========================================================================
typedef uint32_t mm_page_id_t;

#define MM_INVALID_PAGE_ID  UINT32_MAX

// =========================================================================
// Memory Tiers — the hierarchy levels
// =========================================================================
typedef enum {
    MM_TIER_SRAM  = 0,   // L1: Internal SRAM (hot/pinned/DMA)
    MM_TIER_PSRAM = 1,   // L2: External PSRAM (working page cache)
    MM_TIER_USB   = 2,   // L4: USB Pendrive (cold/bulk storage)
    MM_TIER_NONE  = 3    // Not allocated
} mm_tier_t;

// =========================================================================
// SLRU Segment — Segmented LRU cache placement
// =========================================================================
typedef enum {
    MM_SLRU_PROBATION = 0,  // New pages land here first
    MM_SLRU_PROTECTED = 1   // Re-accessed pages get promoted here
} mm_slru_segment_t;

// =========================================================================
// Page Entry — metadata for one 4 KB page in the cache
// =========================================================================
typedef struct {
    mm_page_id_t       page_id;         // Unique page identifier
    mm_tier_t          tier;            // Current storage tier
    uint32_t           psram_offset;    // Offset into PSRAM cache buffer (if in PSRAM)
    uint32_t           usb_offset;      // File offset on USB (if backed by USB)
    bool               valid;           // Entry is in use
    bool               dirty;           // Modified since last write-back
    bool               pinned;          // Locked in current tier (won't be evicted)
    uint32_t           crc32;           // CRC32 checksum for integrity
    uint32_t           access_count;    // Total access count (for LFU reference)
    TickType_t         last_access;     // Tick of last access (for LRU ordering)
    mm_slru_segment_t  slru_segment;    // SLRU segment placement
} mm_page_entry_t;

// =========================================================================
// Allocation Hint — caller preferences for mm_alloc()
// =========================================================================
typedef struct {
    mm_tier_t  preferred_tier;   // Preferred allocation tier (default: MM_TIER_PSRAM)
    bool       pinned;           // Pin allocation in specified tier
    size_t     alignment;        // Alignment requirement (0 = default)
} mm_alloc_hint_t;

// Default allocation hint
#define MM_ALLOC_DEFAULT  { MM_TIER_PSRAM, false, 0 }

// =========================================================================
// I/O Request — queued work item for the async I/O worker task
// =========================================================================
typedef enum {
    MM_IO_READ  = 0,   // Read page from USB → PSRAM
    MM_IO_WRITE = 1,   // Write page from PSRAM → USB
    MM_IO_FLUSH = 2    // Flush all dirty pages
} mm_io_type_t;

typedef struct {
    mm_io_type_t       type;          // Operation type
    mm_page_id_t       page_id;       // Target page
    uint8_t*           buffer;        // Source/destination buffer (in PSRAM)
    size_t             length;        // Transfer length
    SemaphoreHandle_t  done_sem;      // Signaled on completion (can be NULL for fire-and-forget)
    bool               success;       // Set by worker: true if I/O succeeded
} mm_io_request_t;

// =========================================================================
// Statistics — runtime observability counters
// =========================================================================
typedef struct {
    // Cache performance
    uint32_t  hits;              // Page found in PSRAM cache
    uint32_t  misses;            // Page not in cache, loaded from USB
    uint32_t  evictions;         // Pages evicted from cache
    uint32_t  promotions;        // Pages promoted from probation → protected

    // I/O counters
    uint32_t  usb_reads;         // Total page reads from USB
    uint32_t  usb_writes;        // Total page writes to USB
    uint32_t  flushes;           // Explicit flush operations
    uint32_t  journal_replays;   // Journal entries replayed on recovery

    // Integrity
    uint32_t  crc_mismatches;    // CRC32 verification failures

    // Current state
    uint32_t  total_pages;       // Total cache slots (MM_CACHE_PAGES)
    uint32_t  used_pages;        // Currently occupied slots
    uint32_t  dirty_pages;       // Pages with pending write-back
    uint32_t  pinned_pages;      // Pages locked in tier

    // Memory usage
    uint32_t  sram_pool_free;    // Free bytes in SRAM pool
    uint32_t  psram_total;       // Total PSRAM available
    uint32_t  psram_free;        // Free PSRAM available
} mm_stats_t;

// =========================================================================
// Callback type for async I/O completion
// =========================================================================
typedef void (*mm_io_callback_t)(mm_page_id_t page_id, bool success, void* user_data);

#endif // MM_TYPES_H
