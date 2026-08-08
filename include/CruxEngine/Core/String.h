#pragma once

#include "Core/Types.h"
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <initializer_list>

namespace Crux {

struct String {
private:
    char* data_ = nullptr;
    usize size_ = 0;
    usize capacity_ = 0;

public:
    String() = default;
    
    explicit String(usize n, char c = '\0') {
        resize(n, c);
    }
    
    String(const char* cstr) {
        if(cstr) {
            size_ = strlen(cstr);
            capacity_ = size_ + 1;
            data_ = (char*)malloc(capacity_);
            memcpy(data_, cstr, capacity_);
        }
    }
    
    String(const char* data, usize len) {
        size_ = len;
        capacity_ = len + 1;
        data_ = (char*)malloc(capacity_);
        memcpy(data_, data, len);
        data_[len] = '\0';
    }
    
    String(const String& other) {
        if(other.size_ > 0) {
            size_ = other.size_;
            capacity_ = other.capacity_;
            data_ = (char*)malloc(capacity_);
            memcpy(data_, other.data_, capacity_);
        }
    }
    
    String(String&& other) noexcept {
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    
    String& operator=(const String& other) {
        if(this != &other) {
            free(data_);
            size_ = other.size_;
            capacity_ = other.capacity_;
            if(capacity_ > 0) {
                data_ = (char*)malloc(capacity_);
                memcpy(data_, other.data_, capacity_);
            } else {
                data_ = nullptr;
            }
        }
        return *this;
    }
    
    String& operator=(String&& other) noexcept {
        free(data_);
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        return *this;
    }
    
    ~String() { free(data_); }
    
    void resize(usize n, char c = '\0') {
        if(n + 1 > capacity_) {
            capacity_ = n + 1;
            data_ = (char*)realloc(data_, capacity_);
        }
        memset(data_ + size_, c, n - size_);
        size_ = n;
        data_[size_] = '\0';
    }
    
    void reserve(usize n) {
        if(n + 1 > capacity_) {
            capacity_ = n + 1;
            char* newData = (char*)malloc(capacity_);
            if(data_) {
                memcpy(newData, data_, size_ + 1);
                free(data_);
            }
            data_ = newData;
        }
    }
    
    void append(const char* cstr) {
        if(cstr) {
            usize len = strlen(cstr);
            usize newSize = size_ + len;
            if(newSize + 1 > capacity_) {
                capacity_ = newSize + 1;
                data_ = (char*)realloc(data_, capacity_);
            }
            memcpy(data_ + size_, cstr, len + 1);
            size_ = newSize;
        }
    }
    
    void append(const String& s) {
        usize newSize = size_ + s.size_;
        if(newSize + 1 > capacity_) {
            capacity_ = newSize + 1;
            data_ = (char*)realloc(data_, capacity_);
        }
        memcpy(data_ + size_, s.data_, s.size_ + 1);
        size_ = newSize;
    }
    
    char& operator[](usize i) { return data_[i]; }
    char operator[](usize i) const { return data_[i]; }
    
    char* c_str() { return data_; }
    const char* c_str() const { return data_; }
    
    usize size() const { return size_; }
    usize length() const { return size_; }
    usize capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    
    bool startsWith(const char* prefix) const {
        usize len = strlen(prefix);
        if(len > size_) return false;
        return strncmp(data_, prefix, len) == 0;
    }
    
    bool endsWith(const char* suffix) const {
        usize len = strlen(suffix);
        if(len > size_) return false;
        return strncmp(data_ + size_ - len, suffix, len) == 0;
    }
    
    usize find(char c, usize start = 0) const {
        for(usize i = start; i < size_; i++) {
            if(data_[i] == c) return i;
        }
        return usize(-1);
    }
    
    usize find(const char* sub, usize start = 0) const {
        if(!sub || !data_) return usize(-1);
        usize subLen = strlen(sub);
        if(subLen > size_) return usize(-1);
        for(usize i = start; i <= size_ - subLen; i++) {
            if(strncmp(data_ + i, sub, subLen) == 0) return i;
        }
        return usize(-1);
    }
    
    String substr(usize start, usize len = usize(-1)) const {
        usize actualLen = (len == usize(-1)) ? size_ - start : len;
        if(start >= size_) return String();
        actualLen = (actualLen > size_ - start) ? size_ - start : actualLen;
        return String(data_ + start, actualLen);
    }
    
    void format(const char* fmt, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        
        *this = buffer;
    }
    
    static String fromInt(i64 value, i32 base = 10) {
        char buffer[64];
        if(base == 10) {
            snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
        } else if(base == 16) {
            snprintf(buffer, sizeof(buffer), "%llx", (unsigned long long)value);
        } else if(base == 2) {
            char* p = buffer + sizeof(buffer) - 1;
            *p = '\0';
            if(value == 0) *--p = '0';
            unsigned long long v = (unsigned long long)value;
            while(v > 0) { *--p = (char)('0' + (v & 1)); v >>= 1; }
            return p;
        } else if(base == 8) {
            snprintf(buffer, sizeof(buffer), "%llo", (unsigned long long)value);
        }
        return buffer;
    }
    
    static String fromFloat(f64 value, i32 precision = 6) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
        return buffer;
    }
    
    String operator+(const String& s) const {
        String result(*this);
        result.append(s);
        return result;
    }
    
    bool operator==(const char* cstr) const {
        if(!data_ || !cstr) return data_ == cstr;
        return strcmp(data_, cstr) == 0;
    }
    
    bool operator==(const String& s) const {
        if(size_ != s.size_) return false;
        return size_ == 0 || strcmp(data_, s.data_) == 0;
    }
    
    bool operator!=(const String& s) const { return !(*this == s); }
    
    operator bool() const { return !empty(); }
};

inline String operator+(const char* a, const String& b) {
    return String(a) + b;
}

}