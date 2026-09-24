#include <benchmark/benchmark.h>
#include <vector>
#include "hpp/hvector.hpp"
#include "hpp/matmul.hpp"

using T = double;

// 用同样的大小为 n*n 的两个矩阵，分别用 Hvector / std::vector 填充
template <typename Vec>
static void fill_input(Vec& A, Vec& B, Vec& C, size_t n) {
    for (size_t i = 0; i < n*n; ++i) {
        A[i] = T(i % 7 + 1);
        B[i] = T(i % 11 + 1);
        C[i] = T(0);
    }
}

// 统一报告 GFLOPS
#define REPORT_GFLOPS(n)                                                   \
    do {                                                                   \
        double f = 2.0 * (double)(n) * (double)(n) * (double)(n);           \
        state.counters["GFLOPS"] = benchmark::Counter(                      \
            f, benchmark::Counter::kIsIterationInvariantRate,               \
            benchmark::Counter::kIs1000);                                   \
    } while (0)

#define DEFINE_BENCH(NAME, FN_BODY)                                         \
    template <typename Vec>                                                 \
    static void NAME(benchmark::State& state) {                             \
        const size_t n = (size_t)state.range(0);                            \
        Vec A(n*n), B(n*n), C(n*n);                                         \
        fill_input(A, B, C, n);                                             \
        for (auto _ : state) {                                              \
            FN_BODY;                                                        \
            benchmark::ClobberMemory();                                     \
        }                                                                   \
        REPORT_GFLOPS(n);                                                   \
    }

DEFINE_BENCH(BM_matmul_orl_ijk,
    matmul::orl_ijk<T>(A.data(), B.data(), C.data(), n))

DEFINE_BENCH(BM_matmul_ijk,
    matmul::ijk<T>(A.data(), B.data(), C.data(), n))

DEFINE_BENCH(BM_matmul_ikj,
    matmul::ikj<T>(A.data(), B.data(), C.data(), n))

DEFINE_BENCH(BM_matmul_jik,
    matmul::jik<T>(A.data(), B.data(), C.data(), n))

DEFINE_BENCH(BM_matmul_blocked32,
    matmul::blocked<T>(A.data(), B.data(), C.data(), n, 32))

DEFINE_BENCH(BM_matmul_blocked64,
    matmul::blocked<T>(A.data(), B.data(), C.data(), n, 64))

// transposed 需要额外 buffer
template <typename Vec>
static void BM_matmul_transposed(benchmark::State& state) {
    const size_t n = (size_t)state.range(0);
    Vec A(n*n), B(n*n), C(n*n), Bt(n*n);
    fill_input(A, B, C, n);
    for (auto _ : state) {
        matmul::transposed<T>(A.data(), B.data(), C.data(), Bt.data(), n);
        benchmark::ClobberMemory();
    }
    REPORT_GFLOPS(n);
}

// 注册：每种算法各跑 Hvector 和 std::vector
#define REGISTER(name, fn)                                                  \
    BENCHMARK_TEMPLATE(fn, Hvector<T>)                                      \
        ->Name(name "/Hvector")                                             \
        ->RangeMultiplier(2)->Range(64, 512)->Unit(benchmark::kMillisecond);\
    BENCHMARK_TEMPLATE(fn, std::vector<T>)                                  \
        ->Name(name "/std::vector")                                         \
        ->RangeMultiplier(2)->Range(64, 512)->Unit(benchmark::kMillisecond)

REGISTER("orl_ijk",    BM_matmul_orl_ijk);
REGISTER("ijk",        BM_matmul_ijk);
REGISTER("ikj",        BM_matmul_ikj);
REGISTER("jik",        BM_matmul_jik);
REGISTER("blocked32",  BM_matmul_blocked32);
REGISTER("blocked64",  BM_matmul_blocked64);
REGISTER("transposed", BM_matmul_transposed);

BENCHMARK_MAIN();