#include "mm_page_table.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

static void pt_log(int level, const char* fmt, ...) {
    if (level > MM_DEBUG_LOG_LEVEL) return;
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MM:PT] %s\n", buf);
}

void PageTable::init() {
    memset(m_entries, 0, sizeof(m_entries));
    memset(m_hashmap, -1, sizeof(m_hashmap));

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

    m_used_count   = 0;
    m_dirty_count  = 0;
    m_pinned_count = 0;
    m_prot_count   = 0;
    m_prob_count   = 0;

    pt_log(2, "Page table initialized: %u slots (protected: %u, probation: %u)",
           MM_CACHE_PAGES, m_max_protected, m_max_probation);
}

mm_page_entry_t* PageTable::lookup(mm_page_id_t page_id) {
    if (page_id == MM_INVALID_PAGE_ID) return nullptr;

    // O(1) hash map probe with linear probing
    uint32_t bucket = hash_slot(page_id);
    for (uint32_t probe = 0; probe < HASH_BUCKETS; probe++) {
        uint32_t idx = (bucket + probe) & (HASH_BUCKETS - 1);
        int16_t slot = m_hashmap[idx];
        if (slot < 0) return nullptr; // empty bucket = miss
        if (m_entries[slot].valid && m_entries[slot].page_id == page_id) {
            return &m_entries[slot];
        }
    }
    return nullptr;
}

mm_page_entry_t* PageTable::insert(mm_page_id_t page_id, mm_tier_t tier) {
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
            m_entries[i].slru_segment = MM_SLRU_PROBATION;
            m_entries[i].usb_offset   = 0;

            // Insert into hash map
            uint32_t bucket = hash_slot(page_id);
            for (uint32_t probe = 0; probe < HASH_BUCKETS; probe++) {
                uint32_t idx = (bucket + probe) & (HASH_BUCKETS - 1);
                if (m_hashmap[idx] < 0) {
                    m_hashmap[idx] = (int16_t)i;
                    break;
                }
            }

            m_used_count++;
            m_prob_count++;

            pt_log(3, "Inserted page %u into slot %d (probation, tier %d)", page_id, i, tier);
            return &m_entries[i];
        }
    }

    pt_log(1, "ERROR: No free slots for page %u (cache full)", page_id);
    return nullptr;
}

mm_page_id_t PageTable::evict_candidate() {
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

    return MM_INVALID_PAGE_ID;
}

bool PageTable::remove(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    // Update cached counters before clearing
    if (entry->dirty)  m_dirty_count--;
    if (entry->pinned) m_pinned_count--;
    if (entry->slru_segment == MM_SLRU_PROTECTED) m_prot_count--;
    else m_prob_count--;
    m_used_count--;

    // Remove from hash map
    uint32_t bucket = hash_slot(page_id);
    for (uint32_t probe = 0; probe < HASH_BUCKETS; probe++) {
        uint32_t idx = (bucket + probe) & (HASH_BUCKETS - 1);
        if (m_hashmap[idx] < 0) break;
        if (m_entries[m_hashmap[idx]].page_id == page_id) {
            m_hashmap[idx] = -1;
            break;
        }
    }

    entry->valid   = false;
    entry->dirty   = false;
    entry->pinned  = false;
    entry->page_id = MM_INVALID_PAGE_ID;

    pt_log(3, "Removed page %u from cache", page_id);
    return true;
}

bool PageTable::mark_accessed(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    entry->access_count++;
    entry->last_access = xTaskGetTickCount();

    if (entry->slru_segment == MM_SLRU_PROBATION && entry->access_count >= 2) {
        if (m_prot_count >= m_max_protected) {
            int demote_idx = find_lru_in_segment(MM_SLRU_PROTECTED);
            if (demote_idx >= 0) {
                m_entries[demote_idx].slru_segment = MM_SLRU_PROBATION;
                m_prot_count--;
                m_prob_count++;
                pt_log(3, "Demoted page %u to probation", m_entries[demote_idx].page_id);
            }
        }
        entry->slru_segment = MM_SLRU_PROTECTED;
        m_prob_count--;
        m_prot_count++;
        pt_log(3, "Promoted page %u to protected segment", page_id);
    }
    return true;
}

bool PageTable::mark_dirty(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    if (!entry->dirty) {
        entry->dirty = true;
        m_dirty_count++;
    }
    mark_accessed(page_id);
    return true;
}

bool PageTable::mark_clean(mm_page_id_t page_id) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    if (entry->dirty) {
        entry->dirty = false;
        m_dirty_count--;
    }
    return true;
}

bool PageTable::set_pinned(mm_page_id_t page_id, bool pinned) {
    mm_page_entry_t* entry = lookup(page_id);
    if (!entry) return false;

    if (entry->pinned != pinned) {
        entry->pinned = pinned;
        if (pinned) m_pinned_count++;
        else m_pinned_count--;
    }
    pt_log(3, "%s page %u", pinned ? "Pinned" : "Unpinned", page_id);
    return true;
}

uint32_t PageTable::used_count() const {
    return m_used_count;
}

uint32_t PageTable::dirty_count() const {
    return m_dirty_count;
}

uint32_t PageTable::pinned_count() const {
    return m_pinned_count;
}

uint32_t PageTable::protected_count() const {
    return m_prot_count;
}

uint32_t PageTable::probation_count() const {
    return m_prob_count;
}

uint32_t PageTable::get_dirty_pages(mm_page_entry_t** out_entries, uint32_t max_count) {
    if (!out_entries || max_count == 0) return 0;

    uint32_t count = 0;
    for (int i = 0; i < MM_CACHE_PAGES && count < max_count; i++) {
        if (m_entries[i].valid && m_entries[i].dirty) {
            out_entries[count++] = &m_entries[i];
        }
    }
    return count;
}

mm_page_entry_t* PageTable::entry_at(uint32_t index) {
    if (index >= MM_CACHE_PAGES) return nullptr;
    return &m_entries[index];
}

int PageTable::find_lru_in_segment(mm_slru_segment_t segment) {
    int best_idx = -1;
    TickType_t oldest_ticks = 0xFFFFFFFF;

    for (int i = 0; i < MM_CACHE_PAGES; i++) {
        if (!m_entries[i].valid) continue;
        if (m_entries[i].pinned) continue;
        if (m_entries[i].slru_segment != segment) continue;

        if (m_entries[i].last_access < oldest_ticks) {
            oldest_ticks = m_entries[i].last_access;
            best_idx = i;
        }
    }
    return best_idx;
}

uint32_t PageTable::count_in_segment(mm_slru_segment_t segment) const {
    if (segment == MM_SLRU_PROTECTED) return m_prot_count;
    return m_prob_count;
}
