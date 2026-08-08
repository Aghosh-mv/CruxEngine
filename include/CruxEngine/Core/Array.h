#pragma once

#include "Core/Types.h"
#include <cstdlib>
#include <cstring>
#include <new>

namespace Crux {

template<typename T>
class Array {
    T* data_ = nullptr;
    usize size_ = 0;

public:
    Array() = default;
    
    explicit Array(usize n) : size_(n) {
        data_ = (T*)malloc(n * sizeof(T));
        for(usize i = 0; i < n; i++) {
            new(&data_[i]) T();
        }
    }
    
    Array(usize n, const T& value) : size_(n) {
        data_ = (T*)malloc(n * sizeof(T));
        for(usize i = 0; i < n; i++) {
            new(&data_[i]) T(value);
        }
    }
    
    Array(const Array& other) : size_(other.size_) {
        data_ = (T*)malloc(size_ * sizeof(T));
        for(usize i = 0; i < size_; i++) {
            new(&data_[i]) T(other.data_[i]);
        }
    }
    
    Array(Array&& other) noexcept : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }
    
    Array(std::initializer_list<T> init) : size_(init.size()) {
        data_ = (T*)malloc(size_ * sizeof(T));
        usize i = 0;
        for(const auto& elem : init) {
            new(&data_[i++]) T(elem);
        }
    }
    
    ~Array() {
        for(usize i = 0; i < size_; i++) data_[i].~T();
        free(data_);
    }
    
    Array& operator=(const Array& other) {
        if(this != &other) {
            for(usize i = 0; i < size_; i++) data_[i].~T();
            free(data_);
            size_ = other.size_;
            data_ = (T*)malloc(size_ * sizeof(T));
            for(usize i = 0; i < size_; i++) {
                new(&data_[i]) T(other.data_[i]);
            }
        }
        return *this;
    }
    
    Array& operator=(Array&& other) noexcept {
        for(usize i = 0; i < size_; i++) data_[i].~T();
        free(data_);
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
        return *this;
    }
    
    T& operator[](usize i) { return data_[i]; }
    const T& operator[](usize i) const { return data_[i]; }
    
    T* data() { return data_; }
    const T* data() const { return data_; }
    usize size() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }
};

}