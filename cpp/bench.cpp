#include <benchmark/benchmark.h>

#include <vector>
#include <cstdint>
#include <utility>

// 根据你的实际路径调整
#include "hpp/hvector.hpp"

// ============================================================
// 1. push_back 逐个追加，不做 reserve
//    测试自动扩容的策略差异
// ============================================================
template <typename Vec>
static void BM_PushBack_NoReserve(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Vec v;
        for (int i = 0; i < n; ++i) {
            v.push_back(i);
        }
        benchmark::DoNotOptimize(v);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 2. push_back 前先 reserve
//    测试纯追加、无扩容的开销
// ============================================================
template <typename Vec>
static void BM_PushBack_WithReserve(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Vec v;
        v.reserve(n);
        for (int i = 0; i < n; ++i) {
            v.push_back(i);
        }
        benchmark::DoNotOptimize(v);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 3. emplace_back 逐个追加，不做 reserve
// ============================================================
template <typename Vec>
static void BM_EmplaceBack_NoReserve(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Vec v;
        for (int i = 0; i < n; ++i) {
            v.emplace_back(i);
        }
        benchmark::DoNotOptimize(v);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 4. 顺序遍历，求和使用 operator[]
// ============================================================
template <typename Vec>
static void BM_SequentialRead(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    Vec v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) v.push_back(i);

    for (auto _ : state) {
        std::int64_t sum = 0;
        for (int i = 0; i < n; ++i) {
            sum += v[i];
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 5. 随机访问
// ============================================================
template <typename Vec>
static void BM_RandomAccess(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    Vec v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) v.push_back(i);

    std::uint32_t seed = 12345;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    for (auto _ : state) {
        std::int64_t sum = 0;
        for (int i = 0; i < n; ++i) {
            sum += v[next() % n];
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 6. 拷贝构造
// ============================================================
template <typename Vec>
static void BM_CopyConstruct(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    Vec src;
    src.reserve(n);
    for (int i = 0; i < n; ++i) src.push_back(i);

    for (auto _ : state) {
        Vec copy(src);
        benchmark::DoNotOptimize(copy);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 7. 移动构造
// ============================================================
template <typename Vec>
static void BM_MoveConstruct(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    Vec src;
    src.reserve(n);
    for (int i = 0; i < n; ++i) src.push_back(i);

    for (auto _ : state) {
        Vec moved(std::move(src));
        benchmark::DoNotOptimize(moved);
        // 移完之后 src 状态未知，这里恢复一下保证下一轮还能用
        // 如果 Hvector 没实现 move，就把它注释掉
        src = std::move(moved);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 8. 析构（隐式通过作用域）
// ============================================================
template <typename Vec>
static void BM_ConstructDestruct(benchmark::State& state) {
    const int n = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Vec v;
        v.reserve(n);
        for (int i = 0; i < n; ++i) v.push_back(i);
        benchmark::DoNotOptimize(v);
    }
    state.SetItemsProcessed(state.iterations() * n);
}

// ============================================================
// 注册 benchmark
// ============================================================

// push_back / emplace_back 无 reserve，规模从 8 到 8192
#define REGISTER_GROWTH(BM_NAME, FUNC)                                   \
    BENCHMARK_TEMPLATE(FUNC, Hvector<int>)                               \
        ->Name(BM_NAME "/Hvector")                                       \
        ->RangeMultiplier(2)->Range(8, 8 << 10)->Unit(benchmark::kMicrosecond); \
    BENCHMARK_TEMPLATE(FUNC, std::vector<int>)                           \
        ->Name(BM_NAME "/std::vector")                                   \
        ->RangeMultiplier(2)->Range(8, 8 << 10)->Unit(benchmark::kMicrosecond)

REGISTER_GROWTH("PushBack_NoReserve",  BM_PushBack_NoReserve);
REGISTER_GROWTH("EmplaceBack_NoReserve", BM_EmplaceBack_NoReserve);
REGISTER_GROWTH("PushBack_WithReserve", BM_PushBack_WithReserve);

// 遍历 / 随机访问 / 构造析构
#define REGISTER_READ(BM_NAME, FUNC)                                     \
    BENCHMARK_TEMPLATE(FUNC, Hvector<int>)                               \
        ->Name(BM_NAME "/Hvector")                                       \
        ->RangeMultiplier(4)->Range(64, 64 << 10)->Unit(benchmark::kNanosecond); \
    BENCHMARK_TEMPLATE(FUNC, std::vector<int>)                           \
        ->Name(BM_NAME "/std::vector")                                   \
        ->RangeMultiplier(4)->Range(64, 64 << 10)->Unit(benchmark::kNanosecond)

REGISTER_READ("SequentialRead", BM_SequentialRead);
REGISTER_READ("RandomAccess",   BM_RandomAccess);
REGISTER_READ("ConstructDestruct", BM_ConstructDestruct);

// 拷贝 / 移动
#define REGISTER_COPYMOVE(BM_NAME, FUNC)                                 \
    BENCHMARK_TEMPLATE(FUNC, Hvector<int>)                               \
        ->Name(BM_NAME "/Hvector")                                       \
        ->RangeMultiplier(4)->Range(64, 64 << 10)->Unit(benchmark::kMicrosecond); \
    BENCHMARK_TEMPLATE(FUNC, std::vector<int>)                           \
        ->Name(BM_NAME "/std::vector")                                   \
        ->RangeMultiplier(4)->Range(64, 64 << 10)->Unit(benchmark::kMicrosecond)

REGISTER_COPYMOVE("CopyConstruct", BM_CopyConstruct);
REGISTER_COPYMOVE("MoveConstruct", BM_MoveConstruct);

BENCHMARK_MAIN();