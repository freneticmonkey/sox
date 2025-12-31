#ifndef SOX_BENCHMARK_H
#define SOX_BENCHMARK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct sox_benchmark_context_t {
    uint64_t iterations;
    void* user_data;
    bool failed;
} sox_benchmark_context_t;

typedef void (*sox_benchmark_fn)(sox_benchmark_context_t* ctx);

typedef struct sox_benchmark_config_t {
    uint64_t min_iterations;
    uint64_t max_iterations;
    uint64_t target_ns;
    uint32_t max_rounds;
} sox_benchmark_config_t;

typedef struct sox_benchmark_result_t {
    uint64_t iterations;
    uint64_t total_ns;
    double ns_per_op;
    uint32_t rounds;
    bool failed;
} sox_benchmark_result_t;

uint64_t sox_benchmark_now_ns(void);
sox_benchmark_config_t sox_benchmark_default_config(void);
sox_benchmark_result_t sox_benchmark_run(const sox_benchmark_config_t* config,
                                         sox_benchmark_fn fn,
                                         void* user_data);

#endif
