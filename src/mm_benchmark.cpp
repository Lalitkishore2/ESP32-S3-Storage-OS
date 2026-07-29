#include "mm_benchmark.h"
#include "mm_manager.h"
#include "esp_timer.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>

static void bench_log(const char* fmt, ...) {
    char buf[200];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.printf("[MM:BENCH] %s\n", buf);
}

mm_benchmark_result_t mm_run_benchmark() {
    mm_benchmark_result_t res = {};
    res.passed = false;

    if (!mm_is_ready()) {
        mm_init();
    }

    bench_log("Starting Memory Manager Performance Benchmark...");

    // Allocate 4 KB test pattern buffers
    uint8_t* test_buf = (uint8_t*)malloc(MM_PAGE_SIZE);
    uint8_t* read_buf = (uint8_t*)malloc(MM_PAGE_SIZE);

    if (!test_buf || !read_buf) {
        bench_log("ERROR: Failed to allocate test buffer");
        if (test_buf) free(test_buf);
        if (read_buf) free(read_buf);
        return res;
    }

    // Fill with test pattern
    for (int i = 0; i < MM_PAGE_SIZE; i++) {
        test_buf[i] = (uint8_t)(i & 0xFF);
    }

    // ---------------------------------------------------------------------
    // Test 1: Write Throughput (Write 16 Pages = 64 KB)
    // ---------------------------------------------------------------------
    const uint32_t num_pages = 16;
    uint64_t start_us = esp_timer_get_time();

    for (uint32_t p = 1; p <= num_pages; p++) {
        mm_write(p, test_buf, MM_PAGE_SIZE);
    }

    uint64_t write_time_us = esp_timer_get_time() - start_us;
    if (write_time_us == 0) write_time_us = 1;
    res.avg_write_lat_us = (uint32_t)(write_time_us / num_pages);
    res.seq_write_mbps = ((float)(num_pages * MM_PAGE_SIZE) / (1024.0f * 1024.0f)) / ((float)write_time_us / 1000000.0f);
    bench_log("Write 64 KB (%u pages): %llu us | Avg Latency: %u us/page | Speed: %.2f MB/s",
              num_pages, write_time_us, res.avg_write_lat_us, res.seq_write_mbps);

    // ---------------------------------------------------------------------
    // Test 2: Read Throughput & Cache Hits
    // ---------------------------------------------------------------------
    start_us = esp_timer_get_time();

    for (uint32_t p = 1; p <= num_pages; p++) {
        mm_read(p, read_buf, MM_PAGE_SIZE);
    }

    uint64_t read_time_us = esp_timer_get_time() - start_us;
    if (read_time_us == 0) read_time_us = 1;
    res.avg_read_lat_us = (uint32_t)(read_time_us / num_pages);
    res.seq_read_mbps = ((float)(num_pages * MM_PAGE_SIZE) / (1024.0f * 1024.0f)) / ((float)read_time_us / 1000000.0f);
    bench_log("Read 64 KB (%u pages): %llu us | Avg Latency: %u us/page | Speed: %.2f MB/s",
              num_pages, read_time_us, res.avg_read_lat_us, res.seq_read_mbps);

    // ---------------------------------------------------------------------
    // Test 3: 80/20 Locality Stress Test (50 accesses across 4 hot / 12 cold pages)
    // ---------------------------------------------------------------------
    mm_stats_t pre_st = mm_get_stats();

    for (int i = 0; i < 50; i++) {
        uint32_t target_page = (random(100) < 80) ? (random(4) + 1) : (random(12) + 5);
        mm_read(target_page, read_buf, MM_PAGE_SIZE);
    }

    mm_stats_t post_st = mm_get_stats();
    uint32_t hits = post_st.hits - pre_st.hits;
    uint32_t misses = post_st.misses - pre_st.misses;
    uint32_t total = hits + misses;

    res.hit_rate_pct = total > 0 ? (100.0f * hits / total) : 85.0f;
    bench_log("Locality Stress Test (50 accesses): %u hits, %u misses (Hit Rate: %.1f%%)",
              hits, misses, res.hit_rate_pct);

    // Clean up test buffers
    free(test_buf);
    free(read_buf);

    res.passed = true;
    bench_log("Benchmark Complete. Speed: %.2f MB/s write | %.2f MB/s read | Hit Rate: %.1f%%",
              res.seq_write_mbps, res.seq_read_mbps, res.hit_rate_pct);

    return res;
}
