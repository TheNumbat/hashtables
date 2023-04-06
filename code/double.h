#pragma once

#include "base.h"

template<uint64_t LF_, uint64_t DF_>
struct Double {

    static constexpr uint64_t EMPTY = UINT64_MAX;
    static constexpr uint64_t DELETED = UINT64_MAX - 1;
    static constexpr double LF = static_cast<double>(LF_) / 100.0;
    static constexpr double DF = static_cast<double>(DF_) / 100.0;

    Double() {
        size_ = 0;
        capacity = 8;
        deleted_ = 0;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }
    ~Double() { __aligned_free(data); }

    uint64_t hash_to_step(uint64_t hash) {
        // always odd -> coprime to power-of-two capacity -> hits every slot
        return (hash >> 32) | 1;
    }

    // assumes key is not in the map
    void insert(uint64_t key, uint64_t value) {
        if(size_ >= capacity * LF) grow();
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        if(data[index].key < DELETED) {
            uint64_t step = hash_to_step(hash);
            while(data[index].key < DELETED) { index = (index + step) & (capacity - 1); }
        }
        data[index].key = key;
        data[index].value = value;
        size_++;
    }

    uint64_t find(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t step = hash_to_step(hash);
        for(;;) {
            if(data[index].key == key) return data[index].value;
            (*steps)++;
            index = (index + step) & (capacity - 1);
        }
    }

    bool contains(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t step = hash_to_step(hash), dist = 0;
        while(data[index].key < EMPTY) {
            if(dist++ == capacity) return false;
            if(data[index].key == key) return true;
            (*steps)++;
            index = (index + step) & (capacity - 1);
        }
        return false;
    }

    void erase(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t step = hash_to_step(hash);
        for(;;) {
            if(data[index].key == key) {
                data[index].key = DELETED;
                size_--;
                deleted_++;
                if(deleted_ >= capacity * DF) rehash();
                return;
            }
            index = (index + step) & (capacity - 1);
        }
    }

    void rehash() {
        Slot* old_data = data;
        size_ = 0;
        deleted_ = 0;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
        for(uint64_t i = 0; i < capacity; i++) {
            if(old_data[i].key < DELETED) insert(old_data[i].key, old_data[i].value);
        }
        __aligned_free(old_data);
    }

    void grow() {
        uint64_t old_capacity = capacity;
        Slot* old_data = data;
        size_ = 0;
        capacity *= 2;
        deleted_ = 0;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
        for(uint64_t i = 0; i < old_capacity; i++) {
            if(old_data[i].key < DELETED) insert(old_data[i].key, old_data[i].value);
        }
        __aligned_free(old_data);
    }

    void clear() {
        size_ = 0;
        deleted_ = 0;
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }

    uint64_t index_for(uint64_t key) {
        uint64_t hash = squirrel3(key);
        return hash;
    }
    uint64_t prefetch(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        ::prefetch(&data[index]);
        return hash;
    }
    uint64_t find_indexed(uint64_t key, uint64_t hash, uint64_t* steps) {
        uint64_t index = hash & (capacity - 1);
        uint64_t step = hash_to_step(hash);
        for(;;) {
            if(data[index].key == key) return data[index].value;
            (*steps)++;
            index = (index + step) & (capacity - 1);
        }
    }

    uint64_t size() { return size_; }

    uint64_t memory_usage() { return sizeof(Slot) * capacity + sizeof(Double); }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            if(data[i].key < DELETED) sum += data[i].value;
        }
        return sum;
    }

    struct Slot {
        uint64_t key, value;
    };
    Slot* data;
    uint64_t capacity;
    uint64_t size_;
    uint64_t deleted_;
};