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

int main() {
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

        // (b) 强保证：搬移的第一步（末尾备用槽位的移动构造）抛异常，容器应原样不动
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
        //     第 2/3 次是搬移中的赋值，第 4 次是把新值写入 idx
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
                std::cout << "FAIL: 第 " << arm_at << " 次搬移未抛异常\n";
                std::abort();
            }
            // 核心：不能有死槽位，也不能有孤儿对象
            check("basic", v);
            std::cout << "PASS 第 " << arm_at
                      << " 次搬移抛异常 -> 容器仍有效 (size=" << v.size()
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

        if (Probe::live != 0) {
            std::cout << "FAIL: Probe 泄漏，live=" << Probe::live << "\n";
            std::abort();
        }
        std::cout << "PASS 所有 Probe 均已析构，无泄漏\n";
    }

    std::cout << "\nAll test done\n";
    return 0;
}