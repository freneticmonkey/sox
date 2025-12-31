#include "lib/benchmark.h"

#include <string.h>

#if defined(SOX_WINDOWS)
#include <windows.h>
#elif defined(SOX_MACOS)
#include <mach/mach_time.h>
#else
#include <time.h>
#endif

uint64_t sox_benchmark_now_ns(void) {
#if defined(SOX_WINDOWS)
    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#elif defined(SOX_MACOS)
    static mach_timebase_info_data_t info = {0};
    if (info.denom == 0) {
        mach_timebase_info(&info);
    }
    uint64_t t = mach_absolute_time();
    return (t * info.numer) / info.denom;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

sox_benchmark_config_t sox_benchmark_default_config(void) {
    return (sox_benchmark_config_t){
        .min_iterations = 1,
        .max_iterations = 1000000000ULL,
        .target_ns = 1000000000ULL,
        .max_rounds = 16,
    };
}

sox_benchmark_result_t sox_benchmark_run(const sox_benchmark_config_t* config,
                                         sox_benchmark_fn fn,
                                         void* user_data) {
    sox_benchmark_config_t cfg = sox_benchmark_default_config();
    if (config != NULL) {
        memcpy(&cfg, config, sizeof(cfg));
    }

    if (cfg.min_iterations == 0) {
        cfg.min_iterations = 1;
    }
    if (cfg.max_iterations < cfg.min_iterations) {
        cfg.max_iterations = cfg.min_iterations;
    }
    if (cfg.target_ns == 0) {
        cfg.target_ns = 1;
    }
    if (cfg.max_rounds == 0) {
        cfg.max_rounds = 1;
    }

    uint64_t iterations = cfg.min_iterations;
    uint64_t duration = 0;
    bool failed = false;
    uint32_t rounds = 0;

    for (rounds = 0; rounds < cfg.max_rounds; rounds++) {
        sox_benchmark_context_t ctx = {
            .iterations = iterations,
            .user_data = user_data,
            .failed = false,
        };

        uint64_t start = sox_benchmark_now_ns();
        fn(&ctx);
        uint64_t end = sox_benchmark_now_ns();

        duration = end - start;
        if (ctx.failed) {
            failed = true;
            break;
        }

        if (duration >= cfg.target_ns || iterations >= cfg.max_iterations) {
            break;
        }

        if (duration == 0) {
            if (iterations > cfg.max_iterations / 10) {
                iterations = cfg.max_iterations;
            } else {
                iterations *= 10;
            }
            continue;
        }

        double scale = (double)cfg.target_ns / (double)duration;
        uint64_t next = (uint64_t)((double)iterations * scale);
        if (next <= iterations) {
            if (iterations > cfg.max_iterations / 2) {
                next = cfg.max_iterations;
            } else {
                next = iterations * 2;
            }
        }
        if (next > cfg.max_iterations) {
            next = cfg.max_iterations;
        }
        iterations = next;
    }

    sox_benchmark_result_t result = {
        .iterations = iterations,
        .total_ns = duration,
        .ns_per_op = (iterations > 0) ? ((double)duration / (double)iterations) : 0.0,
        .rounds = rounds + 1,
        .failed = failed,
    };

    return result;
}
