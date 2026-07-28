#ifndef MM_BENCHMARK_H
#define MM_BENCHMARK_H

#include "mm_types.h"
#include "config.h"

// =========================================================================
// Memory Manager Benchmark & Stress Test Suite
// =========================================================================

typedef struct {
    float    seq_write_mbps;     // Sequential write speed (MB/s)
    float    seq_read_mbps;      // Sequential read speed (MB/s)
    float    hit_rate_pct;       // Cache hit rate under 80/20 workload
    uint32_t avg_read_lat_us;    // Average cache read latency (microseconds)
    uint32_t avg_write_lat_us;   // Average cache write latency (microseconds)
    bool     passed;             // All tests passed without CRC/integrity errors
} mm_benchmark_result_t;

/**
 * @brief Run full benchmark suite (sequential throughput, hit rate, random access).
 * @return Benchmark result structure
 */
mm_benchmark_result_t mm_run_benchmark();

#endif // MM_BENCHMARK_H
