# Hvector — 手写动态数组容器

一个从零实现的、与 `std::vector` 核心行为兼容的动态数组容器，只用 `::operator new` / placement new / 显式析构来管理对象生命周期，不依赖 `std::vector`，不使用 `new T[n]`。

配套内容：25 组正确性测试、Google Benchmark 性能对比、ASan/UBSan 检查、gdb 调试记录，以及用 `Hvector<float>` 驱动的矩阵乘法缓存对比。

> **文档状态**：本文所有结论均在 **2026-09-24** 用仓库中当前的 `hpp/hvector.hpp` 实测复核过。
> 上一版 README 记录的是若干轮改动之前的代码，其中「第 11 节」的三个断言（拷贝构造泄漏、
> `Hvector(size_t)` 泄漏、`resize(n,val)` 过度扩容）**现在都已不成立**，已改写为「已修复」。
> 同时本轮核查发现了 3 个此前的 README 完全没有记录的真 bug，见第 11 节。

---

## 1. 目录结构

```text
vector/
├── hpp/
│   ├── hvector.hpp          # 容器本体（header-only 模板）
│   └── matmul.hpp           # 矩阵乘法实现（ijk / orl_ijk / ikj / jik / blocked / transposed）
├── cpp/
│   ├── main.cpp             # 正确性测试（25 组，带 assert 式校验）
│   ├── bench.cpp            # Hvector vs std::vector 的 microbenchmark
│   ├── matmul_main.cpp      # 矩阵乘法正确性验证（对照参考实现）
│   └── matmul_bench.cpp     # 矩阵乘法各循环顺序性能对比
├── CMakeLists.txt
└── README.md
```

---

## 2. 构建与运行

`CMakeLists.txt` 提供 `ENABLE_SANITIZERS` 开关，**默认 ON**，即 `-O0 -g -fsanitize=address,undefined`，用于正确性与内存检查。

```bash
# ① 调试/检查构建（默认）：跑正确性 + 内存检查
cmake -S . -B build
cmake --build build -j$(nproc)
./build/Hvector        # 25 组测试
./build/matmul_main    # 5 种算法对拍

# ② 性能构建：关掉插桩，否则 -O0 会让和 std::vector 的对比失去意义
cmake -S . -B build-release -DENABLE_SANITIZERS=OFF
cmake --build build-release -j$(nproc)
./build-release/bench
./build-release/matmul_bench
```

环境：g++ 13.3.0、Intel i5-13500HX（10 核 / 20 线程；L1d 480 KiB（10 实例，各 48 KiB）/ L2 12.5 MiB（10 实例，各 1280 KiB）/ L3 24 MiB）、Linux/WSL2。

> ⚠️ **修复记录**：在本次核查之前，`hpp/hvector.hpp:97,99` 有两处笔误——`data[i].~T();`（少了下划线）
> 和 `::operator deleta(data_);`（`delete` 拼错）。后者是**非依赖名的语法错误**，任何 include 这个
> header 的翻译单元都会直接编译失败，也就是**整个项目 5 个 target 全部编译不过**。
> 已修正为 `data_[i].~T();` 和 `::operator delete(data_);`，两条流水线现在都能干净构建。

---

## 3. 接口列表

### 已实现

| 分类 | 接口 |
| --- | --- |
| 构造/析构 | `Hvector()`、`explicit Hvector(size_t n)`、`Hvector(size_t n, const T& val)`、`Hvector(std::initializer_list<T>)`、`Hvector(iterator first, iterator last)`、`Hvector(const Hvector&)`、`Hvector(Hvector&&) noexcept`、`~Hvector()` |
| 赋值 | `operator=(const Hvector&)`、`operator=(Hvector&&) noexcept`、`assign(size_t count, const T&)`、`assign(size_t count, T&&)` |
| 元素访问 | `operator[]`（含 const 版）、`at`（含 const 版）、`front`、`back`、`data`（含 const 版） |
| 容量 | `size()`、`capacity()`、`empty()`（三者各含 const 版）、`reserve(n)`、`resize(n)`、`resize(n, val)`、`shrink_to_fit()`、`clear()` |
| 修改 | `push_back(const T&)`、`push_back(T&&)`、`emplace_back(Args&&...)`、`pop_back()`、`swap(Hvector&)` |
| 插入删除 | `insert(size_t idx, const T&)`、`insert(size_t idx, T&&)`、`insert(iterator, const T&)`、`insert(iterator, T&&)`、`emplace(size_t idx, Args&&...)`、`erase(size_t)`、`erase(iterator)`、`erase(iterator first, iterator last)` |
| 迭代器 | `begin()`、`end()`（各含 const 版），`iterator = T*`，`const_iterator = const T*` |
| 成员 typedef | `value_type`、`allocator_type`、`size_type`、`difference_type`、`reference`、`const_reference`、`pointer`、`const_pointer`、`iterator`、`const_iterator`、`reverse_iterator`、`const_reverse_iterator` |

`emplace_back` 是唯一的尾部构造入口，`push_back` 的两个重载都转发给它。

### 尚未实现

`insert(pos, n, val)`、`insert(pos, first, last)`、比较运算符（`==` / `<` 等）、`max_size()`、`get_allocator()`、`swap` 的自由函数重载、allocator 参数（`allocator_type` 这个 typedef 存在，但构造函数不接受 allocator 实参）。

### 与标准的已知行为差异

- **`emplace_back` / `emplace` 返回 `void`**。C++17 起 `std::vector::emplace_back` 返回新元素的**引用**，`emplace` 返回指向新元素的迭代器（本实现的 `emplace` 返回迭代器，这部分是对的，`emplace_back` 不是）。`auto& r = v.emplace_back(x);` 在本实现上编译失败（`forming reference to void`）。
- **`insert` 的下标重载会和迭代器重载产生歧义**：因为 `iterator` 就是 `T*`，传字面量 `0` 时 `insert(0, x)` 无法在 `insert(size_t, const T&)` 和 `insert(iterator, const T&)` 之间选择，编译报 `call of overloaded 'insert(int, int)' is ambiguous`。测试里因此都写作 `v.insert(size_t(0), x)`。`std::vector` 没有这个问题，因为它只有迭代器版本。
- **`reverse_iterator` / `const_reverse_iterator` 的 typedef 已定义，但没有 `rbegin()` / `rend()` / `crbegin()` / `crend()`**，这两个 typedef 目前是死代码。
- **越界行为比标准更严格**：`front()` / `back()` / `pop_back()` 在空容器上抛 `std::out_of_range`，而标准规定是 UB。更安全，但属于行为偏离，不能无条件当 `std::vector` 的替代品。
- **`assign(size_t, T&&)` 是非标准扩展**（标准只有 `assign(n, const T&)` / `assign(first, last)` / `assign(initializer_list)`）。
- `size()` / `capacity()` 同时提供 const 和非 const 重载；非 const 版本是冗余的（不返回引用也不修改状态），但无害。

---

## 4. 内存模型

三个成员：`T* data_`、`size_t size_`、`size_t capacity_`。

- **分配**：`::operator new(cap * sizeof(T))`——只拿原始字节，不构造任何对象。这正是不能改用 `new T[n]` 的原因：`new T[n]` 会对全部 n 个元素做默认构造，而容器在 `reserve` 之后、`push_back` 之前的那些槽位里根本不应该存在对象。对没有默认构造函数的 `T`（如 `std::unique_ptr` 之外的许多类型）它甚至无法编译。
- **构造**：`new (data_ + i) T(args...)`（placement new），只在真正有元素的位置调用。
- **析构**：`data_[i].~T()` 显式调用。`delete[]` 会析构整个缓冲区，而这里只有前 `size_` 个是活的，所以必须手工逐个析构，再 `::operator delete(data_)` 归还裸内存。

分配与构造、析构与释放是分开的两件事——对应 allocator 模型里 `allocate`/`construct` 和 `destroy`/`deallocate` 的区分。

**所有会构造元素的路径现在都有 `try/catch` 回滚**：4 个构造函数、`_reallocator`、`resize` 两个重载、`assign`、`shrink_to_fit`、`insert` 的扩容分支。唯一没有做到的是 `insert` / `emplace` 的**非扩容**路径和 `erase`，见第 6 节与第 11 节。

---

## 5. 扩容策略

**恰好 2 倍**：`emplace_back` 和 `insert` 在 `size_ == capacity_` 时调用 `_reallocator(capacity_ * 2)`；`reserve(n)` 则按请求的精确值扩容（`n <= capacity_` 时是 no-op）。

初始容量的边界情况：空容器上 `emplace_back` 会调用 `_reallocator(0)`，函数内部 `if (!new_capacity) new_capacity = 1;` 把它修正为 1。所以容量序列是 **1 → 2 → 4 → 8 → ...**（gdb 实测见第 9 节）。

2 倍而不是 1.5 倍：摊还代价都是 O(1)，2 倍在 `push_back` 场景下平均拷贝次数更少，代价是最坏情况下最多浪费约 50% 内存（1.5 倍约 33%）。这个实现没有把增长因子做成策略参数，是已知不足之一。

扩容的迁移过程（`_reallocator`，`hvector.hpp:432-454`）：

1. 先分配新缓冲区；
2. 用 `std::move_if_noexcept` 把旧元素搬到新缓冲区；
3. 全部成功后才析构旧元素、释放旧内存；
4. 任何一步抛异常，析构已构造的新元素、释放新缓冲区、`throw` 重抛，**原容器保持不变**。

第 2 步用 `move_if_noexcept` 而不是无条件 `std::move`，是因为：移动构造如果抛异常，被移动过的源对象状态是未指定的，"回滚" 无从谈起，强异常安全保证就没了；而拷贝构造失败时源对象不变，可以安全回滚。所以只有当 `T` 的移动构造是 `noexcept`（或者 `T` 不可拷贝）时才用移动。

实测验证（`Counter` 的移动构造标记为 `noexcept`）：一次触发 4→8 扩容的 `push_back` 产生了 `copy=0 move=4 dtor=4`，即 4 个旧元素全部是被**移动**而非拷贝的（`dtor=4` 是扩容后对旧元素逐个显式析构的计数）。

另外两个会重新分配缓冲区的接口：

- `shrink_to_fit()`（`hvector.hpp:298-326`）把容量收紧到 `size_`，同样走 `move_if_noexcept`；`size_ == 0` 时直接释放缓冲并把 `data_` 置 `nullptr`。
- `assign(count, val)`（`_assign_impl`，`hvector.hpp:557-585`）分配 `max(count, capacity_)`——**容量够用时保留原有容量**，不会因为 `assign` 而缩容（Test22 覆盖了这一点）。

---

## 6. 异常安全保证

下表每一行都是用一次性探针程序（`/tmp`，ASan/UBSan 开启）实测得到的，不是照抄标准。

| 操作 | 保证 | 实测 |
| --- | --- | --- |
| `_reallocator`（扩容，走拷贝回退） | **强保证** ✓ | 迁移第 3 个元素时抛异常，`size`/`cap`/元素全部不变，无泄漏 |
| `resize(n)` 增长路径（默认构造抛） | **强保证** ✓ | `size=2 cap=16` 不变，元素仍为 `0 1` |
| `resize(n, val)` 增长路径（拷贝构造抛） | **强保证** ✓ | `size=2 cap=16` 不变，元素仍为 `0 1` |
| `Hvector(const Hvector&)` | **强保证** ✓（构造失败即无对象，不泄漏） | 作用域结束后 `live=0` |
| `Hvector(size_t n)` | **强保证** ✓ | 抛异常后 `live=0` |
| `Hvector(size_t n, const T& val)` | **强保证** ✓ | 抛异常后只剩 `val` 一个活对象 |
| `Hvector(initializer_list)` | **强保证** ✓ | 抛异常后 `live=0` |
| `shrink_to_fit()` | **强保证** ✓ | 抛异常后容量仍为 64、元素仍为 `0 1 2 3` |
| `assign(count, val)` | **强保证** ✓ | 抛异常后 `size=3 cap=4`、元素仍为 `100 101 102` |
| `insert(pos, ...)` **扩容**路径 | **强保证** ✓ | 抛异常后 `size=4 cap=4`、元素仍为 `0 1 2 3` |
| `operator=(const Hvector&)` 需重新分配的分支 | **强保证** ✓ | 抛异常后 `dst` 仍为 `100 101` |
| `operator=(const Hvector&)` 复用已有容量的分支 | 基本保证（逐元素拷贝赋值，中途抛异常则部分元素已被覆盖） | 抛异常后 `dst = [0, 1, 102, 103]`——**状态有效但内容被部分改写** |
| `insert(pos, ...)` / `emplace(pos, ...)` **非扩容**路径 | **❌ 连基本保证都没有**，见 11.3 | 抛异常后 `size_` 不变，但某个槽位已被析构、另一个活对象落在了 `size_` 之外 |
| `erase(...)` | **❌ 异常被吞掉**，见 11.2 | 异常不传播，内容被静默改错 |

### 实测记录（强保证的几条）

扩容走拷贝回退路径（`T` 的拷贝构造会抛、移动构造非 `noexcept`，因此 `move_if_noexcept` 选择拷贝），在迁移第 3 个元素时抛异常：

```
   caught 'copy boom'
1 reserve() copy-fallback: threw=1
   base_live=4  live_now=4 (leak if > base)
   after:                    size=4 cap=4 live= 4 | 0 1 2 3
```

`resize` 增长时默认构造抛异常：

```
   caught 'default boom'
2 resize(n) default-throw: threw=1 live=2 (expect 2)
   after:                    size=2 cap=16 live= 2 | 0 1 
```

两处都做到了不泄漏、原容器不变。

### 实测记录（两条反例）

`operator=` 的复用容量分支只给基本保证：

```
  dst before: size=4 cap=4 live=3 | [0]=100 [1]=101 [2]=102 [3]=103 | dead_slots=0
  caught 'copy-assign boom'
  threw=1 (strong would keep 100 101 102 103)
  dst after:  size=4 cap=4 live=3 | [0]=0 [1]=1 [2]=102 [3]=103 | dead_slots=0
```

状态是有效的（没有泄漏也没有已析构的槽位），但前两个元素已经被改写——这就是「基本保证」的含义。

`erase` 则连异常都不往外传，见 11.2。

---

## 7. 迭代器失效规则

迭代器就是 `T*`，所以规则与 `std::vector` 一致：

| 操作 | 失效范围 |
| --- | --- |
| `push_back` / `emplace_back` / `insert` / `emplace` 触发扩容 | **全部**迭代器、指针、引用失效（缓冲区整个换了地址） |
| `push_back` / `emplace_back` / `insert` / `emplace` 未触发扩容 | `end()` 失效；其余保持有效 |
| `insert(pos, ...)` / `emplace(pos, ...)` 未扩容 | `pos` 及其之后的全部失效 |
| `erase(pos)` | `pos` 及其之后的全部失效 |
| `erase` / `pop_back` / `resize` 缩小 / `clear` / `assign` | 被删除元素及其之后的失效；`clear()` 和 `assign` 使全部失效 |
| `reserve(n)`（`n > capacity_`） | 全部失效 |
| `shrink_to_fit()` | 只要触发了重新分配（`size_ < capacity_` 且非空）就全部失效 |
| `resize(n)`（不触发扩容、且增大） | `end()` 失效 |
| 只读操作 / `operator[]` / `at` | 不失效 |

扩容导致失效的机理：新缓冲区是另一块地址，旧缓冲区在迁移完成后被 `::operator delete` 归还给分配器，旧指针立刻变成悬垂指针——不是"内容过期"而是**访问就是 use-after-free**。第 9 节的 gdb 记录里可以直接看到 `data_` 在每次扩容时地址变化。

同一个机理还有一个容易被忽略的受害者：**扩容时被当作实参传进来的引用**。`_insert_impl` 会先把迭代器换算成下标（`_index`，`hvector.hpp:429-431`）再决定要不要扩容，所以 `insert` 在扩容前后用的是下标而不是迭代器，安全；但 `emplace_back` 没有这个保护，见 11.1。

`insert` / `erase` 的迭代器重载**先做 `it - begin()` 计算出下标**，再走下标版本。这是必须的：如果先扩容再解引用迭代器，`it` 已经悬垂了。

移动之后的源对象：`data_ = nullptr, size_ = 0, capacity_ = 0`。它可以被析构、可以被赋值，但**不能假设里面还有元素**——所以 `for (auto& x : std::move(v))` 这类写法是错的，移动之后 `begin() == end() == nullptr`。

---

## 8. 性能对比（`-O2`，无插桩）

命令：`./build-release/bench --benchmark_min_time=0.3s --benchmark_repetitions=3`，取 3 次重复的**中位数**。
测量日期 2026-09-24。比值 = Hvector / std::vector，**小于 1 表示 Hvector 更快**。

### 8.1 追加写入与遍历

追加/构造类取 n = 8192，遍历/访存类取 n = 65536：

| Benchmark | n | Hvector | std::vector | 比值 |
| --- | ---: | ---: | ---: | ---: |
| `PushBack_NoReserve`（自动扩容） | 8192 | 5.70 µs | 6.49 µs | 0.88× |
| `EmplaceBack_NoReserve` | 8192 | 5.95 µs | 5.67 µs | 1.05× |
| `PushBack_WithReserve`（预分配） | 8192 | 4.02 µs | 3.91 µs | 1.03× |
| `SequentialRead` | 65536 | 8.12 µs | 9.21 µs | 0.88× |
| `RandomAccess` | 65536 | 93.11 µs | 109.02 µs | 0.85× |
| `ConstructDestruct` | 65536 | 38.92 µs | 53.35 µs | 0.73× |
| `CopyConstruct` | 65536 | 5.01 µs | 5.44 µs | 0.92× |
| `MoveConstruct` | 65536 | ~3 ns | ~4 ns | 0.80× |

**结论：与 `std::vector` 基本持平，多数点位互有胜负（大致在 ±25% 以内）。** 这个量级的差异在共享环境的微基准里相当一部分是噪声和代码布局效应，不宜解读为某一方更快。注意同一套代码在不同时间重跑，个别点位能差出 10–20%（例如 `EmplaceBack_NoReserve` 在单次运行的样本里出现过 5.40 µs / 9.54 µs 的极端值），所以这里用 3 次重复的中位数。

几点可以解释的观察：

- `PushBack_WithReserve` 明显快于 `PushBack_NoReserve`（4.02 µs vs 5.70 µs，快约 29%），差值就是 14 次扩容（容量 0→1→2→…→8192，共 14 次 `_reallocator` 调用，见 9.2 的 gdb 记录）的分配 + 迁移成本。这是"能预知规模就先 `reserve`"最直接的证据。
- `MoveConstruct` 约 3 ns，与 n 无关——移动是三个指针字段的交换，不碰元素。`CopyConstruct` 则严格随 n 线性增长（65536 元素 5.01 µs）。移动语义的价值就在这里。
- `RandomAccess` 这个测试用例是有缺陷的：它用 `next() % n` 生成下标，而 `next` 是 LCG（`seed = seed * 1664525 + 1013904223`），下一次迭代依赖上一次的结果。这条串行依赖链的长度本身就主导了耗时，测出来的更像 LCG 的速度而不是随机访存的速度。要真正测缓存行为应该预生成下标数组再遍历。见第 11 节。

### 8.2 矩阵乘法（行主序，`double`，GFLOPS 越高越好）

n = 512：

| 算法 | Hvector | std::vector | 说明 |
| --- | ---: | ---: | --- |
| `ijk`（朴素，最内层是 j） | 0.86 | 0.88 | 基准线 |
| `orl_ijk`（最内层是 k） | 0.85 | 0.84 | 与 ijk 同量级 |
| `jik` | 0.77 | 0.87 | 与 ijk 同量级 |
| **`ikj`** | **9.85** | **10.47** | **比 ijk 快 11.5×** |
| `blocked32`（分块，BS=32） | 7.58 | 7.35 | 比 ikj 慢 23% |
| `blocked64`（分块，BS=64） | 7.17 | 6.69 | 比 ikj 慢 27% |
| `transposed`（先转置 B 再 ikj） | 4.15 | 4.46 | 只有 ikj 的 42% |

n = 256：

| 算法 | Hvector | std::vector |
| --- | ---: | ---: |
| `ijk` | 3.20 | 3.24 |
| `orl_ijk` | 3.66 | 3.47 |
| `jik` | 3.27 | 3.24 |
| `ikj` | 12.03 | 12.36 |
| `blocked32` | 9.33 | 8.64 |
| `blocked64` | 6.68 | 7.16 |
| `transposed` | 5.09 | 4.69 |

**为什么 `ikj` 快**：`C[i*n+j] += A[i*n+k] * B[k*n+j]` 里 `j` 是最内层且只出现在下标 `B[k*n+j]` 和 `C[i*n+j]` 中，两者都连续递增——按行主序访问，每次命中同一缓存行，硬件预取器能完美工作。而 `ijk` 的最内层 `k` 会跳跃 `B` 的每一行（步长 `n*8` 字节），对 n=512 来说每次访问几乎必然缺失，缓存行里另外 7 个 double 全被浪费。

**为什么分块反而更慢**：`ikj` 本身已经是最优的访存模式，分块只是在它之上多加了循环开销和边界判断，没有带来额外收益。分块的价值在 `ijk` 那种访存糟糕的场景里——如果想看分块的效果，应该分块 `ijk` 而不是 `ikj`。这是当前实现的一个设计失误。

`transposed` 要付一次 O(n²) 的转置和 n² 的额外内存，换来的 GFLOPS 只有 `ikj` 的四成，性价比不如直接 `ikj`。

注意 n=512 时 `ikj` 从 n=256 的 12.03 掉到 9.85：n=512 时三个矩阵各 2 MiB，工作集 6 MiB 已经超过单核的 1.25 MiB L2、需要走 L3，而 n=256 时三个矩阵共 1.5 MiB 基本能待在 L2 里。

---

## 9. ASan / UBSan / gdb 记录

### 9.1 Sanitizer

构建时开启 `-fsanitize=address,undefined`（默认配置）：

```
$ ./build/Hvector
...
==== Test24: (count, value) 填充构造 ====
PASS fill ctor int
PASS fill ctor zero count
PASS fill ctor string + growth

==== Test25: initializer_list 构造 ====
PASS init_list int: init_list int size=4, cap=4 : 1 2 3 4 
PASS init_list deep copy
PASS init_list string
PASS init_list empty

All test done
```

**25 组测试全部通过，ASan/UBSan 无任何报告**，进程退出码 0——就这些测试覆盖到的路径而言无泄漏、无越界、无 use-after-free、无 double free、无未定义行为。

`./build/matmul_main` 五种算法对拍参考实现全部 OK：

```
ijk        : OK
ikj        : OK
jik        : OK
blocked    : OK
transposed : OK
```

Valgrind 未安装，功能由 ASan 覆盖（LeakSanitizer 在进程退出时做全量泄漏检查，能力上等价于 `valgrind --leak-check=full`，且对 UAF/越界更敏感）。

> ⚠️ **但「ASan 干净」不等于「没有 bug」**：25 组测试全部通过、ASan 全静默的情况下，
> 第 11 节的三个 bug 依然存在。原因是它们都落在**测试没覆盖到的路径**上（见第 10 节的「覆盖盲区」），
> 而且其中两个还需要「某个操作恰好抛异常」或「实参恰好引用容器自身」才会触发。
> 还有一层：ASan 只能看见插桩过的代码，如果 UAF 的读发生在未插桩的库代码内部（比如 libstdc++
> 的 `std::string` 拷贝构造里），ASan 可能**不报**——11.1 就是这种情况，用 `int` 实例化才会稳定触发。

### 9.2 gdb 记录：扩容时 `data_` 如何变化

断点打在 `_reallocator` 上，观察每次扩容前的状态：

```gdb
break Hvector<int>::_reallocator(unsigned long)
commands
  silent
  printf ">>> grow: old=data_=%p size_=%2lu cap_=%2lu  -->  new_cap=%lu\n", \
         data_, size_, capacity_, new_capacity
  continue
end
run
```

输出节选（`./build/Hvector` 的 Test1，连续 `emplace_back`）：

```
>>> grow: old=data_=(nil)              size_= 0 cap_= 0  -->  new_cap=0
>>> grow: old=data_=0x502000000010     size_= 1 cap_= 1  -->  new_cap=2
>>> grow: old=data_=0x502000000030     size_= 2 cap_= 2  -->  new_cap=4
```

可以读出的三件事：

1. **容量序列是 1 → 2 → 4 → 8**，`new_cap=0` 那一次是空容器上的首次 `emplace_back`，`capacity_ * 2 == 0` 被函数内部的 `if (!new_capacity) new_capacity = 1;` 修正。
2. **`data_` 每次都在变**（`(nil)` → `0x...010` → `0x...030`），地址间隔 0x20 = 32 字节 = 4 个 int，正好是上一轮的容量。这直接证明了扩容是"换一块新内存"，而不是原地扩张。
3. 旧地址在迁移后立刻被 `::operator delete` 释放。这解释了第 7 节的核心结论：**扩容后旧迭代器不是"内容过期"，而是悬垂指针，解引用就是 use-after-free**。ASan 会直接报错。

常用调试命令：`p data_` / `p size_` / `p capacity_` 查看状态，`bt` 看调用链（扩容时链路是 `main → emplace_back → _reallocator → operator new`）。

---

## 10. 测试覆盖

`cpp/main.cpp` 共 25 组，全部通过：

- **扩容与访问**（Test1–Test8）：连续 `emplace_back` 自动扩容、拷贝构造、拷贝赋值、自赋值、移动构造、移动赋值、`std::sort` 排序、`at` 越界抛异常。
- **insert**（Test9–Test12）：头部/中间/尾部插入、空容器首插、触发扩容时容量增长、迭代器版本（`begin()` / `begin()+2` / `end()`）、右值移动插入（`std::string`）。
- **erase**（Test13–Test15）：头部/中间/尾部删除、删到空、迭代器版本 + 返回值（含"边遍历边删偶数"）、删空后再插入。
- **资源类型与只移动类型**（Test16–Test17）：`std::string` 的 insert/erase、`std::unique_ptr<int>` 的 insert/erase。
- **越界**（Test18–Test19）：`insert` / `erase` 越界抛 `std::out_of_range`，空容器 `erase(0)` 抛异常。
- **压力**（Test20）：1000 元素批量插入 + 隔一个删一个，校验剩余元素恰为 `1,3,5,...`。
- **新接口**（Test21–Test25）：`shrink_to_fit`（含 `size_==0` 分支必须把 `data_` 置空）、`assign`（容量内 / 扩容 / 别名实参 `v.assign(3, v[2])` / 空 / `std::string`）、`emplace`（空容器 / 头 / 中 / 尾 / 变参 / 越界）、`(count, value)` 填充构造、`initializer_list` 构造（含深拷贝与空列表）。

另外在 README 编写过程中用一次性探针程序（`/tmp/probe_*.cpp`）验证了异常路径、别名与生命周期——**第 11 节的三个 bug 正是这些探针发现的，25 组测试一组都没抓到**。

### 覆盖盲区（这就是 11.1/11.2/11.3 能长期潜伏的原因）

| 未覆盖的东西 | 后果 |
| --- | --- |
| `Hvector(iterator first, iterator last)` 区间构造 | **至今没有任何测试**。它的 catch 块里藏着两处笔误，因为模板没被实例化，编译器只检查语法、不检查语义，结果整个项目编译不过却直到本次核查才暴露。 |
| 实参引用容器自身（`v.push_back(v[0])`） | 11.1 的 use-after-free 完全没被覆盖。 |
| 需要「恰好抛异常」才出现的路径 | `erase` 吞异常、`insert` 非扩容路径残留已析构槽位，25 组测试都跑的是不抛异常的版本。 |
| `assign(size_t, T&&)`、`insert(pos, n, val)` 相关路径 | 无覆盖（后者也未实现）。 |
| `reverse_iterator` 相关 | typedef 有了，但没有 `rbegin/rend`，也就无从测试。 |

---

## 11. 已知不足

严重程度从高到低。

### 11.1 `emplace_back` / `push_back` 的别名 use-after-free（真 bug，ASan 已复现）

`hvector.hpp:213-220`：

```cpp
template<typename... Args>
void emplace_back(Args&&... args) {
    if(size_ >= capacity_) {
        _reallocator(capacity_*2);                       // ← 先把缓冲区换掉，旧的被 delete
    }
    new (data_ + size_) T(std::forward<Args>(args)...);  // ← args 此时可能已悬垂
    size_++;
}
```

如果 `args` 里有一个引用指向**容器自己的元素**，扩容会把那块内存 `::operator delete` 掉，随后构造新元素时读的就是已释放内存。

实测对照（`std::string` 与 `int` 两种实例化）：

```
std::vector<string>  push_back(v[0])    -> ['AAA' 'BBB' 'AAA']              ✓ 正确
std::vector<int>     push_back(v[0])    -> [11 22 11]                       ✓ 正确
Hvector<string>      push_back(v[0])    -> ['AAA' 'BBB' '']                 ✗ UAF
Hvector<string>      emplace_back(v[1]) -> ['one' 'two' 'three' 'four' '']  ✗ UAF
Hvector<string>      insert(0, v[1])    -> ['BBB' 'AAA' 'BBB']              ✓ 正确（insert 路径避开）
```

`std::vector` 是**必须**处理这个的——标准明确要求 `v.push_back(v[0])` 合法，libstdc++ 的做法是先把实参拷成临时量再扩容。

ASan 报告（用 `Hvector<int>` 实例化时稳定触发）：

```
==51180==ERROR: AddressSanitizer: heap-use-after-free on address 0x502000000030
READ of size 4 at 0x502000000030 thread T0
    #0 Hvector<int>::emplace_back<int const&>(int const&) hpp/hvector.hpp:218
    #1 Hvector<int>::push_back(int const&) hpp/hvector.hpp:222
freed by thread T0 here:
    #1 Hvector<int>::_reallocator(unsigned long) hpp/hvector.hpp:450
    #2 Hvector<int>::emplace_back<int const&>(int const&) hpp/hvector.hpp:216
previously allocated by thread T0 here:
    #1 Hvector<int>::_reallocator(unsigned long) hpp/hvector.hpp:434
SUMMARY: AddressSanitizer: heap-use-after-free hpp/hvector.hpp:218
```

**`-O2`（无插桩）下它会从"读到错值"升级成内存损坏**——读出来的垃圾长度被当成字符串长度，程序直接崩：

```
$ g++ -std=c++17 -O2 -I. probe.cpp && ./a.out
std::vector<string> push_back(v[0]) -> ['AAA' 'BBB' 'AAA']
std::vector<int>    push_back(v[0]) -> [11 22 11]
terminate called after throwing an instance of 'std::length_error'
  what():  basic_string::_M_create
```

> **注意一个反直觉的点**：这个 bug 用 `std::string` 触发时 **ASan 可能一声不吭**。
> 因为 UAF 的那次读发生在未插桩的 libstdc++ 内部（`std::string` 的拷贝构造是库里的外部函数）；
> 只有用 `int` 这类拷贝构造会被内联进模板代码的类型实例化，ASan 才能插桩到那次读。
> **不要用「ASan 没报」当成这个 bug 不存在的证据。**

**修复方向**：照搬 `insert` 扩容分支已经做对的那套（`_insert_with_strong_guarantee_impl`）——要么先把实参拷贝/移动到一个临时对象再扩容，要么改成"先在新缓冲区里构造好全部元素（含新元素）、成功后再析构旧缓冲区"，后者天然没有别名问题。

### 11.2 `erase` 静默吞掉异常（真 bug，已复现）

`hvector.hpp:526-555` 的 `_erase_impl` 里，`catch(...)` 做了状态修补但**没有 `throw;`**：

```cpp
try {
    std::move(last, end(), first);
    for(size_t i = new_size; i < size_; ++i) data_[i].~T();
    size_ = new_size;
} catch(...) {
    size_t p = fidx, q = lidx;
    while(q < size_) { /* 修补... */ }
    for(size_t i = p;i < size_; ++i) data_[i].~T();
}                     // ← 没有 throw;
size_ = new_size;
return data_ + fidx;
```

让 `T::operator=(T&&)` 在第 2 次调用时抛，`v.erase(0)` 表现为：

```
14 erase before: size=6 0 1 2 3 4 5 
    erase PROPAGATED=0  (0 => exception swallowed)
    after:                   size=5 cap=8 live= 5 | 2 2 3 4 5
```

- **异常没往上传**（`threw=false`），调用方完全无法感知失败。
- **内容被静默改错**：`erase(0)` 本该得到 `1 2 3 4 5`，实际得到 `2 2 3 4 5`——元素 `1` 丢了，`2` 重复了一次。

注意仓库里**其余所有**异常路径（`_reallocator`、`resize`、各构造函数、`shrink_to_fit`、`assign`、`insert` 扩容分支）都规规矩矩地 `throw;` 重抛，只有这一处漏了。这个 catch 块本身还在 `kill` 掉一个异常之后继续往下走，等于把一个"保证失败"的操作伪装成成功返回。

### 11.3 `insert` / `emplace` 非扩容路径：连基本保证都没有（真 bug，ASan 已复现）

`_insert_impl`（`hvector.hpp:472-486`）的非扩容分支先把尾部元素整体后移一格，再在空出来的位置构造新元素：

```cpp
} else {
    _shift_elements_backward(pos, this->end(), this->end() + 1);  // 搬移尾部
    new (pos) T (std::forward<U>(val));                            // ← 这里抛，就完了
    size_++;
}
```

`_shift_elements_backward`（`hvector.hpp:462-470`）从后往前逐个 `uninitialized_move` **再** `p->~T()`。这个顺序在抛异常时会同时留下两个烂摊子。

用探针把每个对象标记 `ALIVE`/`DEAD`（析构时把 magic 改成 `DEAD`），让搬移过程中的第 2 次移动构造抛异常：

```
=== (a) insert(middle) NO-growth, shift's 2nd move-construct throws ===
  before: size=4 cap=16 live=4 | [0]=0 [1]=1 [2]=2 [3]=3 | dead_slots=0
  caught 'move boom'
  threw=1
  after:  size=4 cap=16 live=4 | [0]=0 [1]=1 [2]=2 [3]=DESTROYED | dead_slots=1  <-- INVALID
```

- **`size_` 仍是 4，但槽位 `[3]` 已经被析构了**——容器声称这 4 个槽位里都有活对象，访问 `v[3]` 就是读一个已析构对象（UB）。这比"基本保证"还差：基本保证要求「状态有效但内容未指定」，而这不是有效状态。
- **有一个活对象落在了 `size_` 之外**（`live=4`，但 `[0..3]` 里只有 3 个活的，第 4 个在槽位 `[4]`）。析构函数只析构 `data_[0..size_)`，**槽位 `[4]` 那个对象永远不会被析构**——对持有资源的类型就是真泄漏。同时析构函数还会去析构槽位 `[3]`，即**二次析构**。

同样的输入用持有堆资源的类型（内部一个 `unique_ptr<int>`）跑，LeakSanitizer 直接报泄漏并且进程退出码为 1：

```
before: size=4 cap=16
caught 'move boom'
after: size=4 cap=16
scope ended

==55470==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #1 Owner::Owner(Owner const&) /tmp/probe_leak.cpp:11
    #2 Hvector<Owner>::_reallocator(unsigned long) hpp/hvector.hpp:439
    #3 Hvector<Owner>::reserve(unsigned long) hpp/hvector.hpp:238
SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s).
```

****同一个位置的新元素构造抛异常也一样**（`insert(1, tmp)`，`tmp` 的拷贝构造抛），残留的已析构槽位从 `[3]` 变成 `[1]`：

```
  before: size=4 cap=16 live=0 | [0]=0 [1]=1 [2]=2 [3]=3 | dead_slots=0
  caught 'copy boom'
  threw=1
  after:  size=4 cap=16 live=0 | [0]=0 [1]=DESTROYED [2]=1 [3]=2 | dead_slots=1  <-- INVALID STATE (size claims live objects)
```

对照组：同样的 `insert` 如果**触发扩容**，走的是 `_insert_with_strong_guarantee_impl`，它先在新缓冲区里把所有元素（含新元素）构造好、成功后才析构旧缓冲区，所以既没有别名问题也没有残留槽位——抛异常时容器保持原样（`size=4 cap=4`、元素仍为 `0 1 2 3`），不抛异常时得到正确结果 `0 77 1 2 3`，`dead_slots=0`。所以这个 bug 只在**容量够用**的中间/头部插入时出现——而这恰好是更常见的情况。

**修复方向**：`_shift_elements_backward` 不要"移动一个、析构一个"，改成先整体在 `size_` 位置构造好（`uninitialized_move` 不会抛的话），或者干脆复用扩容分支那套"新缓冲区 + 成功后再换"的写法。

### 11.4 其它

- **没有 allocator 支持**。分配走 `::operator new` 硬编码，无法定制内存来源，也没有 `get_allocator()`。改成 `std::allocator_traits<Alloc>` 是下一步。
- **`capacity_` 溢出的极端情况没处理**：`_reallocator(capacity_ * 2)` 在 `capacity_` 接近 `SIZE_MAX/2` 时会回绕成 0，随后被 `if (!new_capacity) new_capacity = 1;` 悄悄"修正"成一个极小的值。也没有 `max_size()`。
- **`operator=(const Hvector&)` 的复用分支只有基本保证**（6 节实测），而重新分配的分支是强保证。要么统一成 copy-and-swap 拿强保证，要么接受这个不对称。
- **接口不完整**（见第 3 节的完整列表），尤其缺比较运算符和 `rbegin/rend`。
- **不符合 STL 容器的形式化要求**：`emplace_back` 返回 `void`、`insert` 下标重载与迭代器重载有歧义、`front`/`back`/`pop_back` 越界抛异常而非 UB。不能直接当作 `std::vector` 的替代品。
- **增长因子写死 2 倍**，没有做成策略。
- **`RandomAccess` 基准用例设计有缺陷**（见 8.1），测的是 LCG 依赖链而非访存。
- **分块矩阵乘法分错了对象**，对已经最优的 `ikj` 分块，收益为负；应该对 `ijk` 分块。
- **矩阵乘法没有做向量化**：没有 `-march=native`、没有显式 SIMD、没有 OpenMP。`ikj` 的 9.85 GFLOPS 是标量 `-O2` 的结果，离单核理论上限还有很大空间。
- **`size()` / `capacity()` 的非 const 重载是冗余的**，没有存在价值但也不会出错。

### 已修复（上一版 README 记录为未修复）

| 编号 | 问题 | 修复方式 | 实测验证 |
| --- | --- | --- | --- |
| 旧 11.1 | 拷贝构造函数异常时泄漏缓冲区 | `hvector.hpp:107-123` 加了 `constructed` 计数 + `try/catch`，catch 里析构已构造元素、`::operator delete`、重抛 | 6 元素源容器在第 3 次拷贝时抛异常，作用域结束后 `live=0`；LeakSanitizer 静默，退出码 0 |
| 旧 11.2 | `Hvector(size_t)` 异常时泄漏，且 `size_` 已提前写成 `cap` | `hvector.hpp:30-44` 同样加回滚 | 默认构造在第 3 个元素抛异常后 `live=0`，无泄漏 |
| 旧 11.3 | `resize(n, val)` 条件写成 `new_size > size_`，容量够用时仍翻倍扩容 | 条件改为 `new_size > capacity_`，与单参版一致（`hvector.hpp:268`） | `cap=64` 时 `resize(5,val)` / `resize(8,val)` 后容量仍是 64（旧行为会变成 128） |
| 新 | `Hvector(iterator, iterator)` 的 `data[i]` / `::operator deleta` 两处笔误导致整个项目编译不过 | 改为 `data_[i]` / `::operator delete` | 修复前 5 个 target 全部编译失败，修复后全部通过 |

---

## 12. 自测问题

1. **为什么不能用 `new T[n]` 作底层存储？** 它会对全部 n 个元素做默认构造。`reserve` 之后到 `push_back` 之前的槽位里不该有对象；对没有默认构造函数的 `T` 直接编译失败；而且 `delete[]` 会析构整个缓冲区，而只有前 `size_` 个是活的。
2. **`allocate` 和 `construct` 的区别？** `allocate` 只拿原始内存、不构造对象、可以用 `T*` 算术但不能解引用；`construct` 在已分配的内存上用 placement new 构造对象。分开是为了让容器能在"容量够但元素不够"的状态下存在，也是 `std::vector<bool>` 之类特化能工作的前提。对应到本实现就是 `::operator new` 与 placement new。
3. **为什么析构要显式调 `T::~T()`？** 因为缓冲区里只有 `size_` 个活对象，而 `::operator delete` 不调用任何析构函数（`delete[]` 才会，但会析构全部 `capacity_` 个）。
4. **`reserve` 后 `size` 和 `capacity` 怎么变？** `size` 不变；`capacity >= n`（本实现是恰好 `n`）。不构造任何元素。
5. **`resize` 增大时新元素如何初始化？** 单参版值初始化（`T()`，对 POD 是零初始化），双参版用给定值拷贝构造。
6. **扩容时旧元素怎么迁移？** `std::move_if_noexcept`——移动构造 `noexcept`（或不可拷贝）时移动，否则拷贝。实测 `Counter` 走的是移动（一次 4→8 扩容 `copy=0 move=4 dtor=4`）。
7. **为什么移动构造要尽量 `noexcept`？** 否则容器在扩容时会退化成拷贝（丢失性能），而且移动中途抛异常时源对象状态未指定、无法回滚，强异常安全保证无法维持。11.3 就是这句话的反面教材：`_shift_elements_backward` 里移动抛异常时，容器直接进入了无效状态。
8. **拷贝赋值为什么推荐 copy-and-swap？** 一次拷贝构造 + 一次 `swap` 就天然获得强异常安全、自动处理自赋值、且不需要分别写"容量够"和"容量不够"两条路径。本实现为了省这次分配手写了两条分支——现在两条分支**都**保证了不泄漏，但复用容量那条只有基本保证（6 节实测 `dst` 被部分改写）。手写多分支的代价就在这儿：**每条分支都要自己保证正确性，而人总会漏掉一条**——11.2 那个漏写 `throw;` 的 catch 块、11.3 那个只覆盖非扩容路径的疏忽，都是同一类错误。
9. **哪些操作导致迭代器失效？** 见第 7 节。核心是：任何触发重新分配的操作让**全部**失效；`insert`/`erase` 从操作点向后失效；`end()` 在几乎所有修改操作后都失效。
10. **`Hvector<std::unique_ptr<int>>` 为什么不能拷贝？** 拷贝构造会实例化 `T(other[i])`，而 `unique_ptr` 的拷贝构造是 `= delete`，模板实例化失败。这也正是 `std::move_if_noexcept` 里"不可拷贝则移动"分支存在的原因——`Test17` 验证了移动路径能正常工作。
11. **为什么 `emplace_back` 里"先扩容、后构造"是危险的？** 见 11.1。扩容会 `::operator delete` 掉旧缓冲区，而 `std::forward<Args>(args)...` 里如果有一个实参引用着旧缓冲区里的元素，构造新元素时读到的就是已释放内存。标准明确要求 `v.push_back(v[0])` 必须合法，所以这不是"用户用错了"，而是实现必须处理的情况。正确顺序是：先把实参落成一个临时对象，或者"先把所有元素（含新元素）在新缓冲区里构造好，成功后再拆旧缓冲区"——后者正是 `insert` 扩容分支在做的事。
12. **栈、堆、缓存和 vector 的关系？** 容器对象本身（`T* data_` + 两个 `size_t`，共 24 字节）在栈上，元素全在堆上，两者通过 `data_` 这一个指针联系起来。元素在堆上连续存放，所以顺序遍历时硬件预取器能持续命中，这是 `SequentialRead` 比 `RandomAccess` 快约一个数量级的原因（8.12 µs vs 93.11 µs @ 65536）。`reserve` 的价值在于减少堆分配次数（13 次 → 1 次），`ikj` 的价值在于让堆上那 n² 个 double 被按缓存行而不是按 n×8 字节的步长访问。
13. **为什么矩阵乘法 `ikj` 比 `ijk` 缓存友好？** 见 8.2，实测差 11.5 倍。`ikj` 最内层连续访问两个数组，`ijk` 最内层以 `n*8` 字节为步长跳跃访问 `B`。

---

## 13. 下一步

按优先级：

1. **修 11.1**：`emplace_back` 的别名 use-after-free。最省事的做法是照搬 `_insert_with_strong_guarantee_impl` 的"新缓冲区 + 成功后替换"结构。
2. **修 11.3**：`_shift_elements_backward` 在异常时会留下已析构的槽位和 `size_` 之外的活对象，对持有资源的类型是真泄漏。
3. **修 11.2**：`_erase_impl` 的 catch 补上 `throw;`（或重新设计成不吞异常）。
4. **补测试**：给上面三条各写一个"抛异常版本"的回归测试（现有 25 组全是不抛异常的）；给 `Hvector(iterator, iterator)` 区间构造补测试——它至今零覆盖，正是它能带着两处笔误一路潜伏到编译不过都没人发现的原因。
5. 加比较运算符、`rbegin/rend`、`insert(pos, n, val)`，把 `emplace_back` 的返回值对齐标准。
6. 改成 `std::allocator_traits` 版本，实现原书 V3 的 allocator 目标；顺便补 `max_size()` 和 `capacity_ * 2` 的溢出检查。
7. 把增长因子做成模板参数，对比 1.5 倍与 2 倍在 `push_back` 场景下时间与峰值内存的权衡。
8. 重写 `RandomAccess` 用例（预生成下标），并对 `ijk` 分块 + `-march=native` 看向量化收益。
