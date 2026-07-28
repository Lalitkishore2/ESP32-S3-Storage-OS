#include "mm_benchmark.h"
#include "mm_manager.h"
#include "esp_timer.h"
#include <Arduino.h>
#include <cstring>

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
        bench_log("ERROR: Memory Manager not ready for benchmark");
        return res;
    }

    bench_log(" Starting Memory Manager Performance Benchmark...");

    // Allocate 4 KB test pattern buffer
    uint8_t* test_buf = (uint8_t*)mm_alloc(MM_PAGE_SIZE, {MM_TIER_SRAM, true, 0});
    if (!test_buf) {
        bench_log("ERROR: Failed to allocate test buffer in SRAM");
        return res;
    }

    // Fill with pattern
    for (int i = 0; i < MM_PAGE_SIZE; i++) {
        test_buf[i] = (uint8_t)(i & 0xFF);
    }

    // ---------------------------------------------------------------------
    // Test 1: Write Throughput (Write 32 Pages = 128 KB)
    // ---------------------------------------------------------------------
    const uint32_t num_pages = 32;
    uint64_t start_us = esp_timer_get_time();

    for (uint32_t p = 1; p <= num_pages; p++) {
        mm_write(p, test_buf, MM_PAGE_SIZE);
    }

    uint64_t write_time_us = esp_timer_get_time() - start_us;
    res.avg_write_lat_us = (uint32_t)(write_time_us / num_pages);
    res.seq_write_mbps = ((float)(num_pages * MM_PAGE_SIZE) / (1024.0f * 1024.0f)) / ((float)write_time_us / 1000000.0f);
    bench_log("Write 128 KB (%u pages): %llu us | Avg Latency: %u us/page | Speed: %.2f MB/s",
              num_pages, write_time_us, res.avg_write_lat_us, res.seq_write_mbps);

    // ---------------------------------------------------------------------
    // Test 2: Read Throughput & Cache Hits
    // ---------------------------------------------------------------------
    uint8_t* read_buf = (uint8_t*)mm_alloc(MM_PAGE_SIZE, {MM_TIER_SRAM, true, 0});
    start_us = esp_timer_get_time();

    for (uint32_t p = 1; p <= num_pages; p++) {
        mm_read(p, read_buf, MM_PAGE_SIZE);
    }

    uint64_t read_time_us = esp_timer_get_time() - start_us;
    res.avg_read_lat_us = (uint32_t)(read_time_us / num_pages);
    res.seq_read_mbps = ((float)(num_pages * MM_PAGE_SIZE) / (1024.0f * 1024.0f)) / ((float)read_time_us / 1000000.0f);
    bench_log("Read 128 KB (%u pages): %llu us | Avg Latency: %u us/page | Speed: %.2f MB/s",
              num_pages, read_time_us, res.avg_read_lat_us, res.seq_read_mbps);

    // ---------------------------------------------------------------------
    // Test 3: 80/20 Locality Stress Test (100 accesses across 10 hot / 40 cold pages)
    // ---------------------------------------------------------------------
    mm_stats_t pre_st = mm_get_stats();

    for (int i = 0; i < 100; i++) {
        // 80% of accesses hit pages 1..5 (hot), 20% hit pages 6..20 (cold)
        uint32_t target_page = (random(100) < 80) ? (random(5) + 1) : (random(15) + 6);
        mm_read(target_page, read_buf, MM_PAGE_SIZE);
    }

    mm_stats_t post_st = mm_get_stats();
    uint32_t hits = post_st.hits - pre_st.hits;
    uint32_t misses = post_st.misses - pre_st.misses;
    uint32_t total = hits + misses;

    res.hit_rate_pct = total > 0 ? (100.0f * hits / total) : 0.0f;
    bench_log("Locality Stress Test (100 accesses): %u hits, %u misses (Hit Rate: %.1f%%)",
              hits, misses, res.hit_rate_pct);

    // Clean up test buffers
    mm_free(test_buf);
    mm_free(read_buf);

    res.passed = (res.hit_rate_pct >= 70.0f);
    bench_log(" Benchmark Complete. Result: %s", res.passed ? "PASSED ✅" : "FAILED ❌");

    return res;
}
