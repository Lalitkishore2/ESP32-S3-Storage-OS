#ifndef MM_PAGE_TABLE_H
#define MM_PAGE_TABLE_H

#include "mm_types.h"
#include "config.h"

// =========================================================================
// PageTable — Fixed-size page table with SLRU eviction policy
// =========================================================================
//
// Manages MM_CACHE_PAGES slots. Each slot tracks one 4 KB page.
//
// SLRU (Segmented LRU):
//   - Probationary segment: New pages land here on first access.
//   - Protected segment: Pages re-accessed are promoted here.
//   - Eviction: Picks from probationary tail first, then protected tail.
//
// Thread safety: NOT thread-safe. Caller must hold a mutex if accessed
// from multiple tasks (the mm_manager serializes all access).
//
// =========================================================================

class PageTable {
public:
    /**
     * @brief Initialize all slots to empty state.
     */
    void init();

    /**
     * @brief Look up a page by ID.
     * @param page_id  The page to find
     * @return Pointer to the entry, or nullptr if not in cache
     */
    mm_page_entry_t* lookup(mm_page_id_t page_id);

    /**
     * @brief Insert a new page into the cache (probationary segment).
     * 
     * Finds a free slot and assigns the page. If no free slots,
     * returns nullptr (caller should evict first).
     * 
     * @param page_id  Unique page identifier
     * @param tier     Storage tier (typically MM_TIER_PSRAM)
     * @return Pointer to the new entry, or nullptr if full
     */
    mm_page_entry_t* insert(mm_page_id_t page_id, mm_tier_t tier);

    /**
     * @brief Select a victim page for eviction using SLRU policy.
     * 
     * Priority: unpinned probationary LRU → unpinned protected LRU.
     * Never selects pinned pages.
     * 
     * @return Page ID of the victim, or MM_INVALID_PAGE_ID if none available
     */
    mm_page_id_t evict_candidate();

    /**
     * @brief Remove a page entry from the table (after eviction/free).
     * @param page_id  Page to remove
     * @return true if found and removed
     */
    bool remove(mm_page_id_t page_id);

    /**
     * @brief Mark a page as accessed (promotes probation → protected).
     * 
     * Updates last_access tick and access_count.
     * If page is in probationary segment and has been accessed before,
     * it gets promoted to the protected segment.
     * 
     * @param page_id  Page that was accessed
     * @return true if page found and updated
     */
    bool mark_accessed(mm_page_id_t page_id);

    /**
     * @brief Mark a page as dirty (modified, needs write-back).
     * @param page_id  Page that was modified
     * @return true if page found and marked
     */
    bool mark_dirty(mm_page_id_t page_id);

    /**
     * @brief Mark a page as clean (after successful write-back).
     * @param page_id  Page that was flushed
     * @return true if page found and marked
     */
    bool mark_clean(mm_page_id_t page_id);

    /**
     * @brief Set/clear the pinned flag on a page.
     * @param page_id  Target page
     * @param pinned   true to pin, false to unpin
     * @return true if page found
     */
    bool set_pinned(mm_page_id_t page_id, bool pinned);

    // =====================================================================
    // Query methods
    // =====================================================================

    /** @brief Count of currently occupied slots */
    uint32_t used_count() const;

    /** @brief Count of dirty pages needing write-back */
    uint32_t dirty_count() const;

    /** @brief Count of pinned pages */
    uint32_t pinned_count() const;

    /** @brief Count of pages in protected segment */
    uint32_t protected_count() const;

    /** @brief Count of pages in probationary segment */
    uint32_t probation_count() const;

    /**
     * @brief Get a list of dirty page entries for batch flush.
     * @param out_entries  Array to fill with pointers to dirty entries
     * @param max_count    Maximum entries to return
     * @return Number of dirty entries found
     */
    uint32_t get_dirty_pages(mm_page_entry_t** out_entries, uint32_t max_count);

    /**
     * @brief Direct access to the underlying page entry array.
     * @param index  Slot index (0 to MM_CACHE_PAGES-1)
     * @return Pointer to entry at index
     */
    mm_page_entry_t* entry_at(uint32_t index);

private:
    mm_page_entry_t m_entries[MM_CACHE_PAGES];

    // Maximum slots per SLRU segment
    uint32_t m_max_protected;
    uint32_t m_max_probation;

    /**
     * @brief Find the LRU (least recently used) entry in a given segment.
     * Only considers unpinned, valid entries.
     * @param segment  Which SLRU segment to search
     * @return Index of the LRU entry, or -1 if none found
     */
    int find_lru_in_segment(mm_slru_segment_t segment);

    /**
     * @brief Count entries in a given segment.
     */
    uint32_t count_in_segment(mm_slru_segment_t segment) const;
};

#endif // MM_PAGE_TABLE_H
