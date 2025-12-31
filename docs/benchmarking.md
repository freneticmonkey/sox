# Benchmarking

Sox supports Go-style benchmarks with a simple naming convention and runner.

## Usage

```
sox my_bench.sox --bench
sox my_bench.sox --bench --bench-time 2.5
sox my_bench.sox --bench --bench-filter Fib
```

## Writing Benchmarks

Define functions that start with `Benchmark` and accept a single table argument
`b`. Use `b["n"]` (or `b["N"]`) as the iteration count.

```sox
fn fib(n) {
  if (n < 2) return n;
  return fib(n - 1) + fib(n - 2);
}

fn BenchmarkFib20(b) {
  var n = b["n"];
  var i = 0;
  while (i < n) {
    fib(20);
    i = i + 1;
  }
}
```

The runner will execute each benchmark with increasing `n` until it reaches
the target time and then prints results similar to:

```
BenchmarkFib20              100000       1234.56 ns/op
```

## C API

For C-based benchmarks (compiler, linker, runtime), use the helper in
`src/lib/benchmark.h`:

```c
static void bench_fn(sox_benchmark_context_t* ctx) {
    for (uint64_t i = 0; i < ctx->iterations; i++) {
        /* work */
    }
}

sox_benchmark_config_t cfg = sox_benchmark_default_config();
sox_benchmark_result_t result = sox_benchmark_run(&cfg, bench_fn, NULL);
```
