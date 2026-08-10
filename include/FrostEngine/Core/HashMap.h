#pragma once

#include "Core/Types.h"
#include "Core/Vector.h"
#include "Core/String.h"
#include <utility>
#include <functional>

namespace Frost {

template<typename T> struct KeyHash {
    u32 operator()(const T& key) const { return std::hash<T>{}(key); }
};

template<> struct KeyHash<String> {
    u32 operator()(const String& key) const {
        u32 h = 2166136261u;
        for (u32 i = 0; i < key.length(); ++i) {
            h ^= (u8)key[i];
            h *= 16777619u;
        }
        return h;
    }
};

template<> struct KeyHash<uint64_t> {
    u32 operator()(uint64_t key) const {
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return (u32)(key ^ (key >> 32));
    }
};

template<typename K, typename V>
class HashMap {
    struct Entry {
        K key;
        V value;
        bool occupied = false;
        bool deleted = false;
    };

    Vector<Entry> buckets_;
    u32 size_ = 0;
    static constexpr f32 MAX_LOAD_FACTOR = 0.75f;

    u32 hash(const K& key) const {
        return KeyHash<K>{}(key) & (buckets_.size() - 1);
    }

    void rehash() {
        u32 newCap = buckets_.size() == 0 ? 16 : buckets_.size() * 2;
        Vector<Entry> newBuckets;
        newBuckets.resize(newCap);
        
        for (const auto& entry : buckets_) {
            if (entry.occupied && !entry.deleted) {
                u32 idx = KeyHash<K>{}(entry.key) & (newCap - 1);
                while (newBuckets[idx].occupied) {
                    idx = (idx + 1) & (newCap - 1);
                }
                newBuckets[idx] = entry;
            }
        }
        buckets_ = std::move(newBuckets);
    }

    u32 findIndex(const K& key) const {
        if (buckets_.empty()) return buckets_.size();
        u32 idx = hash(key);
        u32 start = idx;
        while (true) {
            const Entry& entry = buckets_[idx];
            if (!entry.occupied) return buckets_.size();
            if (!entry.deleted && entry.key == key) return idx;
            idx = (idx + 1) & (buckets_.size() - 1);
            if (idx == start) break;
        }
        return buckets_.size();
    }

    u32 insertIndex(const K& key) {
        if ((f32)size_ / buckets_.size() >= MAX_LOAD_FACTOR) rehash();
        
        u32 idx = hash(key);
        while (true) {
            Entry& entry = buckets_[idx];
            if (!entry.occupied || entry.deleted) {
                return idx;
            } else if (entry.key == key) {
                return idx;
            }
            idx = (idx + 1) & (buckets_.size() - 1);
        }
    }

public:
    class Iterator {
        HashMap* map_;
        u32 idx_;
        
    public:
        Iterator(HashMap* map, u32 idx) : map_(map), idx_(idx) {
            advance();
        }
        
        void advance() {
            while (idx_ < map_->buckets_.size() && 
                   (!map_->buckets_[idx_].occupied || map_->buckets_[idx_].deleted)) {
                idx_++;
            }
        }
        
        bool operator==(const Iterator& other) const { return idx_ == other.idx_; }
        bool operator!=(const Iterator& other) const { return idx_ != other.idx_; }
        Iterator& operator++() { idx_++; advance(); return *this; }
        
        const std::pair<const K, V> operator*() const {
            return {map_->buckets_[idx_].key, map_->buckets_[idx_].value};
        }
        
        const K& key() const { return map_->buckets_[idx_].key; }
        V& value() { return map_->buckets_[idx_].value; }
        const V& value() const { return map_->buckets_[idx_].value; }
    };

    class ConstIterator {
        const HashMap* map_;
        u32 idx_;
        
    public:
        ConstIterator(const HashMap* map, u32 idx) : map_(map), idx_(idx) {
            advance();
        }
        
        void advance() {
            while (idx_ < map_->buckets_.size() && 
                   (!map_->buckets_[idx_].occupied || map_->buckets_[idx_].deleted)) {
                idx_++;
            }
        }
        
        bool operator==(const ConstIterator& other) const { return idx_ == other.idx_; }
        bool operator!=(const ConstIterator& other) const { return idx_ != other.idx_; }
        ConstIterator& operator++() { idx_++; advance(); return *this; }
        
        const std::pair<const K, V> operator*() const {
            return {map_->buckets_[idx_].key, map_->buckets_[idx_].value};
        }
        
        const K& key() const { return map_->buckets_[idx_].key; }
        const V& value() const { return map_->buckets_[idx_].value; }
    };

    HashMap() { buckets_.resize(16); }

    u32 size() const { return size_; }
    bool empty() const { return size_ == 0; }

    V& operator[](const K& key) {
        u32 idx = insertIndex(key);
        Entry& entry = buckets_[idx];
        if (!entry.occupied) {
            entry.key = key;
            entry.value = V{};
            entry.occupied = true;
            entry.deleted = false;
            size_++;
        } else if (entry.deleted) {
            entry.key = key;
            entry.value = V{};
            entry.deleted = false;
            size_++;
        }
        return entry.value;
    }

    Iterator find(const K& key) {
        u32 idx = findIndex(key);
        return Iterator(this, idx);
    }

    ConstIterator find(const K& key) const {
        u32 idx = findIndex(key);
        return ConstIterator(this, idx);
    }

    Iterator begin() { return Iterator(this, 0); }
    Iterator end() { return Iterator(this, buckets_.size()); }
    ConstIterator begin() const { return ConstIterator(this, 0); }
    ConstIterator end() const { return ConstIterator(this, buckets_.size()); }

    bool contains(const K& key) const {
        return findIndex(key) != buckets_.size();
    }

    bool erase(const K& key) {
        u32 idx = findIndex(key);
        if (idx == buckets_.size()) return false;
        buckets_[idx].deleted = true;
        buckets_[idx].occupied = false;
        size_--;
        return true;
    }

    void clear() {
        for (auto& entry : buckets_) {
            entry.occupied = false;
            entry.deleted = false;
        }
        size_ = 0;
    }
};

}