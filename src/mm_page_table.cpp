#include "mm_page_table.h"
#include <Arduino.h>
#include <cstring>
#include "esp_rom_crc.h"   // Hardware-accelerated CRC32

// =========================================================================
// Logging helper
// =========================================================================
static void pt_log(int level, const char* fmt, ...) {
    if (level > MM_DEBUG_LOG_LEVEL) return;
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MM:PT] %s\n", buf);
}

// =========================================================================
// PageTable Implementation
// =========================================================================

void PageTable::init() {
    memset(m_entries, 0, sizeof(m_entries));
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        m_entries[i].page_id      = MM_INVALID_PAGE_ID;
        m_entries[i].tier         = MM_TIER_NONE;
        m_entries[i].valid        = false;
        m_entries[i].dirty        = false;
        m_entries[i].pinned       = false;
        m_entries[i].crc32        = 0;
        m_entries[i].access_count = 0;
        m_entries[i].last_access  = 0;
        m_entries[i].slru_segment = MM_SLRU_PROBATION;
        m_entries[i].psram_offset = i * MM_PAGE_SIZE;
        m_entries[i].usb_offset   = 0;
    }

    m_max_protected = (uint32_t)(MM_CACHE_PAGES * MM_SLRU_PROTECTED_RATIO);
    m_max_probation = MM_CACHE_PAGES - m_max_protected;

    pt_log(2, "Page table initialized: %u slots (protected: %u, probation: %u)",
           MM_CACHE_PAGES, m_max_protected, m_max_probation);
}

mm_page_entry_t* PageTable::lookup(mm_page_id_t page_id) {
    if (page_id == MM_INVALID_PAGE_ID) return nullptr;

    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (m_entries[i].valid && m_entries[i].page_id == page_id) {
            return &m_entries[i];
        }
    }
    return nullptr;
}

mm_page_entry_t* PageTable::insert(mm_page_id_t page_id, mm_tier_t tier) {
    // Find a free slot
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (!m_entries[i].valid) {
            m_entries[i].page_id      = page_id;
            m_entries[i].tier         = tier;
            m_entries[i].valid        = true;
            m_entries[i].dirty        = false;
            m_entries[i].pinned       = false;
            m_entries[i].crc32        = 0;
            m_entries[i].access_count = 1;
            m_entries[i].last_access  = xTaskGetTickCount();
            m_entries[i].slru_segment = MM_SLRU_PROBATION;  // New pages start in probation
            m_entries[i].usb_offset   = 0;

            pt_log(3, "Inserted page %u into slot %d (probation, tier %d)",
                   page_id, i, tier);
            return &m_entries[i];
        }
    }

    pt_log(1, "ERROR: No free slots for page %u (cache full)", page_id);
    return nullptr;
}

mm_page_id_t PageTable::evict_candidate() {
    // SLRU eviction priority:
    // 1. Unpinned probationary LRU (cold, less important pages)
    // 2. Unpinned protected LRU (if all probation is pinned)

    int victim = find_lru_in_segment(MM_SLRU_PROBATION);
    if (victim >= 0) {
        pt_log(3, "Eviction candidate: page %u (probation, slot %d, access=%u)",
               m_entries[victim].page_id, victim, m_entries[victim].access_count);
        return m_entries[victim].page_id;
    }

    victim = find_lru_in_segment(MM_SLRU_PROTECTED);
    if (victim >= 0) {
        pt_log(3, "Eviction candidate: page %u (protected, slot %d, access=%u)",
               m_entries[victim].page_id, victim, m_entries[victim].access_count);
        return m_entries[victim].page_id;
    }

    pt_log(1, "ERROR: No eviction candidate found (all pages pinned?)");
    return MM_INVALID_PAGE_ID;
}

bool PageTable::remove(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    pt_log(3, "Removing page %u (was %s, dirty=%d)",
           page_id,
           entry->slru_segment == MM_SLRU_PROTECTED ? "protected" : "probation",
           entry->dirty);

    entry->page_id      = MM_INVALID_PAGE_ID;
    entry->tier         = MM_TIER_NONE;
    entry->valid        = false;
    entry->dirty        = false;
    entry->pinned       = false;
    entry->crc32        = 0;
    entry->access_count = 0;
    entry->last_access  = 0;
    entry->slru_segment = MM_SLRU_PROBATION;
    entry->usb_offset   = 0;

    return true;
}

bool PageTable::mark_accessed(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    entry->access_count++;
    entry->last_access = xTaskGetTickCount();

    // SLRU promotion: if page is in probation and accessed again, promote to protected
    if (entry->slru_segment == MM_SLRU_PROBATION && entry->access_count >= 2) {
        uint32_t prot_count = count_in_segment(MM_SLRU_PROTECTED);
        if (prot_count < m_max_protected) {
            entry->slru_segment = MM_SLRU_PROTECTED;
            pt_log(3, "Promoted page %u: probation → protected (accesses=%u)",
                   page_id, entry->access_count);
            return true;
        }
        // Protected segment full — demote the LRU protected page to make room
        int demote_idx = find_lru_in_segment(MM_SLRU_PROTECTED);
        if (demote_idx >= 0) {
            m_entries[demote_idx].slru_segment = MM_SLRU_PROBATION;
            entry->slru_segment = MM_SLRU_PROTECTED;
            pt_log(3, "Promoted page %u → protected (demoted page %u → probation)",
                   page_id, m_entries[demote_idx].page_id);
        }
    }

    return true;
}

bool PageTable::mark_dirty(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;
    entry->dirty = true;
    return true;
}

bool PageTable::mark_clean(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;
    entry->dirty = false;
    return true;
}

bool PageTable::set_pinned(mm_page_id_t page_id, bool pinned) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;
    entry->pinned = pinned;
    pt_log(3, "Page %u %s", page_id, pinned ? "PINNED" : "UNPINNED");
    return true;
}

// =========================================================================
// Query Methods
// =========================================================================

uint32_t PageTable::used_count() const {
    uint32_t count = 0;
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (m_entries[i].valid) count++;
    }
    return count;
}

uint32_t PageTable::dirty_count() const {
    uint32_t count = 0;
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (m_entries[i].valid && m_entries[i].dirty) count++;
    }
    return count;
}

uint32_t PageTable::pinned_count() const {
    uint32_t count = 0;
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (m_entries[i].valid && m_entries[i].pinned) count++;
    }
    return count;
}

uint32_t PageTable::protected_count() const {
    return count_in_segment(MM_SLRU_PROTECTED);
}

uint32_t PageTable::probation_count() const {
    return count_in_segment(MM_SLRU_PROBATION);
}

uint32_t PageTable::get_dirty_pages(mm_page_entry_t** out_entries, uint32_t max_count) {
    uint32_t found = 0;
    for (int i = 0; i < MM_CACHE_PAGES && found < max_count; i++) {
        if (m_entries[i].valid && m_entries[i].dirty) {
            out_entries[found++] = &m_entries[i];
        }
    }
    return found;
}

mm_page_entry_t* PageTable::entry_at(uint32_t index) {
    if (index >= MM_CACHE_PAGES) return nullptr;
    return &m_entries[index];
}

// =========================================================================
// Private Helpers
// =========================================================================

int PageTable::find_lru_in_segment(mm_slru_segment_t segment) {
    int lru_idx = -1;
    TickType_t oldest_tick = UINT32_MAX;

    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (m_entries[i].valid &&
            !m_entries[i].pinned &&
            m_entries[i].slru_segment == segment) {
            if (m_entries[i].last_access < oldest_tick) {
                oldest_tick = m_entries[i].last_access;
                lru_idx = i;
            }
        }
    }
    return lru_idx;
}

uint32_t PageTable::count_in_segment(mm_slru_segment_t segment) const {
    uint32_t count = 0;
    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (m_entries[i].valid && m_entries[i].slru_segment == segment) {
            count++;
        }
    }
    return count;
}
