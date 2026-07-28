#ifndef USB_MSC_H
#define USB_MSC_H

#include <Arduino.h>
#include <cstdint>
#include <cstddef>

/**
 * @brief Initialize USB Host library and MSC driver task.
 * @return true if installation succeeded, false otherwise.
 */
bool usb_msc_init();

/**
 * @brief Check if a USB Mass Storage drive is currently connected and mounted.
 * @return true if mounted, false otherwise.
 */
bool is_usb_mounted();

/**
 * @brief Get total capacity of mounted USB drive in bytes.
 * @return Total bytes, or 0 if unmounted.
 */
uint64_t get_usb_total_bytes();

/**
 * @brief Get free available space on mounted USB drive in bytes.
 * @return Free bytes, or 0 if unmounted.
 */
uint64_t get_usb_free_bytes();

/**
 * @brief Force flush dirty FatFs directory/FAT window sectors directly to physical USB media.
 * Ensures files created or uploaded on ESP32 appear instantly when USB drive is plugged into a laptop.
 */
void sync_usb_fatfs();

/**
 * @brief Utility function to clean and validate standard file paths.
 * Prevents directory traversal attacks by disallowing '..' paths.
 * @param path Requested client path.
 * @param safePath Output safe system path buffer.
 * @param maxLen Maximum size of safePath buffer.
 * @return true if path is safe and valid, false if invalid/malicious.
 */
bool sanitize_usb_path(const char* path, char* safePath, size_t maxLen);

#endif // USB_MSC_H
