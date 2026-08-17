#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// =========================================================================
// 1. WI-FI & NETWORK CONFIGURATION
// =========================================================================
#define STA_SSID        "Chennai Home"
#define STA_PASS        "Harish@21"

// Static IP Configuration (Fixed IP: 192.168.0.8)
#define USE_STATIC_IP   true
#define STATIC_IP       192, 168, 0, 8
#define STATIC_GATEWAY  192, 168, 0, 1
#define STATIC_SUBNET   255, 255, 255, 0
#define STATIC_DNS      192, 168, 0, 1

#define AP_SSID         "ESP32-S3"
#define AP_PASS         "lalitkishore27"
#define AP_CHANNEL      1

#define MDNS_HOSTNAME   "storage"   // http://storage.local or http://storage/
#define NETBIOS_NAME    "STORAGE"   // \\STORAGE in Windows File Explorer
#define HTTP_PORT       80

// =========================================================================
// 2. USB MASS STORAGE CLASS (MSC) CONFIGURATION
// =========================================================================
#define VFS_MOUNT_PATH  "/usb"
#define MAX_OPEN_FILES  8

// =========================================================================
// 3. BUFFER CONFIGURATIONS
// =========================================================================
#define FILE_BUFFER_SIZE (16 * 1024) // 16KB transfer buffer

// =========================================================================
// 4. MULTI-LEVEL MEMORY MANAGER (MMManager) CONFIGURATION
// =========================================================================

// Page cache settings
#define MM_PAGE_SIZE          4096                 // 4 KB fixed pages (matches flash sector & FAT32 cluster)
#define MM_CACHE_PAGES        512                  // 512 pages × 4 KB = 2 MB PSRAM cache area
#define MM_SRAM_POOL_SIZE     (50 * 1024)          // 50 KB pinned SRAM pool for hot/DMA/ISR data

// USB I/O burst settings
#define MM_USB_BURST_PAGES    16                   // Batch up to 16 pages (64 KB) per USB transfer

// Cache directory on USB pendrive
#define MM_CACHE_DIR          "/usb/.mm_cache"     // Hidden directory for page cache files
#define MM_JOURNAL_PATH       "/usb/.mm_journal"   // Write-Ahead Log for crash recovery

// Flush & maintenance intervals
#define MM_FLUSH_INTERVAL_MS  5000                 // Background flush dirty pages every 5 seconds
#define MM_STATS_INTERVAL_MS  30000                // Print stats to serial every 30 seconds

// Observability log level (0=off, 1=errors, 2=info, 3=verbose/debug)
#define MM_DEBUG_LOG_LEVEL    2

// SLRU configuration
#define MM_SLRU_PROTECTED_RATIO  0.6f              // 60% of cache pages in protected segment
#define MM_SLRU_PROBATION_RATIO  0.4f              // 40% in probationary segment

#endif // CONFIG_H
