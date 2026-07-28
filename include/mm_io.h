#ifndef MM_IO_H
#define MM_IO_H

#include "mm_types.h"
#include "mm_page_table.h"

// =========================================================================
// Async I/O Engine — Non-blocking USB page read/write via FreeRTOS
// =========================================================================
//
// Uses a dedicated worker task (`mm_io_task`) pinned to Core 0 (same as
// USB Host) with a FreeRTOS Queue for I/O requests.
//
// Supports:
//   - Single page read/write (4 KB)
//   - Burst mode: batches sequential pages into 16-64 KB transfers
//   - Background flush timer for periodic dirty page write-back
//   - Synchronous and fire-and-forget modes
//
// =========================================================================

/**
 * @brief Initialize the async I/O engine.
 * 
 * Creates the I/O request queue and spawns the worker task.
 * Must be called after USB is mounted.
 * 
 * @return true if initialization succeeded
 */
bool mm_io_init();

/**
 * @brief Shut down the async I/O engine.
 * 
 * Stops the worker task and deletes the queue.
 */
void mm_io_shutdown();

/**
 * @brief Read a page from USB into a PSRAM buffer (async or blocking).
 * 
 * If done_sem is provided, signals it on completion (caller waits).
 * If done_sem is NULL, the read is fire-and-forget.
 * 
 * @param page_id   Page identifier (used to construct filename)
 * @param buffer    Destination buffer in PSRAM (must be MM_PAGE_SIZE bytes)
 * @param done_sem  Semaphore to signal on completion (NULL for fire-and-forget)
 * @return true if request was successfully queued
 */
bool mm_io_read_page(mm_page_id_t page_id, uint8_t* buffer, SemaphoreHandle_t done_sem);

/**
 * @brief Write a page from PSRAM buffer to USB (async or blocking).
 * 
 * @param page_id   Page identifier
 * @param buffer    Source buffer in PSRAM (MM_PAGE_SIZE bytes)
 * @param done_sem  Semaphore to signal on completion (NULL for fire-and-forget)
 * @return true if request was successfully queued
 */
bool mm_io_write_page(mm_page_id_t page_id, const uint8_t* buffer, SemaphoreHandle_t done_sem);

/**
 * @brief Flush all dirty pages from the page table to USB.
 * 
 * Iterates dirty pages, writes each to USB, marks clean.
 * Blocks until all writes complete.
 * 
 * @param page_table  Pointer to the page table
 * @param psram_cache Pointer to the PSRAM cache buffer base
 * @return Number of pages flushed
 */
uint32_t mm_io_flush_dirty(PageTable* page_table, uint8_t* psram_cache);

/**
 * @brief Delete a page file from USB storage.
 * 
 * @param page_id  Page identifier
 * @return true if file deleted or didn't exist
 */
bool mm_io_delete_page(mm_page_id_t page_id);

/**
 * @brief Get the USB file path for a page.
 * 
 * @param page_id  Page identifier
 * @param out_path Output buffer for the path
 * @param max_len  Size of output buffer
 */
void mm_io_page_path(mm_page_id_t page_id, char* out_path, size_t max_len);

#endif // MM_IO_H
