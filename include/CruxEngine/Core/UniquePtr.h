#pragma once

#include "Core/Types.h"
#include <cstdlib>
#include <new>
#include <algorithm>

namespace Crux {

template<typename T>
struct DefaultDeleter {
    void operator()(T* ptr) const { 
        if constexpr(std::is_array_v<T>) {
            delete[] ptr;
        } else {
            delete ptr;
        }
    }
};

template<typename T>
struct DefaultDeleter<T[]> {
    void operator()(T* ptr) const { delete[] ptr; }
};

template<typename T, typename Deleter = DefaultDeleter<T>>
class UniquePtr {
    T* ptr_ = nullptr;
    
public:
    UniquePtr() = default;
    
    explicit UniquePtr(T* p) : ptr_(p) {}
    
    ~UniquePtr() { 
        if(ptr_) Deleter()(ptr_); 
    }
    
    UniquePtr(UniquePtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    
    UniquePtr& operator=(UniquePtr&& other) noexcept {
        if(this != &other) {
            if(ptr_) Deleter()(ptr_);
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }
    
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;
    
    T* get() const { return ptr_; }
    T* operator->() const { return ptr_; }
    T& operator*() const { return *ptr_; }
    
    explicit operator bool() const { return ptr_ != nullptr; }
    
    void reset(T* p = nullptr) {
        if(ptr_) Deleter()(ptr_);
        ptr_ = p;
    }
    
    T* release() {
        T* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }
    
    void swap(UniquePtr& other) {
        T* tmp = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = tmp;
    }
    
    static UniquePtr make(Args&&... args) {
        return UniquePtr<T, Deleter>(new T(args...));
    }
    
    template<typename... Args>
    static UniquePtr make(Args&&... args) {
        return UniquePtr<T, Deleter>(new T(std::forward<Args>(args)...));
    }
};

template<typename T, typename... Args>
UniquePtr<T> MakeUnique(Args&&... args) {
    return UniquePtr<T>(new T(std::forward<Args>(args)...));
}

template<typename T>
class UniquePtr<T[]> {
    T* ptr_ = nullptr;
    
public:
    UniquePtr() = default;
    explicit UniquePtr(T* p) : ptr_(p) {}
    
    ~UniquePtr() { delete[] ptr_; }
    
    UniquePtr(UniquePtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    UniquePtr& operator=(UniquePtr&& other) noexcept {
        delete[] ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
        return *this;
    }
    
    UniquePtr(const UniquePtr&) = delete;
    UniquePtr& operator=(const UniquePtr&) = delete;
    
    T* get() const { return ptr_; }
    T& operator[](usize i) const { return ptr_[i]; }
    explicit operator bool() const { return ptr_ != nullptr; }
    
    void reset(T* p = nullptr) { delete[] ptr_; ptr_ = p; }
    T* release() { T* t = ptr_; ptr_ = nullptr; return t; }
};

}