#include "mm_journal.h"
#include "usb_msc.h"
#include "mm_io.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

static bool s_journal_ready = false;

static void journal_log(int level, const char* fmt, ...) {
    if (level > MM_DEBUG_LOG_LEVEL) return;
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MM:JNL] %s\n", buf);
}

bool mm_journal_init() {
    if (!is_usb_mounted()) {
        s_journal_ready = false;
        return false;
    }

    FILE* f = fopen(MM_JOURNAL_PATH, "a+b");
    if (!f) {
        journal_log(1, "ERROR: Failed to open journal file %s", MM_JOURNAL_PATH);
        s_journal_ready = false;
        return false;
    }

    fclose(f);
    s_journal_ready = true;
    journal_log(2, "Journal online at %s", MM_JOURNAL_PATH);
    return true;
}

bool mm_journal_log_write(mm_page_id_t page_id, uint32_t crc32) {
    if (!s_journal_ready || !is_usb_mounted()) return false;

    mm_journal_entry_t entry;
    entry.magic     = MM_JOURNAL_MAGIC;
    entry.page_id   = page_id;
    entry.crc32     = crc32;
    entry.state     = MM_JOURNAL_STATE_PENDING;
    entry.timestamp = (uint32_t)millis();

    FILE* f = fopen(MM_JOURNAL_PATH, "ab");
    if (!f) return false;

    size_t w = fwrite(&entry, sizeof(entry), 1, f);
    fflush(f);
    fclose(f);
    sync_usb_fatfs();

    if (w == 1) {
        journal_log(3, "Logged PENDING write for page %u (CRC: 0x%08X)", page_id, crc32);
        return true;
    }
    return false;
}

bool mm_journal_commit(mm_page_id_t page_id) {
    if (!s_journal_ready || !is_usb_mounted()) return false;

    mm_journal_entry_t entry;
    entry.magic     = MM_JOURNAL_MAGIC;
    entry.page_id   = page_id;
    entry.crc32     = 0;
    entry.state     = MM_JOURNAL_STATE_COMMITTED;
    entry.timestamp = (uint32_t)millis();

    FILE* f = fopen(MM_JOURNAL_PATH, "ab");
    if (!f) return false;

    size_t w = fwrite(&entry, sizeof(entry), 1, f);
    fflush(f);
    fclose(f);
    sync_usb_fatfs();

    if (w == 1) {
        journal_log(3, "Logged COMMITTED write for page %u", page_id);
        return true;
    }
    return false;
}

uint32_t mm_journal_recover() {
    if (!is_usb_mounted()) return 0;

    FILE* f = fopen(MM_JOURNAL_PATH, "rb");
    if (!f) return 0;

    mm_journal_entry_t entry;
    uint32_t pending_page = MM_INVALID_PAGE_ID;
    uint32_t recovered = 0;

    journal_log(2, "Scanning WAL journal for uncommitted transactions...");

    while (fread(&entry, sizeof(entry), 1, f) == 1) {
        if (entry.magic != MM_JOURNAL_MAGIC) continue;

        if (entry.state == MM_JOURNAL_STATE_PENDING) {
            pending_page = entry.page_id;
        } else if (entry.state == MM_JOURNAL_STATE_COMMITTED) {
            if (entry.page_id == pending_page) {
                pending_page = MM_INVALID_PAGE_ID; // Successfully committed
            }
        }
    }
    fclose(f);

    if (pending_page != MM_INVALID_PAGE_ID) {
        journal_log(1, "WARNING: Found uncommitted page %u from power loss mid-write", pending_page);
        // Delete incomplete page file to prevent corrupted reads
        mm_io_delete_page(pending_page);
        recovered++;
        journal_log(2, "Recovered from crash: invalidated corrupted page %u file", pending_page);
    } else {
        journal_log(2, "Journal clean: no power-loss corruption detected");
    }

    // Truncate journal after recovery scan to keep log lean
    mm_journal_clear();
    return recovered;
}

void mm_journal_clear() {
    if (!is_usb_mounted()) return;
    FILE* f = fopen(MM_JOURNAL_PATH, "wb");
    if (f) fclose(f);
    s_journal_ready = true;
    journal_log(3, "Journal cleared");
}
