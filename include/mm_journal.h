#ifndef MM_JOURNAL_H
#define MM_JOURNAL_H

#include "mm_types.h"
#include "config.h"

// =========================================================================
// Write-Ahead Log (WAL) Journal — Power-Loss Crash Recovery Engine
// =========================================================================
//
// Maintains a small circular transaction log on USB storage (`/usb/.mm_journal`).
//
// Protocol:
//   1. Before flushing a dirty page to disk: write PENDING record to journal + sync.
//   2. Write page data file (`/usb/.mm_cache/pg_XXXXXXXX.bin`).
//   3. Write COMMITTED record to journal + sync.
//
// Crash Recovery (Boot):
//   If system resets mid-write, boot scan detects PENDING records with missing
//   COMMITTED marker. Replays write or invalidates corrupted page to preserve data integrity.
//
// =========================================================================

#define MM_JOURNAL_MAGIC 0x4D4D4A4C  // "MMJL"

typedef enum {
    MM_JOURNAL_STATE_PENDING   = 1,
    MM_JOURNAL_STATE_COMMITTED = 2
} mm_journal_state_t;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;         // MM_JOURNAL_MAGIC
    uint32_t page_id;       // Target page ID
    uint32_t crc32;         // Data CRC32 checksum
    uint8_t  state;         // PENDING (1) or COMMITTED (2)
    uint32_t timestamp;     // System tick count
} mm_journal_entry_t;
#pragma pack(pop)

/**
 * @brief Initialize journal system on USB mount.
 * @return true if journal opened/created successfully
 */
bool mm_journal_init();

/**
 * @brief Log intention to write a page (PENDING state).
 * 
 * Must be called before data page write.
 * 
 * @param page_id  Target page
 * @param crc32    Data CRC32 checksum
 * @return true if written to journal
 */
bool mm_journal_log_write(mm_page_id_t page_id, uint32_t crc32);

/**
 * @brief Mark page write as COMMITTED in journal.
 * 
 * Called immediately after data page write succeeds.
 * 
 * @param page_id Target page
 * @return true if committed
 */
bool mm_journal_commit(mm_page_id_t page_id);

/**
 * @brief Scan journal on boot and recover uncommitted transactions.
 * @return Number of transactions recovered
 */
uint32_t mm_journal_recover();

/**
 * @brief Clear/truncate the journal file.
 */
void mm_journal_clear();

#endif // MM_JOURNAL_H
