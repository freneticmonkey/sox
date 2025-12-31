#include "test/benchmark_test.h"

#include "lib/file.h"
#include "vm.h"

static MunitResult _run_benchmark_fib(const MunitParameter params[], void *user_data) {
    (void)params;
    (void)user_data;

    const char* filename = "src/test/scripts/benchmark_fib_bench.sox";

    vm_config_t config = {
        .enable_serialisation = false,
        .suppress_print = true,
        .enable_benchmarks = true,
        .benchmark_time_seconds = 0.01,
        .benchmark_filter = "Fib",
        .args = l_parse_args(
            2,
            (const char *[]){
                "sox",
                filename
            }
        )
    };

    int status = l_run_file(&config);
    munit_assert_int(status, ==, 0);

    return MUNIT_OK;
}

MunitSuite l_benchmark_test_setup() {
    static MunitTest tests[] = {
        {
            .name = (char *)"run_benchmark_fib",
            .test = _run_benchmark_fib,
            .setup = NULL,
            .tear_down = NULL,
            .options = MUNIT_TEST_OPTION_NONE,
            .parameters = NULL,
        },
        {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL}
    };

    return (MunitSuite){
        .prefix = (char *)"bench/",
        .tests = tests,
        .suites = NULL,
        .iterations = 1,
        .options = MUNIT_SUITE_OPTION_NONE
    };
}
