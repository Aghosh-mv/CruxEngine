#pragma once

#include "Core/Types.h"
#include <cstdlib>
#include <cstring>
#include <new>
#include <algorithm>
#include <stdexcept>

namespace Frost {

template<typename T>
class Vector {
    T* data_ = nullptr;
    usize size_ = 0;
    usize capacity_ = 0;

public:
    Vector() = default;
    
    explicit Vector(usize n, const T& value = T{}) {
        reserve(n);
        for(usize i = 0; i < n; i++) {
            push_back(value);
        }
    }
    
    Vector(const Vector& other) {
        reserve(other.size_);
        for(const auto& elem : other) {
            push_back(elem);
        }
    }
    
    Vector(Vector&& other) noexcept 
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    Vector(std::initializer_list<T> init) {
        reserve(init.size());
        for(const auto& elem : init) {
            push_back(elem);
        }
    }

    template<typename Iter>
    Vector(Iter first, Iter last) {
        for (Iter it = first; it != last; ++it) push_back(*it);
    }
    
    ~Vector() { 
        for(usize i = 0; i < size_; i++) data_[i].~T();
        free(data_); 
    }
    
    Vector& operator=(const Vector& other) {
        if(this != &other) {
            clear();
            reserve(other.size_);
            for(const auto& elem : other) {
                push_back(elem);
            }
        }
        return *this;
    }
    
    Vector& operator=(Vector&& other) noexcept {
        for(usize i = 0; i < size_; i++) data_[i].~T();
        free(data_);
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }
    
    void reserve(usize newCap) {
        if(newCap > capacity_) {
            T* newData = (T*)malloc(newCap * sizeof(T));
            for(usize i = 0; i < size_; i++) {
                new(&newData[i]) T(std::move(data_[i]));
                data_[i].~T();
            }
            free(data_);
            data_ = newData;
            capacity_ = newCap;
        }
    }
    
    void resize(usize newSize, const T& value = T{}) {
        if(newSize > size_) {
            reserve(newSize);
            for(usize i = size_; i < newSize; i++) {
                new(&data_[i]) T(value);
            }
        } else if(newSize < size_) {
            for(usize i = newSize; i < size_; i++) data_[i].~T();
        }
        size_ = newSize;
    }
    
    void push_back(const T& value) {
        if(size_ >= capacity_) reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        new(&data_[size_]) T(value);
        size_++;
    }
    
    void push_back(T&& value) {
        if(size_ >= capacity_) reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        new(&data_[size_]) T(std::move(value));
        size_++;
    }
    
    void pushBack(const T& value) { push_back(value); }
    void pushBack(T&& value) { push_back(std::move(value)); }
    void popBack() { pop_back(); }
    
    T pop() {
        T val = data_[size_ - 1];
        data_[--size_].~T();
        return val;
    }
    
    void eraseSwap(usize index) {
        if(index >= size_) return;
        data_[index].~T();
        if(index != size_ - 1) {
            new(&data_[index]) T(std::move(data_[size_ - 1]));
            data_[size_ - 1].~T();
        }
        size_--;
    }
    
    template<typename... Args>
    T& emplace_back(Args&&... args) {
        if(size_ >= capacity_) reserve(capacity_ == 0 ? 4 : capacity_ * 2);
        new(&data_[size_]) T(std::forward<Args>(args)...);
        return data_[size_++];
    }
    
    void pop_back() {
        if(size_ > 0) {
            data_[--size_].~T();
        }
    }
    
    void clear() {
        for(usize i = 0; i < size_; i++) data_[i].~T();
        size_ = 0;
    }
    
    T& operator[](usize i) { return data_[i]; }
    const T& operator[](usize i) const { return data_[i]; }
    
    T& at(usize i) { 
        if(i >= size_) throw std::out_of_range("Vector index out of range");
        return data_[i]; 
    }
    const T& at(usize i) const { 
        if(i >= size_) throw std::out_of_range("Vector index out of range");
        return data_[i]; 
    }
    
    T* data() { return data_; }
    const T* data() const { return data_; }
    
    usize size() const { return size_; }
    usize capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    
    T* begin() { return data_; }
    T* end() { return data_ + size_; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + size_; }
    
    T& front() { return data_[0]; }
    T& back() { return data_[size_ - 1]; }
    const T& front() const { return data_[0]; }
    const T& back() const { return data_[size_ - 1]; }
    
    void swap(Vector& other) {
        T* tmpData = data_;
        usize tmpSize = size_;
        usize tmpCap = capacity_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = tmpData;
        other.size_ = tmpSize;
        other.capacity_ = tmpCap;
    }
    
    void erase(usize index) {
        if(index >= size_) return;
        data_[index].~T();
        for(usize i = index; i < size_ - 1; i++) {
            new(&data_[i]) T(std::move(data_[i + 1]));
            data_[i + 1].~T();
        }
        size_--;
    }
    
    void erase(usize first, usize last) {
        if(first >= size_) return;
        usize count = last - first;
        for(usize i = first; i < last; i++) data_[i].~T();
        for(usize i = first; i < size_ - count; i++) {
            new(&data_[i]) T(std::move(data_[i + count]));
            data_[i + count].~T();
        }
        size_ -= count;
    }
};

}