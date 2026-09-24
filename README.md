# Hvector — 手写动态数组容器

一个从零实现的、与 `std::vector` 核心行为兼容的动态数组容器，只用 `::operator new` / placement new / 显式析构来管理对象生命周期，不依赖 `std::vector`，不使用 `new T[n]`。

配套内容：30 组正确性测试、Google Benchmark 性能对比、ASan/UBSan 检查、gdb 调试记录，以及用 `Hvector<double>` 驱动的矩阵乘法缓存对比。

> **文档状态**：本文所有结论均在 **2026-09-24** 用仓库中当前的 `hpp/hvector.hpp` 实测复核过。
>
> 本轮（第三轮）把上一轮记录为「已知不足」的三个 bug 真正修掉了：
> **11.1**（别名实参读到已被搬走的元素）、**11.2**（`erase` 的修补 `catch` 会留下死槽位）、
> **11.3**（非扩容插入抛异常时泄漏）。修复过程中又发现了**第 4 个同类 bug**：
> `insert` 的两条路径同样受别名实参影响，见 **11.4**。
>
> 三条修复各配了一个「会抛异常」的回归测试，测试组数本轮 **26 → 30**（三轮下来 25 → 30）；其中
> `Hvector(iterator, iterator)` 区间构造是**第一次**拿到覆盖——它此前零覆盖，
> 正是能带着两处笔误一路潜伏到「整个项目编译不过」的原因。
>
> 诚实标注两处时效性：**第 8 节的性能数字仍是上一轮采集的**（本轮只改了扩容路径内部的
> 构造**顺序**，没有重跑 benchmark，所以那一节应读作「改动前的实现」）;
> 上一版 README 里另外三个更早的断言（拷贝构造泄漏、`Hvector(size_t)` 泄漏、
> `resize(n,val)` 过度扩容）在更早的轮次就已修复，保留在第 11 节末尾的「已修复」表里。

---

## 1. 目录结构

```text
vector/
├── hpp/
│   ├── hvector.hpp          # 容器本体（header-only 模板）
│   └── matmul.hpp           # 矩阵乘法实现（ijk / orl_ijk / ikj / jik / blocked / transposed）
├── cpp/
│   ├── main.cpp             # 正确性测试（30 组，带 assert 式校验）
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
./build/Hvector        # 30 组测试
./build/matmul_main    # 5 种算法对拍

# ② 性能构建：关掉插桩，否则 -O0 会让和 std::vector 的对比失去意义
cmake -S . -B build-release -DENABLE_SANITIZERS=OFF
cmake --build build-release -j$(nproc)
./build-release/bench
./build-release/matmul_bench
```

> ⚠️ **`cmake -S . -B build` 需要网络**：`CMakeLists.txt` 用 `FetchContent` 拉
> `google/benchmark`，离线环境下 configure 阶段就会失败（仓库里也没有预先缓存的 `build*/`）。
> 只想跑正确性时可以绕过 CMake，直接编译——正确性目标和矩阵乘法对拍都**不**链接 benchmark：
>
> ```bash
> g++ -std=c++17 -O0 -g -fsanitize=address,undefined -I. cpp/main.cpp -o /tmp/hv && /tmp/hv
> g++ -std=c++17 -O2 -I. cpp/matmul_main.cpp -o /tmp/mm && /tmp/mm
> ```
>
> 本文第 9、11 节的所有实测输出都是用这两条命令跑出来的。

环境：g++ 13.3.0、Intel i5-13500HX（10 核 / 20 线程；L1d 480 KiB（10 实例，各 48 KiB）/ L2 12.5 MiB（10 实例，各 1280 KiB）/ L3 24 MiB）、Linux/WSL2。

> ⚠️ **修复记录（历史）**：在第一次核查之前，`Hvector(iterator first, iterator last)` 的 catch 块里
> 有两处笔误——`data[i].~T();`（少了下划线）和 `::operator deleta(data_);`（`delete` 拼错）。
> 后者是**非依赖名的语法错误**，任何 include 这个 header 的翻译单元都会直接编译失败，
> 也就是**整个项目 5 个 target 全部编译不过**。已修正为 `data_[i].~T();` 和 `::operator delete(data_);`。
>
> 这件事的教训不在于拼写，而在于**为什么它能潜伏**：区间构造当时**一个测试都没有**，
> 模板没被实例化，编译器只检查语法、不检查语义，于是两处笔误安安静静地躺在那里。
> 本轮补上了 Test30（正确性 + 回滚路径），这类「不实例化就不检查」的路径从此有覆盖。

> ⚠️ **修复记录（本轮）**：三个运行时 bug 的修复与对应回归测试：
>
> | 编号 | 修复 | 回归测试 |
> | --- | --- | --- |
> | 11.1 别名实参 | `_insert_with_strong_guarantee_impl` 改成**先构造新元素、再搬移旧元素**（`hvector.hpp:534-574`） | `Test27` |
> | 11.2 `erase` 死槽位 | 删掉 `_erase_impl` 里的修补 `catch`，让异常传播（`hvector.hpp:584-611`） | `Test28`、`Test29` |
> | 11.3 非扩容插入泄漏 | `_insert_no_realloc` 改成「构造 + 赋值」，并先把实参落成临时量（`hvector.hpp:460-502`） | `Test26`、`Test29` |
> | 11.4（本轮新发现）`insert` 别名 | 两条路径分别照上面的顺序/临时量处理 | `Test27`（含 `insert` 两种容量） |

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
- **区间构造只接受 `iterator`（也就是 `T*`），不接受其它容器的迭代器**：`iterator` 定义成裸指针，而
  `std::vector<int>::iterator` 是 `__normal_iterator`，**不会**隐式转换成 `int*`，所以
  `Hvector<int> h(vec.begin(), vec.end())` 直接编译失败（`no matching function for call to ...`）。
  要跨容器构造必须传地址：`Hvector<int> h(vec.data(), vec.data() + vec.size())`。`std::vector` 的区间
  构造是个模板，没有这个限制。`Test30` 里就是这么写的。
- **`emplace(idx)` 传 0 个实参编译不过**：`hvector.hpp:410` 那行 `T value(std::forward<Args>(args)...);`
  在 `Args` 为空包时退化成函数声明 `T value();`（most vexing parse），随后 `std::move(value)` 找不到对象。
  `emplace_back()` 传 0 个实参**没有**这个问题（它不走这条路径），`emplace(idx, x)` 也正常。
- **非扩容 `insert` 与 `erase` 要求 `T` 可赋值**（移动赋值，或对「移动构造会抛且可拷贝」的类型退化成
  拷贝赋值），这是 11.3 / 11.2 修复方式带来的约束，见第 5、6 节。这一条与标准其实是一致的
  （标准对 `vector::insert`/`erase` 同样要求 MoveAssignable），但值得在这里点明：只提供移动构造
  而不提供任何赋值运算符的类型，能 `push_back` 但**不能**中间插入或删除。
- `size()` / `capacity()` 同时提供 const 和非 const 重载；非 const 版本是冗余的（不返回引用也不修改状态），但无害。

---

## 4. 内存模型

三个成员：`T* data_`、`size_t size_`、`size_t capacity_`。

- **分配**：`::operator new(cap * sizeof(T))`——只拿原始字节，不构造任何对象。这正是不能改用 `new T[n]` 的原因：`new T[n]` 会对全部 n 个元素做默认构造，而容器在 `reserve` 之后、`push_back` 之前的那些槽位里根本不应该存在对象。对没有默认构造函数的 `T`（如 `std::unique_ptr` 之外的许多类型）它甚至无法编译。
- **构造**：`new (data_ + i) T(args...)`（placement new），只在真正有元素的位置调用。
- **析构**：`data_[i].~T()` 显式调用。`delete[]` 会析构整个缓冲区，而这里只有前 `size_` 个是活的，所以必须手工逐个析构，再 `::operator delete(data_)` 归还裸内存。

分配与构造、析构与释放是分开的两件事——对应 allocator 模型里 `allocate`/`construct` 和 `destroy`/`deallocate` 的区分。

**会构造元素、且可能中途失败的路径都带 `try/catch` 回滚**：4 个构造函数、`_reallocator`、`resize` 两个重载、
`assign`、`shrink_to_fit`、以及 `insert` / `emplace` / `emplace_back` 的**扩容分支**
（统一汇到 `_insert_with_strong_guarantee_impl`）。回滚的记账方式是「计数器 + 只析构真正构造过的槽位」。

**另外两条路径（非扩容 `insert`、`erase`）故意没有 `catch`**——不是漏了，而是它们根本不需要回滚：
它们只做「构造 + 赋值」和「移动赋值 + 析构」，中途抛异常时**没有任何槽位被提前析构**，
`[0, size_)` 里始终全是活对象，异常直接往外传即可（基本保证）。相关推理写在
`hvector.hpp:460-485` 和 `hvector.hpp:594-605` 的注释里，第 6 节有实测。

---

## 5. 扩容策略

**恰好 2 倍**：`emplace_back` / `push_back` / `insert` / `emplace` 在 `size_ == capacity_` 时都汇到
`_insert_with_strong_guarantee` → `_insert_with_strong_guarantee_impl`，那里算
`new_cap = capacity_ == 0 ? 1 : capacity_ * 2`；`reserve(n)` 则按请求的精确值扩容（`n <= capacity_` 时是 no-op）。

初始容量的边界情况：空容器上 `emplace_back` 走同一条路，`capacity_ == 0` 被三元运算符修正为 1。
所以容量序列是 **1 → 2 → 4 → 8 → ...**（gdb 实测见第 9.2 节）。

> 本轮之前的分工不一样：`emplace_back` 当时调的是 `_reallocator(capacity_ * 2)`。
> 现在 **`_reallocator` 只被 `reserve` 使用**——它只做「搬运已有元素」，不负责构造新元素；
> 插入类操作需要「搬运 + 构造一个新元素」，所以走另一条路。这个分工正是 11.1 的修复方式带来的。

2 倍而不是 1.5 倍：摊还代价都是 O(1)，2 倍在 `push_back` 场景下平均拷贝次数更少，代价是最坏情况下最多浪费约 50% 内存（1.5 倍约 33%）。这个实现没有把增长因子做成策略参数，是已知不足之一。

扩容有**两条**路径，差别只在「要不要顺带构造一个新元素」：

**(a) 纯搬运——`_reallocator`（`hvector.hpp:430-452`，供 `reserve` 用）**

1. 先分配新缓冲区；
2. 用 `std::move_if_noexcept` 把旧元素搬到新缓冲区；
3. 全部成功后才析构旧元素、释放旧内存；
4. 任何一步抛异常，析构已构造的新元素、释放新缓冲区、`throw` 重抛，**原容器保持不变**。

**(b) 扩容插入——`_insert_with_strong_guarantee_impl`（`hvector.hpp:534-574`）**

在新缓冲区里按如下顺序构造 `size_ + 1` 个元素，全部成功后才 `_destruct_data()` + `::operator delete(data_)` + 换指针：

1. **先在 `new_data + idx` 构造新元素**；
2. 再把 `[0, idx)` 原样搬到 `[0, idx)`；
3. 最后把 `[idx, size_)` 搬到 `[idx + 1, size_ + 1)`。

**第 1 步排在最前面是这段代码的核心**，见 11.1 与 11.4：实参可能引用容器自己的元素
（`v.push_back(v[0])`、`v.insert(2, v[0])`），先搬移的话被引用的那个对象已经被 move 走了，
新元素会从 moved-from 状态构造出来。libstdc++ 的 `_M_realloc_insert` 用的也是这个顺序，
注释里写得很直白：*"The order of the three operations is dictated by the C++11 case,
where the moves could alter a new element belonging to the existing vector."*

两条路径都用 `move_if_noexcept` 而不是无条件 `std::move`，是因为：移动构造如果抛异常，
被移动过的源对象状态是未指定的，"回滚" 无从谈起，强异常安全保证就没了；而拷贝构造失败时源对象不变，
可以安全回滚。所以只有当 `T` 的移动构造是 `noexcept`（或者 `T` 不可拷贝）时才用移动。

实测验证（`Counter` 的移动构造标记为 `noexcept`，所以一定选移动）：

```
reserve(4) 填满后:  ctor=4 copy=0 move=0 dtor=0
一次 4->8 扩容的 push_back:  ctor=1 copy=0 move=5 dtor=5
size=5 cap=8
作用域结束 dtor=10
```

一次扩容里发生 **5 次移动构造、5 次析构**：「1 次构造新元素 + 4 次搬移旧元素」是 5 次移动构造，
对应的 5 次析构是「1 个临时实参 + 旧缓冲区那 4 个元素」。`ctor=1` 是 `Counter(99)` 这个实参本身。
对比纯搬运的 `reserve`：那里只有 4 次移动、4 次析构，没有那「多出来的 1 次」。

**这段数字在三个版本上是完全一样的**：把同一个探针分别编译到「本轮修复后」「本轮开工时的半修版」
和「最初那版（`_reallocator(capacity_*2)` 扩容后再 placement new 构造新元素）」上，
三次都打印 `ctor=1 copy=0 move=5 dtor=5`。也就是说本轮改的是**构造顺序**，不是开销——
**顺序决定了别名实参读到的是原对象还是 moved-from 的空壳，而移动次数一条都没多。**
本轮真正的新增开销只在非扩容插入那条路径上（多了一个局部临时量，见 11.3）。

另外两个会重新分配缓冲区的接口：

- `shrink_to_fit()`（`hvector.hpp:299-327`）把容量收紧到 `size_`，同样走 `move_if_noexcept`；`size_ == 0` 时直接释放缓冲并把 `data_` 置 `nullptr`。
- `assign(count, val)`（`_assign_impl`，`hvector.hpp:615-642`）分配 `max(count, capacity_)`——**容量够用时保留原有容量**，不会因为 `assign` 而缩容（Test22 覆盖了这一点）。

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
| `Hvector(iterator first, iterator last)` | **强保证** ✓ | 6 元素源、第 3 次拷贝抛异常，`live` 回到源容器元素数、缓冲区已释放、LSan 静默（Test30e） |
| `shrink_to_fit()` | **强保证** ✓ | 抛异常后容量仍为 64、元素仍为 `0 1 2 3` |
| `assign(count, val)` | **强保证** ✓ | 抛异常后 `size=3 cap=4`、元素仍为 `100 101 102` |
| `insert(pos, ...)` **扩容**路径 | **强保证** ✓（见下方脚注） | 抛异常后 `size=4 cap=4`、元素仍为 `0 1 2 3`；满容量下 3 个插入位置 × 5 个抛点共 15 种组合，每一种都整体回滚（Test26f） |
| `emplace_back` / `push_back` 扩容路径 | **强保证** ✓（同脚注） | 同上，走的是同一个 `_insert_with_strong_guarantee_impl` |
| `operator=(const Hvector&)` 需重新分配的分支 | **强保证** ✓ | 抛异常后 `dst` 仍为 `100 101` |
| `operator=(const Hvector&)` 复用已有容量的分支 | 基本保证（逐元素拷贝赋值，中途抛异常则部分元素已被覆盖） | 抛异常后 `dst = [0, 1, 102, 103]`——**状态有效但内容被部分改写** |
| `insert(pos, ...)` / `emplace(pos, ...)` **非扩容**路径 | **基本保证** ✓（本轮修复，见 11.3） | 抛异常后 `size_` 可能已是 5，但 `[0, size_)` 内 5/5 都是活对象、无孤儿，LSan 静默、退出码 0 |
| `erase(...)` | **基本保证** ✓（本轮修复，见 11.2） | 异常正常传播；抛异常后 `size=6` 不变、6/6 槽位存活、无死槽位，之后容器仍可继续用 |
| 别名实参（`push_back(v[0])` / `insert(0, v[2])` …） | 与 `std::vector` 行为一致 ✓（本轮修复，见 11.1 / 11.4） | 6 组用例全部与 `std::vector` 对拍一致（Test27） |

> **脚注（强保证的前提）**：扩容路径的「强保证」是有条件的。若 `T` 的移动构造**会抛**、
> 且 `T` 不可拷贝，`move_if_noexcept` 只能选移动；此时搬移中途抛异常，旧缓冲区里已经有元素被移走，
> 内容无从恢复——容器仍然**有效**（`size_` 不变、无死槽位、无泄漏），但内容未指定，即退化为基本保证。
> 这与 libstdc++ 的行为一致。`Test26` 里的 `Probe` 恰好就是这种类型（不可拷贝），
> 它验证的正是「退化为基本保证时容器依然有效」。
> 想让强保证无条件成立，需要 `T` 满足「移动构造 `noexcept`」或「可拷贝」，这也是标准对
> `vector` 的 `push_back` 强保证给出的同样前提。

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

### 实测记录（本轮修复的两条）

`erase` 抛异常（`Probe` 的移动赋值第 2 次抛），异常**正常传播**且容器保持有效：

```
  before: size=6 live=6
  erase PROPAGATED=1   (0 => 异常被吞掉，调用方无法感知失败)
  after:  size=6 live=6 dead_slots_in_[0,size_)=0   OK
```

注意 `size_` 保持 6 不变（`erase` 失败了，一个元素都没删掉），而 `[0, 6)` 里 6 个槽位全是活对象——
这就是 `erase` 的基本保证。本轮修复**之前**，这里是 `size=6 live=5 dead_slots=1`，
作用域结束时 `~Hvector` 还会对那个死槽位二次析构，`Probe::live` 一路走到 `-1`（见 11.2）。

非扩容 `insert` 抛异常（`Owner` 持有 `unique_ptr<int>`，所以泄漏会被 LSan 抓住）：

```
  before: size=4 cap=16 live=5
  caught 'copy boom', threw=1
  after:  size=5 cap=16   [0,size_) 内活对象=5/5  死槽位=0  live=6
  v 析构后 live=1
  全部作用域结束后 live=0
```

（`LeakSanitizer: 静默` / `exit=0` 由运行器附加。）

`size_` 已经变成 5（多出来的那个元素在搬移之前就构造好并纳入了管理），5 个槽位全是活对象。
`live=6` 比 `size=5` 多出来的那个是探针自己外层作用域里的 `src`：`_insert_no_realloc` 里的局部临时量
在异常传出函数时**已经随栈回滚析构**了，不是孤儿（11.3 有逐次 `tick` 的日志可以确认）。
本轮修复**之前**同样输入的结果是 `LSan: 4 bytes leaked, exit=1`，见 11.3。

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

同一个机理还有一个容易被忽略的受害者：**扩容时被当作实参传进来的引用**。`_insert_impl` 会先把迭代器换算成下标（`_index`，`hvector.hpp:427-429`）再决定要不要扩容，所以 `insert` 在扩容前后用的是**下标**而不是迭代器，迭代器本身不会悬垂；但实参如果是「元素自己的引用」（`v.insert(2, v[0])`），它指向的就是那块要被搬走的内存——这是另一类问题，见 11.1 与 11.4。

**两类问题要分清**：

1. **迭代器/指针/引用的失效**（本节上半部分）——用户拿着一个 `iterator` 或 `T&`，容器扩容后它悬垂了。
   这是用户的责任，规则见上面的表。
2. **实参自引用（aliasing）**——`v.push_back(v[0])`、`v.insert(0, v[2])`。这里用户没有持久化任何东西，
   实参只在这一条语句里有效，标准**明确要求**这种写法合法（`std::vector` 必须支持），
   所以这是**实现的责任**。本实现现在两条插入路径都处理了：
   - 扩容路径：**先构造新元素、再搬移旧元素**（`hvector.hpp:534-574`），被引用的元素在构造新元素时还没被动过；
   - 非扩容路径：**先把实参落成一个局部临时量**（`hvector.hpp:494`），搬移怎么改都读不到它。
     libstdc++ 的 `insert` 非扩容分支也是这么做的，注释是
     *"__x could be an existing element of this vector, so make a copy of it before _M_insert_aux
     moves elements around."*

顺便一提，libstdc++ 只在 `__position != end()` 时才落那个临时量——本实现同样保留了「尾部追加」
的早退分支（`hvector.hpp:488-492`），那条路径根本不搬移任何元素，也就不会有别名问题，
不必付这个代价。

**仍然没处理的一种**：`v.insert(k, std::move(v[j]))`。右值别名连标准本身都没有规定结果
（C++ 里 `v.insert(v.begin(), std::move(v[0]))` 是出了名的未指定行为），本实现给出的是确定但
不保证可移植的答案。不要依赖它。

`insert` 的迭代器重载最终会**先把 `it` 换算成下标**（`_insert_impl` 的第一行 `size_t idx = _index(pos);`，`hvector.hpp:506`）
再决定要不要扩容。这是必须的：如果反过来先扩容、再去解引用那个迭代器，`it` 已经悬垂了。
`erase` 不会重新分配缓冲区，所以迭代器重载直接把指针交给 `_erase_impl`（`hvector.hpp:418-423`），没有这个问题。

移动之后的源对象：`data_ = nullptr, size_ = 0, capacity_ = 0`。它可以被析构、可以被赋值，但**不能假设里面还有元素**——所以 `for (auto& x : std::move(v))` 这类写法是错的，移动之后 `begin() == end() == nullptr`。

---

## 8. 性能对比（`-O2`，无插桩）

> ⏳ **时效性说明**：本节数字采集于**本轮修复之前**的实现（那时 `emplace_back` 走 `_reallocator`）。
> 本轮改动只调整了扩容路径内部的**构造顺序**（新增元素从「换完缓冲区之后构造」变成「在新缓冲区里最先构造」），
> 移动/拷贝/析构的**次数**与内存分配**次数**都没有变，所以这些结论在定性上不受影响；
> 但没有重跑 benchmark，就如实标注。要重新采集请先解决 2 节的离线构建问题。

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

- `PushBack_WithReserve` 明显快于 `PushBack_NoReserve`（4.02 µs vs 5.70 µs，快约 29%），差值就是 14 次扩容（容量 0→1→2→…→8192，共 14 次进入扩容路径，见 9.2 的 gdb 记录）的分配 + 迁移成本。这是"能预知规模就先 `reserve`"最直接的证据。
- `MoveConstruct` 约 3 ns，与 n 无关——移动是三个指针字段的交换，不碰元素。`CopyConstruct` 则严格随 n 线性增长（65536 元素 5.01 µs）。移动语义的价值就在这里。
- `RandomAccess` 这个测试用例是有缺陷的：它用 `next() % n` 生成下标，而 `next` 是 LCG（`seed = seed * 1664525 + 1013904223`），下一次迭代依赖上一次的结果。这条串行依赖链的长度本身就主导了耗时，测出来的更像 LCG 的速度而不是随机访存的速度。要真正测缓存行为应该预生成下标数组再遍历。见 11.5。

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
$ g++ -std=c++17 -O0 -g -fsanitize=address,undefined -I. cpp/main.cpp -o /tmp/hv && /tmp/hv
...
==== Test28: erase 抛异常（异常传播 + 容器保持有效） ====
PASS erase 异常向外传播
PASS 抛异常后容器有效（size=6，无死槽位、无孤儿）
PASS 抛异常后仍可 emplace_back/erase/clear，最终 live=0
PASS 区间 erase 抛异常 -> 容器仍有效

==== Test29: 资源类型非扩容 insert 抛异常（LeakSanitizer） ====
PASS 第 1 次移动赋值抛异常 -> live=5 == size=5（无孤儿、无泄漏）
PASS 第 2 次移动赋值抛异常 -> live=5 == size=5（无孤儿、无泄漏）
PASS 第 3 次移动赋值抛异常 -> live=5 == size=5（无孤儿、无泄漏）
PASS 所有 Owner 均已析构（LeakSanitizer 亦应静默）

==== Test30: 区间构造 Hvector(iterator, iterator) ====
PASS range from vector: range from vector size=5, cap=5 : 1 2 3 4 5 
PASS range size/capacity = 5/5
PASS range from array: range from array size=3, cap=3 : 7 8 9 
PASS range single element: range single element size=1, cap=1 : 7 
PASS range empty (data_==nullptr, cap==0)
PASS range string + deep copy
PASS range from Hvector: range from Hvector size=5, cap=5 : 1 2 3 4 5 
PASS range then grow: range then grow size=6, cap=10 : 1 2 3 4 5 6 
PASS range ctor rollback: live=6 == 源容器元素数
PASS 区间构造回滚无泄漏

All test done
```

**30 组测试全部通过**（72 条 `PASS`、0 条 `FAIL`），**ASan/UBSan/LeakSanitizer 无任何报告**，
进程退出码 0——就这些测试覆盖到的路径而言无泄漏、无越界、无 use-after-free、无 double free、无未定义行为。

`matmul_main` 五种算法对拍参考实现全部 OK：

```
ijk        : OK
ikj        : OK
jik        : OK
blocked    : OK
transposed : OK
```

Valgrind 未安装，功能由 ASan 覆盖（LeakSanitizer 在进程退出时做全量泄漏检查，能力上等价于 `valgrind --leak-check=full`，且对 UAF/越界更敏感）。

> ⚠️ **「测试全绿 + ASan 静默」曾经同时对应着三个真 bug**——这是本项目最值得记住的一条经验。
> 上一轮就是在这个状态下（26 组全过、ASan 全静默）把 11.1/11.2/11.3 记录为「已知不足」的，
> 原因有三层，每一层都值得单独记住：
>
> 1. **三条都落在测试没覆盖的路径上**：别名实参、抛异常的 `erase`、抛异常的非扩容 `insert`。
>    所以只要测试里没有「实参引用容器自身」和「恰好让第 N 次操作抛异常」这两种用例，
>    它们就可以一直不被触发（第 10 节的覆盖盲区表）。
> 2. **ASan 只能看见插桩过的代码**。如果出问题的读发生在未插桩的库代码内部
>    （比如 libstdc++ 的 `std::string` 拷贝构造），ASan 可能**不报**。11.1 用 `std::string`
>    实测时确实是静默的，换成 `int` 实例化才稳定报出来——**不要用「ASan 没报」当证据**。
> 3. **计数器也会骗人**（本轮新学到的）。11.3 那个探针里，`live` 计数在程序结束时回到 **0**，
>    看上去一切正常；而 LeakSanitizer 同时报出「4 字节泄漏」。原因是「一个对象被析构了两次」
>    和「一个对象从未被析构」在计数上**正好抵消**。所以下面这种「用 `live == size` 判断无泄漏」的
>    探针，必须配合 LSan 或能被 `grep` 的 magic 字段一起看：**单一计数器可能刚好平衡，而平衡不代表正确**。

> 本轮为验证「修复真的发生了」而做的一件事：把 `git show HEAD:hpp/hvector.hpp`（修复前的 header）
> 与新的 `cpp/main.cpp` 一起编译，**新测试在修复前必须失败**。实测确实如此——套件在 Test27 处
> `std::abort()`：
>
> ```
> FAIL push_back(v[0]) 扩容: v[2]="" expect="AAA"
> ```
>
> 这正是「回归测试」的门槛：一个在修复前后都通过的测试，什么也没证明。

### 9.2 gdb 记录：扩容时 `data_` 如何变化

断点打在**扩容插入**的入口（`hvector.hpp:537`，即 `new_cap` 算完、`::operator new` 即将执行那一行），
观察每次扩容前的状态。用 9 次 `emplace_back` 的 `Hvector<int>` 触发 1→2→4→8→16：

```gdb
break /root/vector/hpp/hvector.hpp:537
commands
  silent
  printf ">>> grow: old=data_=%p size_=%2lu cap_=%2lu  -->  new_cap=%lu\n", \
         this->data_, this->size_, this->capacity_, new_cap
  continue
end
run
```

输出（完整，不是节选——一共只有 5 次）：

```
>>> grow: old=data_=(nil)              size_= 0 cap_= 0  -->  new_cap=1
>>> grow: old=data_=0x55555556b2b0     size_= 1 cap_= 1  -->  new_cap=2
>>> grow: old=data_=0x55555556b2d0     size_= 2 cap_= 2  -->  new_cap=4
>>> grow: old=data_=0x55555556b2b0     size_= 4 cap_= 4  -->  new_cap=8
>>> grow: old=data_=0x55555556b2f0     size_= 8 cap_= 8  -->  new_cap=16
```

可以读出的三件事：

1. **容量序列是 1 → 2 → 4 → 8 → 16**。第一次 `new_cap=1` 来自三元运算符
   `capacity_ == 0 ? 1 : capacity_ * 2`，`capacity_ == 0` 直接取 1（不再像旧实现那样
   「先算 0、再在函数里修正成 1」）。每次都在 `size_ == capacity_` 时触发，正是扩容的前提。
2. **`data_` 每次都在变**（`(nil)` → `0x...b2b0` → `0x...b2d0` → …），说明扩容是"换一块新内存"，
   而不是原地扩张。注意第 4 行的地址和第 2 行**重复**了（`0x...b2b0`）——那不是 bug，
   是前一轮释放的 8 字节块被分配器回收后**复用**给了新请求。要看"是不是同一块内存"不能只看地址，
   得看生命周期：旧地址在每次换指针之后立刻被 `::operator delete` 归还。
3. 断点位置也说明了本轮的结构变化：**`_reallocator` 上再也打不到这个断点了**（它现在只服务
   `reserve`），插入类操作的扩容全部经过 `_insert_with_strong_guarantee_impl`。

常用调试命令：`p data_` / `p size_` / `p capacity_` 查看状态；`bt` 看调用链
（扩容时链路是 `main → emplace_back → _insert_with_strong_guarantee → _insert_with_strong_guarantee_impl → operator new`，
比旧实现多一层转发，因为 0/1/N 实参要共用一个函数）。

---

## 10. 测试覆盖

`cpp/main.cpp` 共 30 组，全部通过：

- **扩容与访问**（Test1–Test8）：连续 `emplace_back` 自动扩容、拷贝构造、拷贝赋值、自赋值、移动构造、移动赋值、`std::sort` 排序、`at` 越界抛异常。
- **insert**（Test9–Test12）：头部/中间/尾部插入、空容器首插、触发扩容时容量增长、迭代器版本（`begin()` / `begin()+2` / `end()`）、右值移动插入（`std::string`）。
- **erase**（Test13–Test15）：头部/中间/尾部删除、删到空、迭代器版本 + 返回值（含"边遍历边删偶数"）、删空后再插入。
- **资源类型与只移动类型**（Test16–Test17）：`std::string` 的 insert/erase、`std::unique_ptr<int>` 的 insert/erase。
- **越界**（Test18–Test19）：`insert` / `erase` 越界抛 `std::out_of_range`，空容器 `erase(0)` 抛异常。
- **压力**（Test20）：1000 元素批量插入 + 隔一个删一个，校验剩余元素恰为 `1,3,5,...`。
- **新接口**（Test21–Test25）：`shrink_to_fit`（含 `size_==0` 分支必须把 `data_` 置空）、`assign`（容量内 / 扩容 / 别名实参 `v.assign(3, v[2])` / 空 / `std::string`）、`emplace`（空容器 / 头 / 中 / 尾 / 变参 / 越界）、`(count, value)` 填充构造、`initializer_list` 构造（含深拷贝与空列表）。
- **异常安全**（Test26，本轮新增）：非扩容 `insert` / `emplace` 的**抛异常版本**。用带 `magic` 标记与 `live` 计数的 `Probe`（不可拷贝，因此一定走移动）逐个扫描「第 N 次移动抛异常」：第 1 次抛（实参落临时量）→ 容器原样（强保证）；第 2/3/4 次抛（搬移过程中）→ `size_` 可能已 +1，但 `[0, size_)` 内无死槽位、无孤儿（基本保证）。另含 `emplace` 元素构造抛、扩容路径每步抛，以及**满容量下 3 个插入位置 × 5 个抛点 = 15 种组合**对新缓冲区整体回滚的检查。
- **别名实参**（Test27，本轮新增）：`push_back(v[0])` 扩容、`emplace_back(v[1])` 扩容、`insert(size_t(0), v[2])` 非扩容、`insert(size_t(2), v[0])` 扩容，`std::string` 与 `int` 两种实例化，**每一个都和同序列的 `std::vector` 对拍**。这同时补上了 `push_back` 的首次覆盖——此前 `cpp/main.cpp` 从来没有调用过 `push_back`。
- **erase 抛异常**（Test28，本轮新增）：`Probe::arm(2)` 让 `std::move` 的第 2 次移动赋值抛，断言 ①异常**向外传播** ②`size()` 不变且 `[0, size_)` 内无死槽位、无孤儿 ③之后仍可 `emplace_back`/`erase`/`clear` ④最终 `live == 0`。含区间 `erase` 版本。
- **资源类型的非扩容 insert 抛异常**（Test29，本轮新增）：`Owner` 内含 `std::unique_ptr<int>`，`arm_at` 取 1/2/3。这里的关键证据**不是计数器而是 LeakSanitizer**——孤儿对象的 `unique_ptr` 会由 LSan 在进程退出时报出来，比 `live == size` 更难被"计数器刚好平衡"骗过（见 9.1 的经验 3）。
- **区间构造**（Test30，本轮新增，**此前零覆盖**）：来自 `std::vector` 的存储（必须写 `.data()`，见第 3 节）、裸数组、单元素区间、**空区间**（必须 `data_ == nullptr` 且 `capacity() == 0`）、`std::string` 来源 + 深拷贝、来自另一个 `Hvector`、构造后继续扩容；外加 catch 回滚路径——`CopyProbe::arm(3)` 让第 3 次拷贝构造抛，断言 `live` 回到源容器元素数、缓冲区已释放、LSan 静默。

第 11 节的四个 bug **全都是用一次性探针程序（`/tmp/probe_*.cpp`）发现的，当时的测试一组都没抓到**——这不是巧合，见下面的盲区表。

### 覆盖盲区（本轮把 3 行划掉，剩下的仍然存在）

| 未覆盖的东西 | 后果 |
| --- | --- |
| ~~`Hvector(iterator first, iterator last)` 区间构造~~ | **本轮已覆盖**（Test30）。此前零覆盖，catch 块里两处笔误因此能一路潜伏到「整个项目编译不过」才暴露。 |
| ~~实参引用容器自身（`v.push_back(v[0])`）~~ | **本轮已覆盖**（Test27，含 `std::vector` 对拍）。 |
| ~~需要「恰好抛异常」才出现的路径（`erase`、非扩容 `insert`）~~ | **本轮已覆盖**（Test26 / Test28 / Test29）。此前那 25 组（Test1–Test25）跑的全是不抛异常的版本。 |
| 移动构造会抛**且**类型可拷贝时的扩容路径 | 此时 `move_if_noexcept` 会选拷贝、走强保证；`Probe` 因为不可拷贝走的是另一支。要覆盖这一支需要「可拷贝 + 拷贝构造可抛 + 移动构造可抛」的类型，目前没有。 |
| `insert(pos, n, val)` 相关路径 | 无覆盖（该接口也未实现）。 |
| `assign(size_t, T&&)` 的异常路径 | 只测了正常路径（Test22 覆盖别名实参 `v.assign(3, v[2])`）。 |
| `operator=(const Hvector&)` **复用容量**分支的中途抛异常 | 有探针实测（见第 6 节），但没有进 `cpp/main.cpp`。这是第 6 节表里唯一「只有探针、没有套件测试」的一行。 |
| `reverse_iterator` 相关 | typedef 有了，但没有 `rbegin/rend`，也就无从测试。 |
| `max_size()` / `capacity_ * sizeof(T)` 的乘除溢出 | 极端容量下才会出现，需要注入式的分配器才能测，当前没有 allocator 接口。 |
| 右值别名 `v.insert(k, std::move(v[j]))` | 标准本身未规定，本实现有确定行为但不保证；没有测试，也不建议依赖。 |

---

## 11. 四个真 bug：现象、修复前实测、修复、修复后实测

这一节记录四个**运行时** bug（不是「接口不如标准」这类设计取舍，那些在 11.5）。
11.1–11.3 是上一版 README 记录为「已知不足」的三条，本轮修掉；11.4 是修复过程中新发现的第四条。

叙述结构统一为：**现象 → 修复前实测 → 根因 → 修复方式 → 修复后实测 → 代价/遗留**。

> 关于「修复前」的实测记录，需要说清三个不同的版本，本文分别标注：
>
> | 版本 | 11.1 的表现 |
> | --- | --- |
> | **最初版**（上一版 README 记录的那版） | 真 `use-after-free`：先 `_reallocator` 把旧缓冲区 `delete`，再读实参。ASan 报 `heap-use-after-free`。 |
> | **上一轮**（半修版，即本轮开工时的 `HEAD`） | 退化成 `use-after-move`：先搬移 `[0, idx)` 再构造新元素，实参读到的是**已被 move 走**的对象。值错，但缓冲区还活着，ASan 不再报。 |
> | **本轮修复后** | 先构造新元素再搬移，`std::vector` 对拍一致。 |
>
> 最初版的 ASan 报告本 README 保留，但采集自一份**已经不存在的实现**；
> 为了避免留有无法复现的转录，下面给出了用「把 `emplace_back` 改回旧写法」重建的那份实现
> 重新采集的报告（`hpp/hvector.hpp:216-218` 这几行就是那个 bug 的本体）。

### 11.1 `emplace_back` / `push_back` 的别名 use-after-free（本轮已修复）

**现象**：`v.push_back(v[0])`、`v.emplace_back(v[1])` 这类「实参引用容器自己元素」的调用，
在**恰好触发扩容**时得到错误结果。标准明确要求这种写法合法（`std::vector` 必须支持），
所以这不是用户用错了。

**修复前实测**（最初版，`emplace_back` 里先换缓冲区再构造）。
下面这份报告是**重新采集**的：仓库里已经没有那版实现了，所以把当前 header 的 `emplace_back`
改回旧写法放在 `/tmp/old_uaf/` 下编译运行（只有 `emplace_back` 一处不同），报告里的路径因此是
`/tmp/old_uaf/hpp/hvector.hpp`，行号对应那份副本：

```
==84033==ERROR: AddressSanitizer: heap-use-after-free on address 0x502000000030
READ of size 4 at 0x502000000030 thread T0
    #0 void Hvector<int>::emplace_back<int const&>(int const&) /tmp/old_uaf/hpp/hvector.hpp:218
    #1 Hvector<int>::push_back(int const&)                     /tmp/old_uaf/hpp/hvector.hpp:222
    #2 main                                                    /tmp/p_uaf.cpp:10
freed by thread T0 here:
    #0 operator delete(void*)
    #1 Hvector<int>::_reallocator(unsigned long)               /tmp/old_uaf/hpp/hvector.hpp:447
    #2 void Hvector<int>::emplace_back<int const&>(int const&) /tmp/old_uaf/hpp/hvector.hpp:216
previously allocated by thread T0 here:
    #0 operator new(unsigned long)
    #1 Hvector<int>::_reallocator(unsigned long)               /tmp/old_uaf/hpp/hvector.hpp:431
    #2 void Hvector<int>::emplace_back<int>(int&&)             /tmp/old_uaf/hpp/hvector.hpp:216
SUMMARY: AddressSanitizer: heap-use-after-free /tmp/old_uaf/hpp/hvector.hpp:218
```

（省略了 libc / `_start` 等与本案无关的帧、每条栈后面的 `BuildId`、以及 shadow 字节图；
其余逐字保留。`0x502000000030` 这个地址每次重跑都一样，可以对着复现。）

读的是**已经被 `_reallocator` 释放**的那 8 字节缓冲区（`_reallocator` 在 `:447` 把旧缓冲区
`::operator delete`，紧接着 `:218` 又去读它）。

**修复前实测**（上一轮那个半修版，把 `_reallocator` 换成 `_insert_with_strong_guarantee`，
但构造顺序还是「先搬移、后构造」）：

```
  std::vector<string> push_back(v[0])    -> [AAA|BBB|AAA]  OK
  Hvector<string>     push_back(v[0])    -> [AAA|BBB|<empty>]  WRONG (expect [AAA|BBB|AAA])
  Hvector<int>        push_back(v[0])    -> [11|22|11]  OK
  Hvector<string>     emplace_back(v[1]) -> [one|two|three|four|<empty>]  WRONG (expect [one|two|three|four|two])
  Hvector<string>     insert(0,v[2]) 非扩容 -> [B|A|B|C|D]  WRONG (expect [C|A|B|C|D])
  Hvector<string>     insert(2,v[0]) 扩容   -> [A|B|<empty>|C|D]  WRONG (expect [A|B|A|C|D])
```

注意这一版已经**没有 UAF 了**（旧缓冲区还活着，ASan 静默），但结果仍然是错的——
`<empty>` 就是 `std::string` 被 move 之后留下的空壳。**「ASan 不报」和「结果正确」是两件事。**

**根因**：构造新元素时，实参指向的那个对象已经被 `move_if_noexcept` 搬走过一次。
`std::string` 的移动构造是 `noexcept`，所以 `move_if_noexcept` 一定选移动，
移动之后源串只剩一个空壳，新元素于是从**空壳**构造出来。
`int` 看不出问题，因为 `int` 的「移动」是拷贝、源对象不变——这也正好解释了上一版 README 里
「要用 `int` 才能稳定触发 ASan」那个现象。

**修复方式**：交换构造顺序——**先构造新元素，再搬移旧元素**（`hvector.hpp:534-574`）：

```cpp
try {
    new (new_data + idx) T(std::forward<Args>(args)...);   // ① 先构造新元素：旧缓冲区一个字节都没动
    mid_done = true;
    for (; head_done < idx; ++head_done) {                 // ② 搬 [0, idx)
        new (new_data + head_done) T(std::move_if_noexcept(data_[head_done]));
    }
    for (; tail_done < size_ - idx; ++tail_done) {         // ③ 搬 [idx, size_) -> [idx+1, size_+1)
        new (new_data + idx + 1 + tail_done) T(std::move_if_noexcept(data_[idx + tail_done]));
    }
} catch (...) { /* 只析构真正构造过的三块：{idx} ∪ [0,head) ∪ [idx+1, idx+1+tail) */ }
```

顺序不是随便定的，libstdc++ 的 `_M_realloc_insert` 用的是同一个顺序，注释写得很直白：

> *"The order of the three operations is dictated by the C++11 case, where the moves could alter
> a new element belonging to the existing vector."*

**代价**：没有额外开销。移动/拷贝/析构的次数与旧实现完全相同，只是次序变了。
代价落在 **catch 的记账**上：已构造的槽位不再是「一段前缀」，而是
`{idx} ∪ [0, head_done) ∪ [idx + 1, idx + 1 + tail_done)` 三块，必须用三个计数器分别记录
（旧实现的单个 `constructed` 计数在这里会**漏析构一个槽位**）。这套记账由 Test26 的
「满容量 × 3 个插入位置 × 5 个抛点 = 15 种组合」逐一验证。

顺带清理：原来 `_insert_with_strong_guarantee_impl` / `_insert_with_strong_guarantee`
各有**两个函数体一字不差的重复重载**（一个 `template<class U>`、一个 `template<class... Args>`），
本轮合并成一个 variadic 版本（重载决议本来就让单参调用走那个更特化的版本，合并是等价的）。
留着两份副本的真实风险正是上一轮「只修一半」的成因。

**修复后实测**（完整探针输出，与 `std::vector` 对拍）：

```
=== (1) 别名：实参引用容器自己的元素 ===
  std::vector<string> push_back(v[0])    -> [AAA|BBB|AAA]  OK
  Hvector<string>     push_back(v[0])    -> [AAA|BBB|AAA]  OK
  Hvector<int>        push_back(v[0])    -> [11|22|11]  OK
  Hvector<string>     emplace_back(v[1]) -> [one|two|three|four|two]  OK
  Hvector<string>     insert(0,v[2]) 非扩容 -> [C|A|B|C|D]  OK
  Hvector<string>     insert(2,v[0]) 扩容   -> [A|B|A|C|D]  OK
```

回归测试：**Test27**（6 组用例，`std::string` + `int`，每组与 `std::vector` 对拍）。

**遗留**：强保证的**前提**仍然是「`T` 的移动构造 `noexcept`」或「`T` 可拷贝」，
见第 6 节的脚注。这与 libstdc++ 的处境相同，不是本实现特有的退让。

### 11.2 `erase` 的修补 `catch`：静默吞异常 → 死槽位 + 二次析构（本轮已修复）

**现象**：`T` 的移动赋值在 `std::move(last, end(), first)` 中途抛异常时，`erase` 的行为曾经错得很远，
而且**连错法都变过一次**——这是一个「修 bug 反而变糟」的少见例子，值得按版本分开看。

**最初版**：`catch(...)` 做状态修补，但**结尾没有 `throw;`**：

```cpp
try {
    std::move(last, end(), first);
    for(size_t i = new_size; i < size_; ++i) data_[i].~T();
    size_ = new_size;
} catch(...) {
    size_t p = fidx, q = lidx;          // 修补：析构 data_[p]，再从 data_[q] uninitialized_move 重建
    while(q < size_) { /* ... */ }
    for(size_t i = p; i < size_; ++i) data_[i].~T();
}                                       // ← 没有 throw;
size_ = new_size;
return data_ + fidx;
```

于是调用方**完全无法感知失败**：`catch` 把异常吃掉、修补一遍、然后当作成功返回。

> 这部分实测数据来自**按上面结构重建**的那版实现（原实现已不在仓库里）。
> 用重建版实测的结果是 `PROPAGATED=0`、`size=5 live=5 dead_slots=0`，内容甚至**恰好是对的**
> （`1 2 3 4 5`）——这恰恰是它最阴险的地方：**内容对，不代表契约对**。
> `erase` 失败了、一个元素都没删掉，但调用方拿到的是「成功」的返回值。
> （上一版 README 在这里记录的实测内容是 `2 2 3 4 5`，与重建版不同；由于原实现已不可考，
> 本节只把**重建版实测到的**内容写出来，其余保留机制层面的描述。）

**上一轮（半修版，即本轮开工时的 `HEAD`）**：给那个 `catch` 补上了 `throw;`。
补完之后异常确实往外传了，但**修补代码还在**，于是变成了更严重的问题——
修补仍然会析构槽位、重建对象，而 `throw;` 让 `size_ = new_size`（在 `catch` 之后）**永远不会执行**：

```
=== (2) erase 抛异常（移动赋值第 2 次抛） ===
  before: size=6 live=6
  erase PROPAGATED=1   (0 => 异常被吞掉，调用方无法感知失败)
  after:  size=6 live=5 dead_slots_in_[0,size_)=1   <-- 无效状态

=== 探针结束：Owner::live=0 Probe::live=-1 ===
```

（这一版已经能被 `dead_slots_in_[0,size_)` 这个探针断言抓到，但要看到「`Probe::live` 变成 **-1**」
必须把输出打印到末尾——`-1` 出现在探针最后一行，前面几行看起来都还"正常"。）

`Probe::live` 一路走到 **`-1`**，这不是「泄漏」而是**二次析构**的签名：
`size_` 仍是 6，但槽位 `[5]` 在修补里已经被析构过一次，作用域结束时 `~Hvector()` 又析构一遍。
**「只补一个 `throw;`」在这里是把一个契约问题升级成了内存安全问题。**

**根因**：那个修补过程是给更早的「移动一个、析构一个」的 `_shift_elements_backward` 写的；
当非扩容插入改成「构造 + 赋值」之后（见 11.3），`erase` 的搬移过程中**已经没有任何槽位被提前析构**，
修补本身成了纯粹的破坏——它把 `size_` 之外的槽位析构掉，而 `size_` 因为异常要继续传播并没有跟着改小。

**修复方式**：**删掉整个 `try/catch`**，让异常直接传播（`hvector.hpp:584-611`）。
`std::move` 的三参版本是逐个元素的**移动赋值**，它不构造也不析构任何对象，所以：

- 抛异常时 `size_` 保持原值、`[0, size_)` 内所有槽位仍是活对象 → **有效状态**（内容未指定）；
- 这正是标准给 `erase` 的**基本保证**，不需要任何修补。

原地留了一段注释说明「为什么这里故意没有 `catch`」，以免后人再把手写修补加回来：

```cpp
size_t new_size = size_ - (lidx - fidx);

// 这里**故意没有** try/catch。
// std::move 的三参版本是逐个元素的【移动赋值】，它不析构任何对象、也不构造任何对象，
// 所以中途抛异常时：所有 [0, size_) 的槽位仍然都是活对象，size_ 也还是原值……
// 一句话：内容对不代表状态有效，别在 catch 里做没必要的修补。
std::move(last, end(), first);
for(size_t i = new_size; i < size_; ++i) { data_[i].~T(); }
size_ = new_size;
```

**修复后实测**（同一个探针，`Probe::arm(2)`）：

```
  before: size=6 live=6
  erase PROPAGATED=1   (0 => 异常被吞掉，调用方无法感知失败)
  after:  size=6 live=6 dead_slots_in_[0,size_)=0   OK

=== 探针结束：Owner::live=0 Probe::live=0 ===
```

`size=6 live=6 dead_slots=0`，并且探针结束时 `Probe::live=0`（不再有 `-1`）。
回归测试：**Test28**（单元素 `erase` + 区间 `erase`，断言异常传播、无死槽位、无孤儿、之后仍可继续使用）。

**行为变化（需要记住的一点）**：`erase` 现在**会抛异常**，而最初版不会。
如果有代码依赖「`erase` 从不抛」，那是一处需要跟着改的调用契约——但依赖一个静默吞掉失败的
`erase` 本来就是错的。

### 11.3 `insert` / `emplace` 非扩容路径：抛异常时泄漏 + 死槽位（本轮已修复）

**现象**：容量够用时的中间/头部插入（**最常见的情况**）走 `_insert_no_realloc` 的旧实现
`_shift_elements_backward`——从后往前逐个「`uninitialized_move` 构造一个新元素，然后析构源槽位」。
一旦某一步抛异常，会同时留下两个烂摊子：

- **死槽位**：某个已经在 `[0, size_)` 之内的槽位被提前析构了，但容器仍声称它有活对象 → 访问即 UB，析构时**二次析构**；
- **孤儿对象**：已经构造好、却落在 `size_` 之外的对象，`~Hvector()` 只析构 `[0, size_)`，这个对象**永远不会被析构**——对持有资源的类型就是**真泄漏**。

**修复前实测**（重建版，把 `_insert_no_realloc` 换回 `_shift_elements_backward` 的写法放在 `/tmp/old_shift/`，
`Owner` 持有 `unique_ptr<int>`，`reserve(16)` 后 4 个元素，`insert(size_t(1), src)`，
让第 3 次拷贝构造抛异常——前两次消耗在搬移里）：

```
  before: size=4 cap=16 live=5
  caught 'copy boom', threw=1
  after:  size=4 cap=16   [0,size_) 内活对象=4/4  死槽位=0  live=5
  v 析构后 live=1
  全部作用域结束后 live=0

ERROR: LeakSanitizer: detected memory leaks
Direct leak of 4 byte(s) in 1 object(s) allocated from:
    #0 operator new(unsigned long)
    #1 Owner::Owner(Owner const&)                                  /tmp/p_shift_old.cpp:18
    #2 void Hvector<Owner>::_insert_no_realloc<Owner const&>(...) /tmp/old_shift/hpp/hvector.hpp:496
    #3 Owner* Hvector<Owner>::_insert_impl<Owner const&>(...)     /tmp/old_shift/hpp/hvector.hpp:512
    #4 Hvector<Owner>::insert(unsigned long, Owner const&)        /tmp/old_shift/hpp/hvector.hpp:386
    #5 main                                                        /tmp/p_shift_old.cpp:39
SUMMARY: AddressSanitizer: 4 byte(s) leaked in 1 allocation(s)
进程退出码 = 1
```

（`进程退出码` 由运行器附加，其余是探针进程自己的输出；报告里的行号属于 `/tmp/old_shift/`
下那份重建副本，不是仓库里的 `hpp/hvector.hpp`。）

这里有两个**特别值得看**的地方：

1. **`live` 在作用域结束后回到 0，而 LSan 同时报出泄漏。** 看上去矛盾，其实是
   「一个对象被析构了两次」和「一个对象从未被析构」在计数器上**正好相互抵消**。
   **单一计数器可能刚好平衡，而平衡不代表正确**——这是本项目最容易踩的一个认知陷阱，
   只有 LeakSanitizer（或能 `grep` 的 magic 字段）才能戳破它。
2. **死槽位这次没被 `magic` 字段抓到**（`死槽位=0` 是假象）。因为抛异常的那次拷贝构造**已经先把
   `magic` 写成了 `ALIVE`**、然后才在函数体里抛——槽位被析构过，但内存里的 magic 又变回了"活着"。
   而在这个「移动一个、析构一个」的循环里，**被析构的槽位恰好总是下一次要构造的那个槽位**
   （构造 `data_[i]` 之后才析构 `data_[i-1]`，于是失败的那次构造正好落在刚被析构的槽位上），
   所以 magic 在这里的系统性失效不是巧合。
   （上一版 README 记录过一份「`[3]=DESTROYED`、`dead_slots=1`」的转录，即那个探针确实抓到过死槽位；
   本轮按同结构重建后没能复现出 magic 命中的情形。两种结果取决于抛点与 magic 写入的先后，
   本文只把**本轮复现到的**写出来——结论不变：**magic 与 LSan 是互补的证据，不能只信一个**。）

**根因**：「移动一个、析构一个」把「搬移」和「销毁」耦合在了一个循环里。搬移是**可失败**的操作
（移动/拷贝构造、赋值都可能抛），销毁是**不可逆**的操作，两者交错的中间态必然既不是「全部活着」
也不是「全部已析构」。正确做法是让这两件事彻底分开。

**修复方式**：改成「构造 + 赋值」，并把实参先落成临时量（`hvector.hpp:487-502`）：

```cpp
template<typename U>
void _insert_no_realloc(size_t idx, U&& val) {
    if(idx == size_) {                        // 尾部追加：根本不搬移，也不会有别名问题
        new (data_ + size_) T(std::forward<U>(val));
        ++size_;
        return;
    }

    T value(std::forward<U>(val));            // ① 先把实参固化（别名安全，见 11.4）

    new (data_ + size_) T(std::move_if_noexcept(data_[size_ - 1]));   // ② 在末尾构造多出来的那个
    ++size_;                                                          // ③ 立刻纳入管理
    for(size_t i = size_ - 2; i > idx; --i) {                         // ④ 从后往前移动赋值
        data_[i] = std::move_if_noexcept(data_[i - 1]);
    }
    data_[idx] = std::move(value);                                    // ⑤ 写入 idx
}
```

关键在于 **②③ 的次序**：先把多出来的元素构造在**末尾的备用槽位**上，然后**立刻 `++size_`**
把它纳入管理。这样在 ④⑤ 的搬移过程中，`[0, size_)` **始终全是活对象**，中途抛异常也不会出现
「死槽位」或「孤儿」——异常保证从「连基本保证都没有」变成了**基本保证**：

- ① 或 ② 抛 → 容器一个字节都没动（**强保证**，比旧实现还强一点）；
- ④ 或 ⑤ 抛 → `size_` 可能已经 +1，但所有槽位都活着、无孤儿、无泄漏（**基本保证**）。

**修复后实测**（同一个探针，`-I/root/vector`）：

```
  before: size=4 cap=16 live=5
  caught 'copy boom', threw=1
  after:  size=5 cap=16   [0,size_) 内活对象=5/5  死槽位=0  live=6
  v 析构后 live=1
  全部作用域结束后 live=0
```

（后两行由运行器附加：LeakSanitizer 静默、进程退出码 0。）

`size_` 已经变成 5、5/5 槽位全是活对象、无泄漏、退出码 0。
这里的 `live=6` **不是**「`size_` 之外多了一个对象」：探针在外层作用域还留着 `src`（`Owner src(99)`），
6 = `src` + `[0, size_)` 里的 5 个。把 `tick` 打上日志就能看清这 6 是怎么来的
（在 `Owner` 的拷贝构造/拷贝赋值里各打一行、并打印 `live`）：

```
    tick#1 (copy-ctor)   live=5     ① 构造局部临时量 value
    tick#2 (copy-ctor)   live=6     ② 在末尾备用槽位构造 data_[4]（此时已 ++size_）
    tick#3 (copy-assign) live=7     ④ 搬移中的第一次赋值 -> 抛
  threw=1  after: size=5 live=6     value 随栈回滚被析构，7 -> 6
```

两处都值得注意：一是**抛点落在了 ④「搬移」而不是 ①「固化实参」**，此时容器已经是 `size_=5`、
全部槽位存活的状态；二是那个局部临时量在异常离开 `_insert_no_realloc` 时**已经析构**（7→6），
所以它既不是孤儿、也不该被算进「多出来的活对象」里——这正是下面 11.3 修复前那版
`live=5 / size=4` 容易让人误判的地方：**两个版本都会出现 `live == size_ + 1`，
一个是对的、一个是错的，光看计数器的差值分不出来。**

回归测试：**Test26**（非扩容 `insert`/`emplace` 的每一步抛异常）与 **Test29**
（资源类型 + LeakSanitizer）。Test26 的 `Probe` 不可拷贝，所以一定走移动，能精确地
「让第 N 次移动抛异常」。

**代价**：每次容量富余的中间/头部插入**多一次移动构造 + 一次移动赋值**（①⑤ 这两步）。
尾部追加走早退分支，不付这个代价；`int`、`std::string`、`unique_ptr` 上都是常数级开销。
另一个代价是**类型要求变严**：这条路径现在要求 `T` **可赋值**（对只移类型是移动赋值，
对「移动构造会抛且可拷贝」的类型退化成拷贝赋值），而旧实现的 `uninitialized_move` + 显式析构
只要求可移动构造。这一条与标准是一致的（标准对 `vector::insert` / `erase` 同样要求 MoveAssignable），
但确实会让「可构造但不可赋值」的类型少支持一个操作，见第 3 节。

### 11.4 `insert` 的实参自引用（本轮修复 11.1 时新发现）

**现象**：11.1 修到一半时发现——**同一个** `insert` 调用，会因为「容量够不够」而给出**两种不同的答案**。
两个路径都有问题：

```
  Hvector<string> insert(0,v[2]) 非扩容 -> [B|A|B|C|D]  WRONG (expect [C|A|B|C|D])
  Hvector<string> insert(2,v[0]) 扩容   -> [A|B|<empty>|C|D]  WRONG (expect [A|B|A|C|D])
```

非扩容那行尤其能说明问题：容器是 `{A,B,C,D}`、容量富余，`insert(0, v[2])` 期望把 `C` 插到头部。
实测第一个位置是 `B`——因为搬移 `[0, size_)` 整体右移一格的过程中，`v[2]` 那个位置**先被写成了 `B`**，
等到真正构造新元素时读到的已经是覆盖后的值。扩容那行则是 `<empty>`，即 11.1 的同一种
「读到已经 move 走的空壳」。

**根因**：两条路径都把「读取实参」和「搬移元素」这两件事排错了顺序。

**修复方式**（两处，互相对应）：

| 路径 | 修法 | 与 libstdc++ 的对应 |
| --- | --- | --- |
| 扩容 | **先构造新元素、再搬移旧元素** | `_M_realloc_insert` 的顺序注释 |
| 非扩容 | **先把实参落成一个局部临时量** `T value(std::forward<U>(val));`，搬移结束后再写入 | `insert(const_iterator, const value_type&)` 里的 `_Temporary_value __x_copy(this, __x);`，注释是 *"__x could be an existing element of this vector, so make a copy of it before _M_insert_aux moves elements around."* |

libstdc++ 只在 `__position != end()` 时才落那个临时量；本实现同样保留了「尾部追加」的早退分支
（`hvector.hpp:488-492`）——那条路径不搬移任何元素，本身就没有别名问题。

**修复后实测**：11.1 节末尾那份完整探针输出的第 5、6 行
（`insert(0,v[2]) 非扩容 -> [C|A|B|C|D] OK`、`insert(2,v[0]) 扩容 -> [A|B|A|C|D] OK`）。

回归测试：**Test27**（含 `insert` 的两种容量情形；每个用例都与 `std::vector` 对拍）。

**遗留**：右值别名 `v.insert(k, std::move(v[j]))` 仍然不做保证——标准本身也没有规定它。
这一条列在 11.5。

### 11.5 其它（设计取舍与尚未做的）

- **没有 allocator 支持**。分配走 `::operator new` 硬编码，无法定制内存来源，也没有 `get_allocator()`。改成 `std::allocator_traits<Alloc>` 是下一步。
- **`capacity_` 的溢出检查只做了一半**：`_insert_with_strong_guarantee_impl` 里已经用
  `new_cap = capacity_ == 0 ? 1 : capacity_ * 2` 取代了旧实现「先算 0 再修正成 1」的写法，
  但 `capacity_ * 2` 在接近 `SIZE_MAX/2` 时仍会回绕，`new_cap * sizeof(T)` 这个乘法也可能溢出，
  两者都**没有**处理，也没有 `max_size()`。
- **右值别名不做保证**：`v.insert(k, std::move(v[j]))` 的结果是本实现的确定行为，但标准未规定，
  不要依赖。
- **magic 标记型探针有盲区**：见 11.3——如果抛异常的那个构造函数已经把 magic 写好才抛，
  死槽位就检测不到。写这类探针时要配合 LeakSanitizer 一起看。
- **非扩容 `insert` / `erase` 要求 `T` 可赋值**（11.2/11.3 修复的代价），只提供移动构造、
  不提供任何赋值运算符的类型能 `push_back` 但不能中间插入或删除（与标准一致，但会让人意外）。
- **`operator=(const Hvector&)` 的复用分支只有基本保证**（6 节实测），而重新分配的分支是强保证。要么统一成 copy-and-swap 拿强保证，要么接受这个不对称。
- **接口不完整**（见第 3 节的完整列表），尤其缺比较运算符和 `rbegin/rend`。
- **不符合 STL 容器的形式化要求**：`emplace_back` 返回 `void`、`insert` 下标重载与迭代器重载有歧义、`front`/`back`/`pop_back` 越界抛异常而非 UB。不能直接当作 `std::vector` 的替代品。
- **增长因子写死 2 倍**，没有做成策略。
- **`RandomAccess` 基准用例设计有缺陷**（见 8.1），测的是 LCG 依赖链而非访存。
- **分块矩阵乘法分错了对象**，对已经最优的 `ikj` 分块，收益为负；应该对 `ijk` 分块。
- **矩阵乘法没有做向量化**：没有 `-march=native`、没有显式 SIMD、没有 OpenMP。`ikj` 的 9.85 GFLOPS 是标量 `-O2` 的结果，离单核理论上限还有很大空间。
- **`size()` / `capacity()` 的非 const 重载是冗余的**，没有存在价值但也不会出错。

### 已修复汇总

**本轮修复的四个 bug**（第 11 节上面有完整的前后实测）：

| 编号 | 问题 | 修复方式 | 回归测试 |
| --- | --- | --- | --- |
| 11.1 | `emplace_back` / `push_back` 扩容时别名实参 `use-after-free` → 退化成「读到 move 走的空壳」 | 扩容路径改为**先构造新元素、再搬移旧元素**（`hvector.hpp:534-574`）；顺带把两对重复重载合并成一个 variadic 版本 | Test27 |
| 11.2 | `erase` 的修补 `catch`：先静默吞异常，补 `throw;` 后又留下死槽位 + 二次析构 | **删掉整个 `try/catch`**（`hvector.hpp:584-611`），让异常传播、`size_` 不变、所有槽位存活 | Test28 |
| 11.3 | 非扩容 `insert` / `emplace` 抛异常时留下死槽位与孤儿对象（对持有资源的类型是真泄漏，LeakSanitizer 直接报出来） | 改成「先在末尾备用槽位构造 + 立刻 `++size_` + 再用移动赋值搬移」（`hvector.hpp:487-502`） | Test26、Test29 |
| 11.4 | `insert` 的实参自引用（本轮新发现，两条路径都错） | 扩容路径同 11.1；非扩容路径先把实参落成局部临时量 | Test27 |

**更早轮次修复的问题**（保留记录）：

| 编号 | 问题 | 修复方式 | 实测验证 |
| --- | --- | --- | --- |
| 早期 1 | 拷贝构造函数异常时泄漏缓冲区 | `hvector.hpp:107-123` 加了 `constructed` 计数 + `try/catch`，catch 里析构已构造元素、`::operator delete`、重抛 | 6 元素源容器在第 3 次拷贝时抛异常，作用域结束后 `live=0`；LeakSanitizer 静默，退出码 0 |
| 早期 2 | `Hvector(size_t)` 异常时泄漏，且 `size_` 已提前写成 `cap` | `hvector.hpp:30-44` 同样加回滚 | 默认构造在第 3 个元素抛异常后 `live=0`，无泄漏 |
| 早期 3 | `resize(n, val)` 条件写成 `new_size > size_`，容量够用时仍翻倍扩容 | 条件改为 `new_size > capacity_`，与单参版一致（`hvector.hpp:268`） | `cap=64` 时 `resize(5,val)` / `resize(8,val)` 后容量仍是 64（旧行为会变成 128） |
| 早期 4 | `Hvector(iterator, iterator)` 的 `data[i]` / `::operator deleta` 两处笔误导致整个项目编译不过 | 改为 `data_[i]` / `::operator delete` | 修复前 5 个 target 全部编译失败，修复后全部通过；本轮补上 Test30 让它不会再无覆盖地潜伏 |

---

## 12. 自测问题

1. **为什么不能用 `new T[n]` 作底层存储？** 它会对全部 n 个元素做默认构造。`reserve` 之后到 `push_back` 之前的槽位里不该有对象；对没有默认构造函数的 `T` 直接编译失败；而且 `delete[]` 会析构整个缓冲区，而只有前 `size_` 个是活的。
2. **`allocate` 和 `construct` 的区别？** `allocate` 只拿原始内存、不构造对象、可以用 `T*` 算术但不能解引用；`construct` 在已分配的内存上用 placement new 构造对象。分开是为了让容器能在"容量够但元素不够"的状态下存在，也是 `std::vector<bool>` 之类特化能工作的前提。对应到本实现就是 `::operator new` 与 placement new。
3. **为什么析构要显式调 `T::~T()`？** 因为缓冲区里只有 `size_` 个活对象，而 `::operator delete` 不调用任何析构函数（`delete[]` 才会，但会析构全部 `capacity_` 个）。
4. **`reserve` 后 `size` 和 `capacity` 怎么变？** `size` 不变；`capacity >= n`（本实现是恰好 `n`）。不构造任何元素。
5. **`resize` 增大时新元素如何初始化？** 单参版值初始化（`T()`，对 POD 是零初始化），双参版用给定值拷贝构造。
6. **扩容时旧元素怎么迁移？** `std::move_if_noexcept`——移动构造 `noexcept`（或不可拷贝）时移动，否则拷贝。实测 `Counter` 走的是移动（一次 4→8 扩容：`ctor=1 copy=0 move=5 dtor=5`，其中 5 次移动 = 1 次构造新元素 + 4 次搬移旧元素）。
7. **为什么移动构造要尽量 `noexcept`？** 否则容器在扩容时会退化成拷贝（丢失性能），而且移动中途抛异常时源对象状态未指定、无法回滚，强异常安全保证无法维持。11.3 曾是这句话的反面教材（`_shift_elements_backward` 里移动抛异常时容器直接进入无效状态）；本轮修掉 11.3 用的正是这句话的**正面**应用：把「移动 + 析构」换成「构造 + 赋值」，让源槽位在搬移全程都是活对象，于是「移动抛异常」这件不可避免的事不再产生无效状态。
8. **拷贝赋值为什么推荐 copy-and-swap？** 一次拷贝构造 + 一次 `swap` 就天然获得强异常安全、自动处理自赋值、且不需要分别写"容量够"和"容量不够"两条路径。本实现为了省这次分配手写了两条分支——现在两条分支**都**保证了不泄漏，但复用容量那条只有基本保证（6 节实测 `dst` 被部分改写）。手写多分支的代价就在这儿：**每条分支都要自己保证正确性，而人总会漏掉一条**。
   本轮给这句话添了一个更贴切的例子：11.2 的那个 `catch` **本来是想做"状态修补"**（它把析构掉的槽位重建回来、还顺手把内容也拼对了），结果它的前提在 11.3 的修复之后已经失效，修补变成了破坏——`erase` 的死槽位就是这么来的。**主动修补比不修补更危险**：不修补至少状态是可推理的，错误只在调用方看不到异常；而一个前提过时的修补，会在下次别的改动（比如补一个 `throw;`）时突然变成内存安全问题。
9. **哪些操作导致迭代器失效？** 见第 7 节。核心是：任何触发重新分配的操作让**全部**失效；`insert`/`erase` 从操作点向后失效；`end()` 在几乎所有修改操作后都失效。
10. **`Hvector<std::unique_ptr<int>>` 为什么不能拷贝？** 拷贝构造会实例化 `T(other[i])`，而 `unique_ptr` 的拷贝构造是 `= delete`，模板实例化失败。这也正是 `std::move_if_noexcept` 里"不可拷贝则移动"分支存在的原因——`Test17` 验证了移动路径能正常工作。
11. **为什么 `emplace_back` 里"先扩容、后构造"是危险的？** 见 11.1 与 11.4。扩容会 `::operator delete` 掉旧缓冲区，
    而 `std::forward<Args>(args)...` 里如果有一个实参引用着旧缓冲区里的元素，构造新元素时读到的就是已释放内存。
    标准明确要求 `v.push_back(v[0])` 必须合法，所以这不是"用户用错了"，而是实现必须处理的情况。
    现在的做法是**"先把新元素在新缓冲区里构造好、再把旧元素搬过去、全部成功后才拆旧缓冲区"**
    （`hvector.hpp:534-574`），顺序与 libstdc++ 的 `_M_realloc_insert` 一致；非扩容那条路径则
    **先把实参落成局部临时量**。两者都修好之后，`insert` 的答案不再随容量变化而变化。
12. **栈、堆、缓存和 vector 的关系？** 容器对象本身（`T* data_` + 两个 `size_t`，共 24 字节）在栈上，元素全在堆上，两者通过 `data_` 这一个指针联系起来。元素在堆上连续存放，所以顺序遍历时硬件预取器能持续命中，这是 `SequentialRead` 比 `RandomAccess` 快约一个数量级的原因（8.12 µs vs 93.11 µs @ 65536）。`reserve` 的价值在于减少堆分配次数（13 次 → 1 次），`ikj` 的价值在于让堆上那 n² 个 double 被按缓存行而不是按 n×8 字节的步长访问。
13. **为什么矩阵乘法 `ikj` 比 `ijk` 缓存友好？** 见 8.2，实测差 11.5 倍。`ikj` 最内层连续访问两个数组，`ijk` 最内层以 `n*8` 字节为步长跳跃访问 `B`。
14. **为什么 `erase` 反而**不**写 `try/catch`？** 见 11.2。`std::move(last, end(), first)` 的三参版本是逐个元素的**移动赋值**——它不构造任何对象、也不析构任何对象。所以中途抛异常时 `size_` 保持原值、`[0, size_)` 内所有槽位都还是活对象，容器处于「有效但内容未指定」的状态，异常直接往外传就得到标准要求的基本保证。这里**任何**手写的回滚/修补都只会帮倒忙：回滚要「恢复原内容」就得先把已被覆盖的元素存出来（额外开销），而修补如果前提过时（11.2 就是活例子）会直接制造死槽位。**能靠「不写代码」拿到的保证，就不要写代码。**
15. **为什么非扩容 `insert` 要先落一个临时量？** 见 11.4。搬移 `[idx, size_)` 的过程中，被实参引用的那个元素会**先被覆盖掉**：`v.insert(0, v[2])` 在容量富余时会得到 `{B,A,B,C,D}` 而不是 `{C,A,B,C,D}`——同一个调用因为容量够不够给出不同答案。临时量把「读取实参」和「搬移元素」这两件事彻底解耦：搬移怎么改都读不到它了。libstdc++ 的 `insert` 也这么做，注释原文是 *"__x could be an existing element of this vector, so make a copy of it before _M_insert_aux moves elements around."*。代价是每次中间插入多一次移动，而**尾部追加**走早退分支、不付这个代价——libstdc++ 也只在 `__position != end()` 时才落临时量，两边是同一个取舍。

---

## 13. 下一步

上一轮的待办 1–4（修 11.1 / 11.3 / 11.2 + 补测试）**已在本轮完成**，见第 11 节。剩下的按优先级：

1. **修 11.4/11.2 的测试缺口**：11.4 的右值别名仍无保证，`operator=(const Hvector&)` 复用容量分支的
   抛异常路径仍只有探针、没有进套件（第 10 节盲区表最后几行）。补上它们，并给「移动构造会抛**且**可拷贝」
   的类型补一个走拷贝回退的扩容异常用例——那是第 6 节脚注里唯一没被 Test26 覆盖的分支。
2. 加比较运算符、`rbegin/rend`、`insert(pos, n, val)`，把 `emplace_back` 的返回值对齐标准
   （顺带修 `emplace(idx)` 零实参编译不过的问题，第 3 节）。
3. 改成 `std::allocator_traits` 版本，实现原书 V3 的 allocator 目标；用自定义 allocator 顺便把
   `max_size()` 与 `new_cap * sizeof(T)` 的溢出检查做成可测试的（现在这两种溢出都只能靠读代码发现）。
4. 把区间构造改成迭代器**模板**（现在是 `iterator = T*`，传别的容器的迭代器不编译，第 3 节），
   并给 `Hvector(const Hvector&)` 之外的重载加 `std::enable_if` 约束，避免和 `(size_t, const T&)`
   之类的重载打架。
5. 给 `operator=(const Hvector&)` 统一成 copy-and-swap，或至少让两条分支的保证一致（第 6 节）。
6. 把增长因子做成模板参数，对比 1.5 倍与 2 倍在 `push_back` 场景下时间与峰值内存的权衡。
7. **重新采集第 8 节的性能数据**（当前数字是本次修复前的），并重写 `RandomAccess` 用例（预生成下标），
   再对 `ijk` 分块 + `-march=native` 看向量化收益。
8. 把第 11 节那套「抛异常探针」沉淀成可复用的测试工具（一个能按次数抛异常的通用类型 +
   一个检查「无死槽位 / live == size」的断言函数），这样以后新加的接口不用每次重写探针。
   现在 `cpp/main.cpp` 里的 `Probe` / `Owner` / `CopyProbe` 已经有点重复了。
