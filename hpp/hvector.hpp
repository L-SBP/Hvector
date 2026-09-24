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
    //   1) 先在最末尾的后继槽位构造出多出来的那个元素（此刻容器还没变）；
    //   2) 立刻 ++size_ 把它纳入管理，此后 [0, size_) 始终全是活对象；
    //   3) 从后往前用移动赋值把 [idx, size_-2] 搬到 [idx+1, size_-1]；
    //   4) 把新值写入 idx。
    // 异常保证：第 1 步抛 -> 容器完全没变（强保证）；
    //          第 3/4 步抛 -> 容器仍有效、无死槽位、无孤儿对象（基本保证）。
    template<typename U>
    void _insert_no_realloc(size_t idx, U&& val) {
        if(idx == size_) {                        // 尾部追加，根本不需要搬移
            new (data_ + size_) T(std::forward<U>(val));
            ++size_;
            return;
        }

        new (data_ + size_) T(std::move_if_noexcept(data_[size_ - 1]));
        ++size_;
        for(size_t i = size_ - 2; i > idx; --i) {
            data_[i] = std::move_if_noexcept(data_[i - 1]);
        }
        data_[idx] = std::forward<U>(val);
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

    template<typename U>
    void _insert_with_strong_guarantee_impl(size_t idx, U&& val) {
        size_t new_cap = capacity_ == 0 ? 1 : capacity_ * 2;
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));

        size_t constructed = 0;
        try {
            for (size_t i = 0; i < idx; ++i, ++constructed) {
                new (new_data + constructed)
                    T(std::move_if_noexcept(data_[i]));
            }
            new (new_data + constructed) T(std::forward<U>(val));
            ++constructed;
            for (size_t i = idx; i < size_; ++i, ++constructed) {
                new (new_data + constructed)
                    T(std::move_if_noexcept(data_[i]));
            }
        } catch (...) {
            for (size_t i = 0; i < constructed; ++i) {
                new_data[i].~T();
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

    template<typename... Args>
    void _insert_with_strong_guarantee_impl(size_t idx, Args&&... args) {
       size_t new_cap = capacity_ == 0 ? 1 : capacity_ * 2;
        T* new_data = static_cast<T*>(::operator new(new_cap * sizeof(T)));

        size_t constructed = 0;
        try {
            for (size_t i = 0; i < idx; ++i, ++constructed) {
                new (new_data + constructed)
                    T(std::move_if_noexcept(data_[i]));
            }
            new (new_data + constructed) T(std::forward<Args>(args)...);
            ++constructed;
            for (size_t i = idx; i < size_; ++i, ++constructed) {
                new (new_data + constructed)
                    T(std::move_if_noexcept(data_[i]));
            }
        } catch (...) {
            for (size_t i = 0; i < constructed; ++i) {
                new_data[i].~T();
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

    template<typename U>
    void _insert_with_strong_guarantee(size_t idx, U&& val) {
        _insert_with_strong_guarantee_impl(idx, std::forward<U>(val));
    }

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
        try {
            std::move(last, end(), first);

            for(size_t i = new_size; i < size_; ++i) {
                data_[i].~T();
            }
            size_ = new_size;
        } catch(...) {
            size_t p = fidx, q = lidx;
            while(q < size_) {
                data_[p].~T();
                std::uninitialized_move(begin() + q, begin() + q + 1, begin() + p);
                ++p;++q;
            }
            for(size_t i = p;i < size_; ++i) {
                data_[i].~T();
            }
            throw;
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