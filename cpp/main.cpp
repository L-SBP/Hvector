#include <iostream>
#include <algorithm>
#include <stdexcept>
#include "hpp/hvector.hpp"

// ===== 异常安全测试用的探针类型 =====
//   live      : 当前存活对象数（构造 +1，析构 -1）
//   magic     : 构造时置 ALIVE，析构时清零 —— 用来发现"已被析构却仍留在 size 之内"的死槽位
//   countdown : >0 时，第 countdown 次移动构造/移动赋值会抛异常（0 表示不抛）
// 说明：move 不修改 magic，这样"被移动过的对象"仍是活对象，不会被误判成死槽位。

// emplace 用它作为参数时，Probe 的构造会抛异常
struct BoomArg { };

struct Probe {
    static constexpr int ALIVE = 0x5A5A;
    static inline int live = 0;
    static inline int countdown = 0;

    int magic = ALIVE;
    int value = 0;

    static void tick() {
        if (countdown > 0 && --countdown == 0) {
            throw std::runtime_error("probe boom");
        }
    }
    static void arm(int n) { countdown = n; }

    explicit Probe(int v = 0) : value(v) { ++live; }
    Probe(const Probe&) = delete;              // 只移动类型：强制走 move 分支
    Probe& operator=(const Probe&) = delete;

    Probe(Probe&& o) : value(o.value) { tick(); ++live; }
    Probe& operator=(Probe&& o) { tick(); value = o.value; return *this; }

    // emplace 构造新元素时用它来触发构造期异常
    Probe(int, BoomArg) { throw std::runtime_error("probe ctor boom"); }

    ~Probe() { magic = 0; --live; }

    bool is_alive() const { return magic == ALIVE; }
};

// Owner：持有堆资源的类型。用途是让 **LeakSanitizer 自己**（而不只是 live 计数器）
// 证明"抛异常后没有落在 size_ 之外的孤儿对象"——孤儿对象 = 一个没有被析构的 Owner
// = 它内部的 unique_ptr 泄漏。移动构造不 tick，让搬移的前几步能成功；移动赋值 tick，
// 用来精确指定"第几次移动赋值抛异常"。
struct Owner {
    static inline int live = 0;
    static inline int countdown = 0;

    std::unique_ptr<int> ptr;

    static void tick() {
        if (countdown > 0 && --countdown == 0) {
            throw std::runtime_error("owner boom");
        }
    }
    static void arm(int n) { countdown = n; }

    explicit Owner(int v = 0) : ptr(new int(v)) { ++live; }
    Owner(const Owner&) = delete;
    Owner& operator=(const Owner&) = delete;
    Owner(Owner&& o) noexcept : ptr(std::move(o.ptr)) { ++live; }
    Owner& operator=(Owner&& o) {
        tick();
        ptr = std::move(o.ptr);
        return *this;
    }
    ~Owner() { --live; }

    int value() const { return ptr ? *ptr : -1; }
};

// CopyProbe：第 N 次【拷贝构造】抛异常。区间构造 Hvector(iterator, iterator) 的
// catch 回滚路径需要它——那段代码历史上带着两处笔误零覆盖地躺在仓库里，一 include 就编译不过。
struct CopyProbe {
    static constexpr int ALIVE = 0x5A5A;
    static inline int live = 0;
    static inline int countdown = 0;

    int magic = ALIVE;

    static void tick() {
        if (countdown > 0 && --countdown == 0) {
            throw std::runtime_error("copy boom");
        }
    }
    static void arm(int n) { countdown = n; }

    CopyProbe() { ++live; }
    CopyProbe(const CopyProbe&) { tick(); ++live; }
    CopyProbe& operator=(const CopyProbe&) = default;
    ~CopyProbe() { magic = 0; --live; }

    bool is_alive() const { return magic == ALIVE; }
};

int main() {
    // 每个 << 都立刻 flush：std::cout 在重定向到文件/管道时是全缓冲的，
    // 而失败路径用的是 std::abort() —— 不 flush 的话 FAIL 诊断会随缓冲区一起丢掉，
    // 重定向跑测试就只能看到一个光秃秃的 "Aborted"。
    std::cout << std::unitbuf;

    std::cout << "==== Test1: emplace_back + 自动扩容 ====\n";
    Hvector<int> v1;
    v1.emplace_back(10);
    v1.emplace_back(20);
    v1.emplace_back(30);
    v1.emplace_back(40);
    print_vec("v1", v1);


    std::cout << "\n==== Test2: 拷贝构造 ====\n";
    Hvector<int> v2 = v1;
    v2[1] = 999;
    print_vec("v1", v1);
    print_vec("v2", v2);


    std::cout << "\n==== Test3: 拷贝赋值 ====\n";
    Hvector<int> v3;
    v3.emplace_back(1);
    v3.emplace_back(2);
    v3 = v1;
    print_vec("v3", v3);


    std::cout << "\n==== Test4: 自赋值测试 v3 = v3 ====\n";
    v3 = v3;
    print_vec("v3", v3);


    std::cout << "\n==== Test5: 移动构造 ====\n";
    Hvector<int> v4 = std::move(v1);
    print_vec("v4", v4);
    print_vec("v1(已经被移走，应该空)", v1);


    std::cout << "\n==== Test6: 移动赋值 ====\n";
    Hvector<int> v5;
    v5.emplace_back(777);
    v5 = std::move(v4);
    print_vec("v5", v5);
    print_vec("v4(被移走后)", v4);


    std::cout << "\n==== Test7: STL算法 std::sort ====\n";
    Hvector<int> vsort;
    vsort.emplace_back(5);
    vsort.emplace_back(1);
    vsort.emplace_back(3);
    std::sort(vsort.begin(), vsort.end());
    print_vec("sorted vsort", vsort);


    std::cout << "\n==== Test8: at越界异常测试 ====\n";
    try {
        auto val = vsort.at(100);
        std::cout << "ERROR: 不该执行到这里\n";
    } catch (std::out_of_range& e) {
        std::cout << "OK: catch out_of_range: " << e.what() << "\n";
    }
    std::cout << "\n==== Test9: insert 头部 / 中间 / 尾部 ====\n";
    {
        Hvector<int> v;
        for (int i = 1; i <= 4; ++i) v.emplace_back(i);   // [1,2,3,4]
        expect_vec("init", v, {1,2,3,4});

        // 头部插入
        v.insert(size_t(0), 0);                            // [0,1,2,3,4]
        expect_vec("insert head", v, {0,1,2,3,4});

        // 中间插入
        v.insert(size_t(3), 99);                           // [0,1,2,99,3,4]
        expect_vec("insert middle", v, {0,1,2,99,3,4});

        // 尾部插入，idx == size()
        v.insert(v.size(), 100);                           // [0,1,2,99,3,4,100]
        expect_vec("insert tail", v, {0,1,2,99,3,4,100});

        // 空容器插入第一个
        Hvector<int> empty;
        empty.insert(size_t(0), 7);                        // [7]
        expect_vec("insert into empty", empty, {7});
    }

    std::cout << "\n==== Test10: insert 触发扩容 ====\n";
    {
        Hvector<int> v;
        for (int i = 0; i < 4; ++i) v.emplace_back(i);     // size=4, cap=4
        size_t old_cap = v.capacity();

        // 头部插入，必然扩容
        v.insert(size_t(0), -1);
        std::cout << "old_cap=" << old_cap
                << " new_cap=" << v.capacity()
                << " size=" << v.size() << "\n";
        expect_vec("insert after grow", v, {-1,0,1,2,3});
        if (v.capacity() <= old_cap) {
            std::cout << "FAIL: capacity did not grow\n";
            std::abort();
        }
    }

    std::cout << "\n==== Test11: insert 迭代器版本 ====\n";
    {
        Hvector<int> v;
        for (int i = 1; i <= 3; ++i) v.emplace_back(i);    // [1,2,3]

        // 头部：begin()
        v.insert(v.begin(), 0);                            // [0,1,2,3]
        expect_vec("insert at begin", v, {0,1,2,3});

        // 中间：begin()+2
        v.insert(v.begin() + 2, 99);                       // [0,1,99,2,3]
        expect_vec("insert at begin+2", v, {0,1,99,2,3});

        // 尾部：end()
        v.insert(v.end(), 100);                            // [0,1,99,2,3,100]
        expect_vec("insert at end", v, {0,1,99,2,3,100});
    }

    std::cout << "\n==== Test12: insert 右值 / 移动语义 ====\n";
    {
        Hvector<std::string> v;
        v.emplace_back("a");
        v.emplace_back("b");
        v.emplace_back("c");                               // [a,b,c]

        std::string s = "hello";
        v.insert(size_t(1), std::move(s));                 // 移动插入到中间
        // s 被移走，内容未指定，不检查 s

        std::cout << "v = [";
        for (auto& x : v) std::cout << x << ",";
        std::cout << "]\n";

        if (v.size() != 4 || v[0] != "a" || v[1] != "hello"
            || v[2] != "b" || v[3] != "c") {
            std::cout << "FAIL: insert rvalue\n";
            std::abort();
        }
        std::cout << "PASS insert rvalue\n";
    }

    std::cout << "\n==== Test13: erase 头部 / 中间 / 尾部 ====\n";
    {
        Hvector<int> v;
        for (int i = 0; i < 5; ++i) v.emplace_back(i);     // [0,1,2,3,4]

        // 中间删除
        v.erase(size_t(2));                                // [0,1,3,4]
        expect_vec("erase middle", v, {0,1,3,4});

        // 头部删除
        v.erase(size_t(0));                                // [1,3,4]
        expect_vec("erase head", v, {1,3,4});

        // 尾部删除
        v.erase(v.size() - 1);                             // [1,3]
        expect_vec("erase tail", v, {1,3});

        // 删到空
        v.erase(size_t(0));
        v.erase(size_t(0));
        expect_vec("erase to empty", v, {});
        if (!v.empty()) {
            std::cout << "FAIL: should be empty\n";
            std::abort();
        }
    }

    std::cout << "\n==== Test14: erase 迭代器版本 + 返回值 ====\n";
    {
        Hvector<int> v;
        for (int i = 0; i < 5; ++i) v.emplace_back(i);     // [0,1,2,3,4]

        auto it = v.erase(v.begin() + 1);                  // 删除 1, 返回指向 2
        expect_vec("erase begin+1", v, {0,2,3,4});
        if (*it != 2) {
            std::cout << "FAIL: erase return iterator = " << *it << " expect 2\n";
            std::abort();
        }

        // 连续删除：删除所有偶数
        for (auto i = v.begin(); i != v.end(); ) {
            if (*i % 2 == 0) i = v.erase(i);
            else ++i;
        }
        expect_vec("erase all even", v, {3});
    }

    std::cout << "\n==== Test15: erase 到空后再 insert ====\n";
    {
        Hvector<int> v;
        for (int i = 0; i < 3; ++i) v.emplace_back(i);
        while (!v.empty()) v.erase(size_t(0));
        expect_vec("empty after erase", v, {});

        v.insert(size_t(0), 42);
        expect_vec("insert after empty", v, {42});
    }

    std::cout << "\n==== Test16: insert/erase 资源类型 std::string ====\n";
    {
        Hvector<std::string> v;
        v.emplace_back("one");
        v.emplace_back("two");
        v.emplace_back("three");                           // [one,two,three]

        v.insert(size_t(1), std::string("inserted"));      // [one,inserted,two,three]
        if (v.size() != 4 || v[0] != "one" || v[1] != "inserted"
            || v[2] != "two" || v[3] != "three") {
            std::cout << "FAIL: string insert\n";
            std::abort();
        }

        v.erase(size_t(2));                                // [one,inserted,three]
        if (v.size() != 3 || v[0] != "one" || v[1] != "inserted"
            || v[2] != "three") {
            std::cout << "FAIL: string erase\n";
            std::abort();
        }
        std::cout << "PASS string insert/erase\n";
    }

    std::cout << "\n==== Test17: insert/erase 只移动类型 unique_ptr ====\n";
    {
        Hvector<std::unique_ptr<int>> v;
        v.emplace_back(new int(1));
        v.emplace_back(new int(2));
        v.emplace_back(new int(3));                        // [1,2,3]

        // 中间移动插入
        v.insert(size_t(1), std::make_unique<int>(99));    // [1,99,2,3]
        if (v.size() != 4 || *v[0] != 1 || *v[1] != 99
            || *v[2] != 2 || *v[3] != 3) {
            std::cout << "FAIL: unique_ptr insert\n";
            std::abort();
        }

        // 中间删除
        v.erase(size_t(1));                                // [1,2,3]
        if (v.size() != 3 || *v[0] != 1 || *v[1] != 2 || *v[2] != 3) {
            std::cout << "FAIL: unique_ptr erase\n";
            std::abort();
        }
        std::cout << "PASS unique_ptr insert/erase\n";
    }

    std::cout << "\n==== Test18: insert 越界异常 ====\n";
    {
        Hvector<int> v;
        v.emplace_back(1);

        try {
            v.insert(size_t(100), 0);
            std::cout << "FAIL: insert 越界未抛异常\n";
            std::abort();
        } catch (std::out_of_range& e) {
            std::cout << "OK: insert out_of_range: " << e.what() << "\n";
        }
    }

    std::cout << "\n==== Test19: erase 越界异常 ====\n";
    {
        Hvector<int> v;
        v.emplace_back(1);

        try {
            v.erase(size_t(100));
            std::cout << "FAIL: erase 越界未抛异常\n";
            std::abort();
        } catch (std::out_of_range& e) {
            std::cout << "OK: erase out_of_range: " << e.what() << "\n";
        }

        // 空容器 erase(0)
        Hvector<int> empty;
        try {
            empty.erase(size_t(0));
            std::cout << "FAIL: empty erase(0) 未抛异常\n";
            std::abort();
        } catch (std::out_of_range& e) {
            std::cout << "OK: empty erase out_of_range: " << e.what() << "\n";
        }
    }

    std::cout << "\n==== Test20: insert/erase 大量连续操作 ====\n";
    {
        Hvector<int> v;
        const int N = 1000;

        // 依次尾部插入
        for (int i = 0; i < N; ++i) {
            v.insert(v.size(), i);
        }
        if (v.size() != (size_t)N) {
            std::cout << "FAIL: size after bulk insert = " << v.size() << "\n";
            std::abort();
        }
        for (int i = 0; i < N; ++i) {
            if (v[i] != i) {
                std::cout << "FAIL: bulk insert v[" << i << "]=" << v[i] << "\n";
                std::abort();
            }
        }
        std::cout << "PASS bulk insert, size=" << v.size()
                << " cap=" << v.capacity() << "\n";

        // 删除所有偶数索引：每次删当前位置，i 后移跳过被提升上来的元素
        for (size_t i = 0; i < v.size(); i++) {
            v.erase(i);
        }
        std::cout << "after erase: size=" << v.size() << "\n";
        if (v.size() != (size_t)(N / 2)) {
            std::cout << "FAIL: size after bulk erase = " << v.size()
                    << " expect " << N/2 << "\n";
            std::abort();
        }

        // 校验剩余元素都是奇数：1,3,5,...
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] != (int)(2 * i + 1)) {
                std::cout << "FAIL: v[" << i << "]=" << v[i]
                        << " expect " << (2*i+1) << "\n";
                std::abort();
            }
        }
        std::cout << "PASS bulk insert/erase\n";
    }

    std::cout << "\n==== Test21: shrink_to_fit ====\n";
    {
        Hvector<int> v;
        for (int i = 0; i < 8; ++i) v.emplace_back(i);
        v.reserve(32);
        if (v.capacity() < 32) {
            std::cout << "FAIL: reserve did not grow capacity\n";
            std::abort();
        }

        v.resize(3);
        v.shrink_to_fit();
        if (v.size() != 3 || v.capacity() != 3
            || v[0] != 0 || v[1] != 1 || v[2] != 2) {
            std::cout << "FAIL: shrink_to_fit int\n";
            std::abort();
        }
        std::cout << "PASS shrink_to_fit int, size=" << v.size()
                << " cap=" << v.capacity() << "\n";

        Hvector<std::string> strings;
        strings.emplace_back("one");
        strings.emplace_back("two");
        strings.reserve(16);
        strings.pop_back();
        strings.shrink_to_fit();
        if (strings.size() != 1 || strings.capacity() != 1
            || strings[0] != "one") {
            std::cout << "FAIL: shrink_to_fit string\n";
            std::abort();
        }
        std::cout << "PASS shrink_to_fit string\n";

        Hvector<int> empty;
        empty.reserve(8);
        empty.shrink_to_fit();
        if (!empty.empty() || empty.capacity() != 0 || empty.data() != nullptr) {
            std::cout << "FAIL: shrink_to_fit empty\n";
            std::abort();
        }
        std::cout << "PASS shrink_to_fit empty\n";
    }

    std::cout << "\n==== Test22: assign ====\n";
    {
        Hvector<int> v;
        v.reserve(8);
        v.emplace_back(1);
        v.emplace_back(2);
        size_t old_cap = v.capacity();

        v.assign(4, 7);
        if (v.size() != 4 || v.capacity() != old_cap
            || v[0] != 7 || v[1] != 7 || v[2] != 7 || v[3] != 7) {
            std::cout << "FAIL: assign within capacity\n";
            std::abort();
        }
        std::cout << "PASS assign within capacity\n";

        v.assign(12, 9);
        if (v.size() != 12 || v.capacity() < 12) {
            std::cout << "FAIL: assign with growth\n";
            std::abort();
        }
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] != 9) {
                std::cout << "FAIL: assign with growth value\n";
                std::abort();
            }
        }
        std::cout << "PASS assign with growth\n";

        v.assign(3, v[2]);
        if (v.size() != 3 || v[0] != 9 || v[1] != 9 || v[2] != 9) {
            std::cout << "FAIL: assign aliased value\n";
            std::abort();
        }
        std::cout << "PASS assign aliased value\n";

        Hvector<std::string> strings;
        strings.assign(3, std::string("hello"));
        if (strings.size() != 3 || strings[0] != "hello"
            || strings[1] != "hello" || strings[2] != "hello") {
            std::cout << "FAIL: assign string\n";
            std::abort();
        }
        std::cout << "PASS assign string\n";

        strings.assign(0, std::string("unused"));
        if (!strings.empty()) {
            std::cout << "FAIL: assign empty\n";
            std::abort();
        }
        std::cout << "PASS assign empty\n";
    }

    std::cout << "\n==== Test23: emplace ====\n";
    {
        Hvector<int> v;
        auto first = v.emplace(0, 10);
        if (first != v.begin() || v.size() != 1 || v[0] != 10) {
            std::cout << "FAIL: emplace into empty\n";
            std::abort();
        }

        v.reserve(8);
        v.emplace(v.size(), 30);
        v.emplace(1, 20);
        v.emplace(1, 15);
        if (v.size() != 4 || v[0] != 10 || v[1] != 15
            || v[2] != 20 || v[3] != 30) {
            std::cout << "FAIL: emplace head/middle/tail\n";
            std::abort();
        }
        std::cout << "PASS emplace head/middle/tail\n";

        Hvector<std::string> strings;
        strings.emplace(0, "hello");
        strings.emplace(1, 3, '!');
        strings.emplace(strings.size(), "world");
        if (strings.size() != 3 || strings[0] != "hello"
            || strings[1] != "!!!" || strings[2] != "world") {
            std::cout << "FAIL: emplace string arguments\n";
            std::abort();
        }
        std::cout << "PASS emplace string arguments\n";

        try {
            v.emplace(v.size() + 1, 99);
            std::cout << "FAIL: emplace 越界未抛异常\n";
            std::abort();
        } catch (std::out_of_range& e) {
            std::cout << "OK: emplace out_of_range: " << e.what() << "\n";
        }
    }

    std::cout << "\n==== Test24: (count, value) 填充构造 ====\n";
    {
        Hvector<int> a(3, 7);
        if (a.size() != 3 || a.capacity() != 3
            || a[0] != 7 || a[1] != 7 || a[2] != 7) {
            std::cout << "FAIL: fill ctor int\n";
            std::abort();
        }
        std::cout << "PASS fill ctor int\n";

        // count == 0 时不得留下野指针
        Hvector<int> zero(0, 7);
        if (!zero.empty() || zero.capacity() != 0 || zero.data() != nullptr) {
            std::cout << "FAIL: fill ctor zero count\n";
            std::abort();
        }
        std::cout << "PASS fill ctor zero count\n";

        Hvector<std::string> s(2, "hi");
        if (s.size() != 2 || s[0] != "hi" || s[1] != "hi") {
            std::cout << "FAIL: fill ctor string\n";
            std::abort();
        }
        // 填充构造后仍可正常扩容
        s.emplace_back("more");
        if (s.size() != 3 || s[2] != "more") {
            std::cout << "FAIL: fill ctor growth\n";
            std::abort();
        }
        std::cout << "PASS fill ctor string + growth\n";
    }

    std::cout << "\n==== Test25: initializer_list 构造 ====\n";
    {
        Hvector<int> v{1, 2, 3, 4};
        expect_vec("init_list int", v, {1,2,3,4});

        // 深拷贝：修改副本不应影响原容器
        Hvector<int> copy = v;
        copy[0] = 100;
        if (v[0] != 1 || copy[0] != 100) {
            std::cout << "FAIL: init_list deep copy\n";
            std::abort();
        }
        std::cout << "PASS init_list deep copy\n";

        Hvector<std::string> s{"a", "b"};
        if (s.size() != 2 || s[0] != "a" || s[1] != "b") {
            std::cout << "FAIL: init_list string\n";
            std::abort();
        }
        std::cout << "PASS init_list string\n";

        std::initializer_list<int> no_elem;
        Hvector<int> empty(no_elem);
        if (!empty.empty() || empty.capacity() != 0 || empty.data() != nullptr) {
            std::cout << "FAIL: init_list empty\n";
            std::abort();
        }
        std::cout << "PASS init_list empty\n";
    }

    std::cout << "\n==== Test26: insert/emplace 非扩容路径的异常安全 ====\n";
    {
        // 校验容器不变量：size 之内的槽位全是活对象，且对象总数恰好等于 size
        // （后者同时排除"size 之外的孤儿对象"和泄漏；前者排除"size 之内的死槽位"）
        auto check = [](const char* tag, const Hvector<Probe>& v) {
            for (size_t i = 0; i < v.size(); ++i) {
                if (!v[i].is_alive()) {
                    std::cout << "FAIL " << tag << ": size 内的槽位 " << i
                              << " 已被析构（无效状态）\n";
                    std::abort();
                }
            }
            if (Probe::live != static_cast<int>(v.size())) {
                std::cout << "FAIL " << tag << ": live=" << Probe::live
                          << " size=" << v.size()
                          << "（存在孤儿对象或泄漏）\n";
                std::abort();
            }
        };

        // (a) 正常路径：容量富余时头部 / 中间 / 尾部插入
        {
            Hvector<Probe> v;
            v.reserve(16);
            for (int i = 0; i < 4; ++i) v.emplace_back(Probe(i));

            v.insert(size_t(0), Probe(100));      // 头部
            v.insert(size_t(2), Probe(101));      // 中间
            v.emplace(v.size(), 102);             // 尾部

            const int expect[7] = {100, 0, 101, 1, 2, 3, 102};
            if (v.size() != 7) {
                std::cout << "FAIL: no-throw size=" << v.size() << "\n";
                std::abort();
            }
            for (size_t i = 0; i < v.size(); ++i) {
                if (!v[i].is_alive() || v[i].value != expect[i]) {
                    std::cout << "FAIL: no-throw v[" << i << "]=" << v[i].value
                              << " expect=" << expect[i] << "\n";
                    std::abort();
                }
            }
            check("no-throw", v);
            std::cout << "PASS 非扩容 insert/emplace 正常路径\n";
        }

        // (b) 强保证：实参先被落成的那个临时对象在移动构造时抛异常——此时容器还没被碰过，
        //     所以必须原样不动。tick 编号见 (c) 的说明
        {
            Hvector<Probe> v;
            v.reserve(16);
            for (int i = 0; i < 4; ++i) v.emplace_back(Probe(i));

            Probe::arm(1);                        // 第 1 次移动构造开始抛
            bool threw = false;
            try {
                v.insert(size_t(1), Probe(99));
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (!threw) {
                std::cout << "FAIL: 未抛异常\n";
                std::abort();
            }
            if (v.size() != 4 || v[0].value != 0 || v[1].value != 1
                || v[2].value != 2 || v[3].value != 3) {
                std::cout << "FAIL: 构造尾元素抛异常时应保持容器原样\n";
                std::abort();
            }
            check("strong-construct-fail", v);
            std::cout << "PASS 移动构造抛异常 -> 容器原样（强保证）\n";
        }

        // (c) 基本保证：搬移过程中的移动赋值抛异常
        //     非扩容的 insert(size_t(1), Probe(99)) 在 4 元素容器上的 tick 编号：
        //       #1 实参临时对象的移动构造        #2 末尾备用槽位的移动构造
        //       #3 #4 搬移中的两次移动赋值       #5 把新值写回 idx 的移动赋值
        //     arm 2 落在"搬移还没开始"的阶段（容器应原样），3/4 落在搬移阶段（基本保证）
        for (int arm_at : {2, 3, 4}) {
            Hvector<Probe> v;
            v.reserve(16);
            for (int i = 0; i < 4; ++i) v.emplace_back(Probe(i));

            Probe::arm(arm_at);
            bool threw = false;
            try {
                v.insert(size_t(1), Probe(99));
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (!threw) {
                std::cout << "FAIL: 第 " << arm_at << " 次移动未抛异常\n";
                std::abort();
            }
            // 核心：不能有死槽位，也不能有孤儿对象
            check("basic", v);
            std::cout << "PASS 第 " << arm_at
                      << " 次移动抛异常 -> 容器仍有效 (size=" << v.size()
                      << ", live=" << Probe::live << ")\n";

            // 抛出异常后容器还能继续正常使用
            v.emplace_back(Probe(7));
            check("basic-reuse", v);
            v.clear();
            check("basic-clear", v);
        }

        // (d) 强保证：emplace 的新元素构造抛异常（此时容器还没被搬移）
        {
            Hvector<Probe> v;
            v.reserve(16);
            for (int i = 0; i < 4; ++i) v.emplace_back(Probe(i));

            bool threw = false;
            try {
                v.emplace(size_t(2), 5, BoomArg{});
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (!threw) {
                std::cout << "FAIL: emplace 构造未抛异常\n";
                std::abort();
            }
            if (v.size() != 4 || v[0].value != 0 || v[1].value != 1
                || v[2].value != 2 || v[3].value != 3) {
                std::cout << "FAIL: emplace 构造抛异常时应保持容器原样\n";
                std::abort();
            }
            check("strong-emplace-ctor-fail", v);
            std::cout << "PASS emplace 构造抛异常 -> 容器原样（强保证）\n";
        }

        // (e) 对照组：扩容路径本来就是"新缓冲区 + 成功后再换"，同样保持原样
        {
            Hvector<Probe> v;
            v.reserve(1);
            v.emplace_back(Probe(0));             // size=1, cap=1 (已满)

            Probe::arm(2);                        // 在新缓冲区里构造第 2 个元素时抛
            bool threw = false;
            try {
                v.insert(size_t(0), Probe(77));
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (!threw) {
                std::cout << "FAIL: 扩容路径未抛异常\n";
                std::abort();
            }
            if (v.size() != 1 || v[0].value != 0) {
                std::cout << "FAIL: 扩容路径抛异常时应保持容器原样\n";
                std::abort();
            }
            check("strong-growth", v);
            std::cout << "PASS 扩容路径抛异常 -> 容器原样（强保证）\n";
        }

        // (f) 扩容路径：新缓冲区在【每一步】抛异常都必须被完整拆掉
        //     4 元素 / 4 容量上 insert(idx, Probe(99)) 的 tick 编号：
        //       #1 新元素， #2..#(1+idx) 搬移头部 [0, idx)，其后搬移尾部 [idx, size_)，共 5 次
        //     新元素的构造顺序改成了"先新元素、再搬移"，catch 里因此要记三段
        //     （新元素 + 头部前缀 + 尾部前缀），漏掉任何一段都会留下 size_ 之外的孤儿对象。
        for (size_t idx : {size_t(0), size_t(2), size_t(4)}) {
            for (int arm_at = 1; arm_at <= 5; ++arm_at) {
                Hvector<Probe> v;
                v.reserve(4);
                for (int i = 0; i < 4; ++i) v.emplace_back(Probe(i));   // size=4 cap=4
                size_t cap_before = v.capacity();

                Probe::arm(arm_at);
                bool threw = false;
                try {
                    v.insert(idx, Probe(99));
                } catch (const std::runtime_error&) {
                    threw = true;
                }
                if (!threw) {
                    std::cout << "FAIL: 扩容路径 idx=" << idx << " 第 " << arm_at
                              << " 次移动构造未抛异常\n";
                    std::abort();
                }
                if (v.size() != 4 || v.capacity() != cap_before) {
                    std::cout << "FAIL: 扩容路径 idx=" << idx
                              << " 抛异常后 size/cap 变了\n";
                    std::abort();
                }
                check("growth-rollback", v);
                v.clear();
                if (Probe::live != 0) {
                    std::cout << "FAIL: 扩容回滚后 clear，live=" << Probe::live << "\n";
                    std::abort();
                }
            }
        }
        std::cout << "PASS 扩容路径每一步抛异常 -> 新缓冲区整体回滚（3 个位置 x 5 步 = 15 种组合）\n";

        if (Probe::live != 0) {
            std::cout << "FAIL: Probe 泄漏，live=" << Probe::live << "\n";
            std::abort();
        }
        std::cout << "PASS 所有 Probe 均已析构，无泄漏\n";
    }

    std::cout << "\n==== Test27: 实参引用容器自身（别名） ====\n";
    {
        // 局部辅助：把 Hvector<std::string> 与期望值逐元素比对
        auto expect_str = [](const char* tag, const Hvector<std::string>& v,
                             std::initializer_list<const char*> expect) {
            if (v.size() != expect.size()) {
                std::cout << "FAIL " << tag << ": size=" << v.size()
                          << " expect=" << expect.size() << "\n";
                std::abort();
            }
            size_t i = 0;
            for (const char* e : expect) {
                if (v[i] != e) {
                    std::cout << "FAIL " << tag << ": v[" << i << "]=\"" << v[i]
                              << "\" expect=\"" << e << "\"\n";
                    std::abort();
                }
                ++i;
            }
            std::cout << "PASS " << tag << ": ";
            print_vec(tag, v);
        };

        // (a) 尾部插入 + 必然扩容：push_back(v[0])
        //     （顺带补上 push_back 的首次覆盖——此前 26 组测试一次都没调用过它）
        {
            Hvector<std::string> v;
            v.emplace_back("AAA");
            v.emplace_back("BBB");                    // size=2 cap=2，下一次必然扩容
            v.push_back(v[0]);
            expect_str("push_back(v[0]) 扩容", v, {"AAA", "BBB", "AAA"});
        }

        // (b) emplace_back + 必然扩容，别名元素在 idx 之前
        {
            Hvector<std::string> v;
            v.emplace_back("one");
            v.emplace_back("two");
            v.emplace_back("three");
            v.emplace_back("four");                   // size=4 cap=4
            v.emplace_back(v[1]);
            expect_str("emplace_back(v[1]) 扩容", v,
                       {"one", "two", "three", "four", "two"});
        }

        // (c) 容量富余时的头部插入：非扩容路径（搬移会覆盖被引用的元素）
        {
            Hvector<std::string> v;
            v.emplace_back("A");
            v.emplace_back("B");
            v.emplace_back("C");
            v.emplace_back("D");
            v.reserve(16);                            // 容量富余 -> _insert_no_realloc
            v.insert(size_t(0), v[2]);
            expect_str("insert(0, v[2]) 非扩容", v, {"C", "A", "B", "C", "D"});
        }

        // (d) 满容量 + idx>0 的中间插入：扩容路径，别名元素在 idx 之前
        {
            Hvector<std::string> v;
            v.emplace_back("A");
            v.emplace_back("B");
            v.emplace_back("C");
            v.emplace_back("D");                      // size=4 cap=4
            v.insert(size_t(2), v[0]);
            expect_str("insert(2, v[0]) 扩容", v, {"A", "B", "A", "C", "D"});
        }

        // (e) int 实例化：拷贝构造会被内联进模板代码，ASan 能插桩到那次读
        {
            Hvector<int> v;
            v.emplace_back(11);
            v.emplace_back(22);
            v.push_back(v[0]);
            expect_vec("int push_back(v[0])", v, {11, 22, 11});
        }

        // (f) std::vector 对照：同一串调用必须给出相同结果
        {
            std::vector<std::string> s;
            s.emplace_back("AAA");
            s.emplace_back("BBB");
            s.push_back(s[0]);

            Hvector<std::string> h;
            h.emplace_back("AAA");
            h.emplace_back("BBB");
            h.push_back(h[0]);

            if (s.size() != h.size()) {
                std::cout << "FAIL: 与 std::vector 尺寸不一致\n";
                std::abort();
            }
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] != h[i]) {
                    std::cout << "FAIL: 与 std::vector 不一致 v[" << i
                              << "]: Hvector=\"" << h[i]
                              << "\" std::vector=\"" << s[i] << "\"\n";
                    std::abort();
                }
            }
            std::cout << "PASS 与 std::vector 行为一致: ";
            print_vec("Hvector", h);
        }
    }

    std::cout << "\n==== Test28: erase 抛异常（异常传播 + 容器保持有效） ====\n";
    {
        // 校验容器不变量：size 之内的槽位全是活对象，且对象总数恰好等于 size
        // （前者排除"size 之内的死槽位"，后者排除"size_ 之外的孤儿对象"）
        auto check = [](const char* tag, const Hvector<Probe>& v) {
            for (size_t i = 0; i < v.size(); ++i) {
                if (!v[i].is_alive()) {
                    std::cout << "FAIL " << tag << ": size 内的槽位 " << i
                              << " 已被析构（无效状态，析构函数会二次析构）\n";
                    std::abort();
                }
            }
            if (Probe::live != static_cast<int>(v.size())) {
                std::cout << "FAIL " << tag << ": live=" << Probe::live
                          << " size=" << v.size() << "（孤儿对象或泄漏）\n";
                std::abort();
            }
        };

        // (a) erase(0)：搬移过程中的第 2 次移动赋值抛异常
        {
            Hvector<Probe> v;
            v.reserve(16);
            for (int i = 0; i < 6; ++i) v.emplace_back(Probe(i));
            size_t before = v.size();

            Probe::arm(2);
            bool threw = false;
            try {
                v.erase(size_t(0));
            } catch (const std::runtime_error&) {
                threw = true;
            }

            // 关键 1：异常必须往外传。erase 是"保证失败"的操作，
            //         把它伪装成成功返回比直接抛异常危险得多
            if (!threw) {
                std::cout << "FAIL: erase 把异常吞掉了（调用方无法感知失败）\n";
                std::abort();
            }
            std::cout << "PASS erase 异常向外传播\n";

            // 关键 2：size_ 必须保持原值，且 size 之内不能有死槽位
            if (v.size() != before) {
                std::cout << "FAIL: 抛异常后 size=" << v.size()
                          << "，应保持 " << before << "\n";
                std::abort();
            }
            check("erase-throw", v);
            std::cout << "PASS 抛异常后容器有效（size=" << v.size()
                      << "，无死槽位、无孤儿）\n";

            // 关键 3：抛异常后容器还能继续正常使用
            v.emplace_back(Probe(100));
            check("erase-throw-reuse", v);
            v.erase(size_t(1));
            check("erase-throw-erase-again", v);
            v.clear();
            if (Probe::live != 0) {
                std::cout << "FAIL: clear 后仍有 live=" << Probe::live << "\n";
                std::abort();
            }
            std::cout << "PASS 抛异常后仍可 emplace_back/erase/clear，最终 live=0\n";
        }

        // (b) 区间 erase：第 1 次移动赋值就抛
        {
            Hvector<Probe> v;
            v.reserve(16);
            for (int i = 0; i < 5; ++i) v.emplace_back(Probe(i));

            Probe::arm(1);
            bool threw = false;
            try {
                v.erase(v.begin() + 1, v.begin() + 3);
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (!threw) {
                std::cout << "FAIL: 区间 erase 未把异常传出来\n";
                std::abort();
            }
            if (v.size() != 5) {
                std::cout << "FAIL: 区间 erase 抛异常后 size=" << v.size()
                          << "，应保持 5\n";
                std::abort();
            }
            check("range-erase-throw", v);
            std::cout << "PASS 区间 erase 抛异常 -> 容器仍有效\n";
            v.clear();
        }

        if (Probe::live != 0) {
            std::cout << "FAIL: Test28 结束时 Probe 泄漏，live=" << Probe::live << "\n";
            std::abort();
        }
    }

    std::cout << "\n==== Test29: 资源类型非扩容 insert 抛异常（LeakSanitizer） ====\n";
    {
        // Owner 持有 unique_ptr：一旦有对象落在 size_ 之外（没有被析构），
        // 它内部的堆内存就是真泄漏，LeakSanitizer 会在进程退出时报错。
        // 4 元素容器上 insert(size_t(1), Owner(99)) 的移动赋值编号：
        //   #1 #2 = 搬移循环里的两次移动赋值      #3 = 把新值写回 idx
        for (int arm_at : {1, 2, 3}) {
            Hvector<Owner> v;
            v.reserve(16);
            for (int i = 0; i < 4; ++i) v.emplace_back(Owner(i));

            Owner::arm(arm_at);
            bool threw = false;
            try {
                v.insert(size_t(1), Owner(99));
            } catch (const std::runtime_error&) {
                threw = true;
            }

            if (!threw) {
                std::cout << "FAIL: 第 " << arm_at << " 次移动赋值未抛异常\n";
                std::abort();
            }
            if (Owner::live != static_cast<int>(v.size())) {
                std::cout << "FAIL: live=" << Owner::live << " size=" << v.size()
                          << "（有孤儿对象落在 size_ 之外 -> 泄漏）\n";
                std::abort();
            }
            std::cout << "PASS 第 " << arm_at << " 次移动赋值抛异常 -> live="
                      << Owner::live << " == size=" << v.size()
                      << "（无孤儿、无泄漏）\n";

            // 抛异常后仍可继续使用
            v.emplace_back(Owner(7));
            if (Owner::live != static_cast<int>(v.size())) {
                std::cout << "FAIL: 复用后 live=" << Owner::live
                          << " size=" << v.size() << "\n";
                std::abort();
            }
            v.clear();
            if (Owner::live != 0) {
                std::cout << "FAIL: clear 后仍有 live=" << Owner::live << "\n";
                std::abort();
            }
        }
        std::cout << "PASS 所有 Owner 均已析构（LeakSanitizer 亦应静默）\n";
    }

    std::cout << "\n==== Test30: 区间构造 Hvector(iterator, iterator) ====\n";
    {
        // (a) 来自 std::vector 的元素区间。注意区间构造的形参类型就是 T*，
        //     不能直接吃 std::vector<int>::iterator（__normal_iterator 不会隐式转成 int*），
        //     所以用 data() —— 这也是这个构造函数的一个已知局限
        std::vector<int> src{1, 2, 3, 4, 5};
        Hvector<int> a(src.data(), src.data() + src.size());
        expect_vec("range from vector", a, {1, 2, 3, 4, 5});
        if (a.size() != 5 || a.capacity() != 5) {
            std::cout << "FAIL: 区间构造后 size=" << a.size()
                      << " cap=" << a.capacity() << "，应为 5/5\n";
            std::abort();
        }
        std::cout << "PASS range size/capacity = 5/5\n";

        // (b) 裸数组 / 单元素区间 / 空区间
        int arr[3] = {7, 8, 9};
        Hvector<int> b(arr, arr + 3);
        expect_vec("range from array", b, {7, 8, 9});

        Hvector<int> one(arr, arr + 1);
        expect_vec("range single element", one, {7});

        Hvector<int> none(arr, arr);
        if (!none.empty() || none.capacity() != 0 || none.data() != nullptr) {
            std::cout << "FAIL: 空区间应得到 data_==nullptr / cap==0\n";
            std::abort();
        }
        std::cout << "PASS range empty (data_==nullptr, cap==0)\n";

        // (c) std::string 来源 + 深拷贝
        std::vector<std::string> names{"alpha", "beta", "gamma"};
        Hvector<std::string> s(names.data(), names.data() + names.size());
        if (s.size() != 3 || s[0] != "alpha" || s[1] != "beta" || s[2] != "gamma") {
            std::cout << "FAIL: range string\n";
            std::abort();
        }
        s[0] = "changed";
        if (names[0] != "alpha") {
            std::cout << "FAIL: range string 不是深拷贝\n";
            std::abort();
        }
        std::cout << "PASS range string + deep copy\n";

        // (d) 来自另一个 Hvector 的区间，且构造后仍能正常扩容
        Hvector<int> c(a.begin(), a.end());
        expect_vec("range from Hvector", c, {1, 2, 3, 4, 5});
        c.emplace_back(6);
        expect_vec("range then grow", c, {1, 2, 3, 4, 5, 6});

        // (e) 拷贝构造抛异常 -> catch 回滚，不能泄漏、不能留下野指针
        {
            std::vector<CopyProbe> source(6);
            const int base = CopyProbe::live;          // 源容器里的 6 个活对象
            CopyProbe::arm(3);                         // 第 3 次拷贝构造抛

            bool threw = false;
            try {
                Hvector<CopyProbe> v(source.data(), source.data() + source.size());
            } catch (const std::runtime_error&) {
                threw = true;
            }
            if (!threw) {
                std::cout << "FAIL: 区间构造未抛异常\n";
                std::abort();
            }
            if (CopyProbe::live != base) {
                std::cout << "FAIL: 回滚不干净，live=" << CopyProbe::live
                          << " 应为 " << base << "（已构造的 2 个没有析构）\n";
                std::abort();
            }
            std::cout << "PASS range ctor rollback: live=" << CopyProbe::live
                      << " == 源容器元素数\n";
        }
        if (CopyProbe::live != 0) {
            std::cout << "FAIL: CopyProbe 泄漏，live=" << CopyProbe::live << "\n";
            std::abort();
        }
        std::cout << "PASS 区间构造回滚无泄漏\n";
    }

    std::cout << "\nAll test done\n";
    return 0;
}