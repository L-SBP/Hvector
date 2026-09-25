#ifndef HVECTOR_HPP
#define HVECTOR_HPP
#include <new>
#include <utility>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>

template <class T>
class Hvector {
public:
    using value_type             = T;
    using allocator_type         = std::allocator<T>;
    using size_type              = std::size_t;
    using difference_type        = std::ptrdiff_t;
    using reference              = value_type&;
    using const_reference        = const value_type&;
    using pointer                = typename std::allocator_traits<allocator_type>::pointer;
    using const_pointer          = typename std::allocator_traits<allocator_type>::const_pointer;
    using iterator               = T*;
    using const_iterator         = const T*;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    Hvector():size_(0), capacity_(0), data_(nullptr) {}

    explicit Hvector(size_t cap):size_(cap), capacity_(cap) {
        data_  = static_cast<T*>(::operator new(cap * sizeof(T)));
        size_t constructed = 0;
        try {
            for(;constructed < cap;++constructed) {
                new (data_ + constructed) T();
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                data_[i].~T();
            }
            ::operator delete(data_);
            throw;
        }
    }

    Hvector(size_t cap, const T& val): data_(nullptr), size_(cap), capacity_(cap) {
        if(cap == 0)    return ;
        data_ = static_cast<T*>(::operator new(cap * sizeof(T)));
        size_t constructed = 0;
        try {
            for(;constructed < cap;++constructed) {
                new (data_ + constructed) T(val);
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                data_[i].~T();
            }
            ::operator delete(data_);
            data_ = nullptr;
            size_ = capacity_ = 0;
            throw;
        }
    }

    Hvector(std::initializer_list<T> li): data_(nullptr), size_(0), capacity_(0) {
        size_t n = li.size();
        if(n == 0)  return ;
        data_ = static_cast<T*>(::operator new(n * sizeof(T)));
        size_t constructed = 0;
        try {
            for(auto it = li.begin();it != li.end();++it, ++constructed) {
                new (data_ + constructed) T(*it);
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                data_[i].~T();
            }
            ::operator delete(data_);
            data_ = nullptr;
            throw;
        }
        capacity_ = n;
        size_ = n;
    }

    Hvector(iterator first, iterator last): data_(nullptr), size_(0), capacity_(0) {
        size_t n = static_cast<size_t> (last - first);
        if(n == 0)  return ;
        data_ = static_cast<T*>(::operator new(n * sizeof(T)));
        size_t constructed = 0;
        try {
            for(auto it = first; it != last;++it, ++constructed) {
                new (data_ + constructed) T(*it);
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                data_[i].~T();
            }
            ::operator delete(data_);
            data_ = nullptr;
            throw;
        }
        capacity_ = n;
        size_ = n;
    }

    Hvector(const Hvector& other) {
        data_ = static_cast<T*>(::operator new(other.capacity_ * sizeof(T)));
        size_ = other.size_;
        capacity_ = other.capacity_;
        size_t constructed = 0;
        try {
            for(;constructed < size_;++constructed) {
                new (data_ + constructed) T(other.data_[constructed]);
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                data_[i].~T();
            }
            ::operator delete(data_);
            throw;
        }
    }

    Hvector(Hvector&& other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    ~Hvector() {
        for(size_t i = 0;i < size_;++i) {
            data_[i].~T();
        }
        ::operator delete(data_);
    }

    // 复制
    Hvector& operator=(const Hvector& other) {
        if(this == &other) { return *this; }

        if(capacity_ < other.size_) {
            T* new_data = static_cast<T*>(::operator new(other.capacity_ * sizeof(T)));
            size_t constructed = 0;
            try {
                for(;constructed < other.size_; ++constructed) {
                    new (new_data + constructed) T(other.data_[constructed]);
                }
            } catch (...) {
                for(size_t i = 0;i < constructed;++i) {
                    new_data[i].~T();
                }
                ::operator delete(new_data);
                throw;
            }
            _destruct_data();
            ::operator delete(data_);

            data_ = new_data;
            size_ = other.size_;
            capacity_ = other.capacity_;
        } else {
            size_t common = size_ < other.size_ ? size_ : other.size_;
            for(size_t i = 0;i < common;++i)    data_[i] = other.data_[i];
            for(size_t i = common; i < other.size_; ++i) new (data_ + i) T(other.data_[i]);
            for(size_t i = other.size_; i < size_; ++i) data_[i].~T();

            size_ = other.size_;
        }
        return *this;
    }

    void swap(Hvector& other) {
        std::swap(other.data_, data_);
        std::swap(other.size_, size_);
        std::swap(other.capacity_, capacity_);
    }

    // Hvector& operator=(const Hvector& other) noexcept {
    //     Hvector tmp(other);
    //     swap(tmp);
    //     return *this;
    // }

    Hvector& operator=(Hvector&& other) noexcept {
        if(this == &other)  return *this;

        _destruct_data();
        ::operator delete(data_);

        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;

        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }

    bool operator==(const Hvector& other) const {
        if(size_ != other.size_)  return false;
        for(size_t i = 0;i < size_;++i) {
            if(data_[i] != other.data_[i])   return false;
        }
        return true;
    }
    
    T& operator[](size_t idx) { return data_[idx]; }
    const T& operator[](size_t idx) const { return data_[idx]; }

    size_t size() { return size_; }
    size_t size() const { return size_; }

    size_t capacity() { return capacity_; }
    size_t capacity() const { return capacity_; }

    template<typename... Args>
    void emplace_back(Args&&... args) {
        if(size_ >= capacity_) {
            _insert_with_strong_guarantee(size_, std::forward<Args>(args)...);
        } else {
            new (data_ + size_) T(std::forward<Args>(args)...);
            size_++;
        }
    }

    void push_back(const T& x) { emplace_back(x); }
    void push_back(T&& x) { emplace_back(std::move(x)); }
    
    void pop_back() {
        if(size_ == 0) {
            throw std::out_of_range("The contaner is empty");
        }
        data_[size_-1].~T();
        size_--;
    }

    bool empty() { return size_ == 0; }
    bool empty() const { return size_ == 0; }

    void reserve(size_t new_capacity) {
        if(new_capacity > capacity_) {
            _reallocator(new_capacity);
        }
    }

    void resize(size_t new_size) {
        if(new_size > capacity_) {
            reserve(std::max(new_size, capacity_ * 2));
        }

        if(new_size > size_) {
            size_t constructed = size_;
            try {
                for(;constructed < new_size;++constructed) {
                    new (data_ + constructed) T();
                }
            } catch (...) {
                for(size_t i = size_;i < constructed;++i) {
                    data_[i].~T();
                }
                throw;
            }
        } else {
            for(size_t i = new_size;i < size_;++i) {
                data_[i].~T();
            }
        }
        size_ = new_size;
    }

    void resize(size_t new_size, const T& val) {
        if(new_size > capacity_)    reserve(std::max(new_size, capacity_ * 2));
        if(new_size > size_) {
            size_t constructed = size_;
            try {
                for(;constructed < new_size;++constructed) {
                    new (data_ + constructed) T(val);
                }
            } catch (...) {
                for(size_t i = size_;i < constructed;++i) {
                    data_[i].~T();
                }
                throw;
            }
        } else {
            for(size_t i = new_size;i < size_;++i) {
                data_[i].~T();
            }
        }
        size_ = new_size;
    }

    void assign(size_t count, const T& val) {
        _assign_impl(count, val);
    }

    void assign(size_t count, T&& val) {
        T value(std::move(val));
        _assign_impl(count, value);
    }

    void shrink_to_fit() {
        if(size_ == capacity_)  return ;
        if(size_ == 0) {
            _destruct_data();
            ::operator delete(data_);
            data_ = nullptr;
            capacity_ = 0;
            return;
        }
        size_t new_cap = size_;
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));
        
        size_t constructed = 0;
        try {
            for(;constructed < new_cap;++constructed) {
                new (new_data + constructed) T(std::move_if_noexcept(data_[constructed]));
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                new_data[i].~T();
            }
            ::operator delete(new_data);
            throw;
        }
        _destruct_data();
        ::operator delete(data_);
        data_ = new_data;
        capacity_ = new_cap;
    }

    void clear() {
        _destruct_data();
        size_ = 0;
    }

    T& at(size_t idx) {
        if(idx >= size_) {
            throw std::out_of_range("idx out of range");
        }
        return data_[idx];
    }

    const T& at(size_t idx) const {
        if(idx >= size_) {
            throw std::out_of_range("idx out of range");
        }
        return data_[idx];
    }

    T& front() {
        if(size_ == 0) {
            throw std::out_of_range("The contaner is empty");
        }
        return data_[0];
    }

    const T& front() const {
        if(size_ == 0) {
            throw std::out_of_range("The contaner is empty");
        }
        return data_[0];
    }

    T& back() {
        if(size_ == 0) {
            throw std::out_of_range("The contaner is empty");
        }
        return data_[size_-1];
    }

    const T& back() const {
        if(size_ == 0) {
            throw std::out_of_range("The contaner is empty");
        }
        return data_[size_-1];
    }

    T* data() { return data_; }
    const T* data() const {return data_; }

    iterator begin() { return data_; }
    const_iterator begin() const { return data_; }

    iterator end() { return data_ + size_; }
    const_iterator end() const { return data_ + size_; }

    iterator insert(size_t idx, const T& val) { 
        return _insert_impl(this->begin() + idx, val);
    }

    iterator insert(size_t idx, T&& val) {
        return _insert_impl(this->begin() + idx, std::move(val));
    }

    iterator insert(iterator it, const T& val) {
        return _insert_impl(it, val);
    }

    iterator insert(iterator it, T&& val) {
        return _insert_impl(it, std::move(val));
    }

    template<typename... Args>
    iterator emplace(size_t idx, Args&& ... args) {
        if(idx > size_) {
            throw std::out_of_range("The index out of range");
        }
        // 先把新元素物化成一个临时对象，再走统一的插入流程：
        //   * 构造本身抛异常 -> 容器完全没动（强保证）
        //   * 顺带解决 args 引用到容器内部元素时的别名问题
        //     （否则搬移会把被引用的元素改掉）
        T value(std::forward<Args>(args)...);
        return _insert_impl(this->begin() + idx, std::move(value));
    }
    
    iterator erase(size_t idx) {
        return _erase_impl(this->begin() + idx, this->begin() + idx + 1);
    }

    iterator erase(iterator it) {
        return _erase_impl(it, it+1);
    }

    iterator erase(iterator first, iterator last) {
        return _erase_impl(first, last);
    }

private:
    size_t _index(iterator it) {
        return static_cast<size_t> (it - this->begin());
    }
    void _reallocator(size_t new_capacity) {
        if(!new_capacity)  new_capacity = 1;
        T* new_data = static_cast<T*>(::operator new (new_capacity * sizeof(T)));
        
        size_t constructed = 0;
        try {
            for(constructed = 0;constructed < size_;++constructed) {
                new (new_data + constructed) T(std::move_if_noexcept(data_[constructed]));
            }
        } catch (...) {
            for(size_t i = 0;i < constructed;++i) {
                new_data[i].~T();
            }
            ::operator delete (new_data);
            throw;
        }
        
        _destruct_data();
        ::operator delete(data_);

        data_ = new_data;
        capacity_ = new_capacity;
    }

    void _destruct_data() {
        for(size_t i = 0;i < size_;++i) {
            data_[i].~T();
        }
    }

    // 容量富余时的就地插入：把 [idx, size_) 整体后移一格，再把新值写入 idx。
    //
    // 旧版 _shift_elements_backward 是"移动一个、析构一个"（uninitialized_move 之后
    // 立刻 p->~T()）。中途抛异常时会同时留下两处烂摊子：
    //   * 被提前析构的源槽位 —— 它在 [0, size_) 之内，容器声称有活对象，实际已是死槽位；
    //   * 已经构造好、但还没有计入 size_ 的对象 —— 落在 size_ 之外，析构函数永远不会碰它。
    // 于是既有二次析构（析构函数又去析构那个死槽位），又有资源泄漏。
    //
    // 正确的做法是"构造 + 赋值"而不是"移动 + 析构"：
    //   1) 先把实参落成一个独立的临时对象（别名安全，见下）；
    //   2) 在最末尾的后继槽位构造出多出来的那个元素（此刻容器还没变）；
    //   3) 立刻 ++size_ 把它纳入管理，此后 [0, size_) 始终全是活对象；
    //   4) 从后往前用移动赋值把 [idx, size_-2] 搬到 [idx+1, size_-1]；
    //   5) 把临时对象移动赋值进 idx。
    // 异常保证：第 1/2 步抛 -> 容器完全没变（强保证）；
    //          第 4/5 步抛 -> 容器仍有效、无死槽位、无孤儿对象（基本保证）。
    //
    // 第 1 步是为了别名：实参可能引用容器自己的元素（v.insert(0, v[2])）。
    // 第 4 步会把下标 [idx, size_) 上的元素整体移动一格，被引用的那个对象会先被覆盖掉，
    // 于是第 5 步赋进去的是一个已经被搬走的值——同一个调用会因为容量够不够（走这条路径
    // 还是扩容路径）而给出不同的答案。先落成临时对象之后，搬移怎么改都读不到它了。
    // 代价是每次非扩容插入多一次移动构造 + 一次移动赋值；尾部追加走下面的早退分支，
    // 不付这个代价。对只移类型（unique_ptr）走移动，对只拷贝类型退化成拷贝。
    //
    // 设计取舍：这条路径用的是**移动赋值**，所以要求 T 可移动赋值；
    // 旧版的 uninitialized_move + 显式析构只要求可移动构造（见 README 第 11 节）。
    template<typename U>
    void _insert_no_realloc(size_t idx, U&& val) {
        if(idx == size_) {                        // 尾部追加，根本不需要搬移，也不会有别名问题
            new (data_ + size_) T(std::forward<U>(val));
            ++size_;
            return;
        }

        T value(std::forward<U>(val));

        new (data_ + size_) T(std::move_if_noexcept(data_[size_ - 1]));
        ++size_;
        for(size_t i = size_ - 2; i > idx; --i) {
            data_[i] = std::move_if_noexcept(data_[i - 1]);
        }
        data_[idx] = std::move(value);
    }

    template<typename U>
    iterator _insert_impl(iterator pos, U&& val) {
        size_t idx = _index(pos);
        if(idx > size_) {
            throw std::out_of_range("The index out of range");
        }
        if(size_ == capacity_) {
            _insert_with_strong_guarantee(idx, std::forward<U>(val));
        } else {
            _insert_no_realloc(idx, std::forward<U>(val));
        }
        return data_ + idx;
    }

    // 需要扩容时的插入（emplace_back / push_back / insert / emplace 都汇到这里）：
    // 先在【新缓冲区】里把 size_ + 1 个元素全部构造好，全部成功之后才析构旧缓冲区、换指针。
    //   * 构造期间旧缓冲区和它上面的元素完全没被动过；
    //   * 中途任何一步抛异常 -> 析构新缓冲区里已构造的部分、释放新缓冲区、重抛，
    //     原容器保持不变（强异常安全保证），也不会留下"已析构的槽位"或"size_ 之外的孤儿对象"。
    //
    // 构造顺序：**先构造新元素，再搬移旧元素**。
    // 这个顺序不是随便定的：实参可能引用容器自己的元素（v.push_back(v[0])、v.insert(2, v[0])）。
    // 若先搬移，被引用的那个对象就已经被 move 走了，新元素会从 moved-from 状态构造出来——
    // 值悄悄错掉。std::string 的移动构造是 noexcept，move_if_noexcept 会选移动，
    // 于是复制出来的是一个空串（旧版这里更糟：先 _reallocator 把旧缓冲区 delete 掉再读实参，
    // 那是真的 use-after-free）。先构造新元素时旧缓冲区还没被触碰，别名因此是安全的。
    // libstdc++ 的 _M_realloc_insert 用的也是这个顺序。
    //
    // 注意 emplace_back 走这里时 idx == size_，即"新元素在最后"，别名元素一定在它前面，
    // 所以这处顺序正是 11.1 的关键。
    template<typename... Args>
    void _insert_with_strong_guarantee_impl(size_t idx, Args&&... args) {
        size_t new_cap = capacity_ == 0 ? 1 : capacity_ * 2;
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));

        // 已构造的槽位不再是一段前缀 [0, constructed)：新元素落在 idx 上，
        // 另外两段分列两侧。用三个计数分别记录，catch 里才能只析构真正构造过的东西。
        size_t head_done = 0;                 // [0, head_done) 已构造
        bool   mid_done  = false;             // {idx} 已构造
        size_t tail_done = 0;                 // [idx+1, idx+1+tail_done) 已构造
        try {
            new (new_data + idx) T(std::forward<Args>(args)...);
            mid_done = true;
            for (; head_done < idx; ++head_done) {
                new (new_data + head_done)
                    T(std::move_if_noexcept(data_[head_done]));
            }
            for (; tail_done < size_ - idx; ++tail_done) {
                new (new_data + idx + 1 + tail_done)
                    T(std::move_if_noexcept(data_[idx + tail_done]));
            }
        } catch (...) {
            for (size_t i = 0; i < tail_done; ++i) {
                new_data[idx + 1 + i].~T();
            }
            for (size_t i = 0; i < head_done; ++i) {
                new_data[i].~T();
            }
            if (mid_done) {
                new_data[idx].~T();
            }
            ::operator delete(new_data);
            throw;
        }

        _destruct_data();
        ::operator delete(data_);
        data_ = new_data;
        capacity_ = new_cap;
        ++size_;
    }

    // 0 参（emplace_back() 默认构造）、1 参（push_back / insert 转发过来的引用）、
    // N 参变参都走这一个重载。原来还有一个 template<typename U> 的单参版本，
    // 函数体和这个一字不差；两份副本只要有一份忘了跟着改，就会出现"只修一半"。
    template<typename... Args>
    void _insert_with_strong_guarantee(size_t idx, Args&&... args) {
        _insert_with_strong_guarantee_impl(idx, std::forward<Args>(args)...);
    }

    iterator _erase_impl(iterator first, iterator last) {
        size_t fidx = _index(first),
               lidx = _index(last);
        if(size_ == 0 || fidx > size_ || lidx > size_|| lidx < fidx) {
            throw std::out_of_range("The iterator has error");
        }
        if(fidx == size_)   return first;

        size_t new_size = size_ - (lidx - fidx);

        // 这里**故意没有** try/catch。
        // std::move 的三参版本是逐个元素的【移动赋值】，它不析构任何对象、也不构造任何对象，
        // 所以中途抛异常时：所有 [0, size_) 的槽位仍然都是活对象，size_ 也还是原值，
        // 容器处于"有效但内容未指定"的状态——这正是 erase 的基本保证，异常直接往外传即可。
        //
        // 曾经这里有一个 catch，试图"修补"成搬移后的样子：它先析构 data_[p]、
        // 再用 uninitialized_move 从 data_[q] 重建，最后析构 [new_size, size_) 的尾部。
        // 但那套修补是给上一版"移动一个、析构一个"的 _shift_elements_backward 写的；
        // 现在没有任何槽位被提前析构，修补反而把 size_ 之外的槽位析构掉了，
        // 而 size_ 因为异常要继续往外传并没有跟着改小 —— 结果就是 size_ 之内留下死槽位，
        // 作用域结束时 ~Hvector() 会对它们二次析构（实测 live 计数会变成负数）。
        // 一句话：内容对不代表状态有效，别在 catch 里做没必要的修补。
        std::move(last, end(), first);
        for(size_t i = new_size; i < size_; ++i) {
            data_[i].~T();
        }
        size_ = new_size;
        return data_ + fidx;
    }

    template<typename U>
    void _assign_impl(size_t count, U&& val) {
        if(count == 0) {
            _destruct_data();
            size_ = 0;
            return;
        }

        size_t new_capacity = std::max(count, capacity_);
        T* new_data = static_cast<T*>(::operator new(new_capacity * sizeof(T)));
        size_t constructed = 0;
        try {
            for(;constructed < count;++constructed) {
                new (new_data + constructed) T(std::forward<U>(val));
            }
        } catch(...) {
            for(size_t i = 0;i < constructed;++i) {
                new_data[i].~T();
            }
            ::operator delete(new_data);
            throw;
        }

        _destruct_data();
        ::operator delete(data_);
        data_ = new_data;
        size_ = count;
        capacity_ = new_capacity;
    }

    T* data_;
    size_t size_;
    size_t capacity_;
};

template<typename T>
void print_vec(const char* name, const Hvector<T>& v) {
    std::cout << name << " size=" << v.size() << ", cap=" << v.capacity() << " : ";
    for (auto x : v) {
        std::cout << x << " ";
    }
    std::cout << "\n";
}

template<class V>
void expect_vec(const char* name, const V& v, std::initializer_list<int> expect) {
    if (v.size() != expect.size()) {
        std::cout << "FAIL " << name << ": size=" << v.size()
                  << " expect=" << expect.size() << "\n";
        std::abort();
    }
    size_t i = 0;
    for (int e : expect) {
        if (v[i] != e) {
            std::cout << "FAIL " << name << ": v[" << i << "]=" << v[i]
                      << " expect=" << e << "\n";
            std::abort();
        }
        ++i;
    }
    std::cout << "PASS " << name << ": ";
    print_vec(name, v);
}
#endif // HVECTOR_HPP