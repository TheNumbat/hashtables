#pragma once

#include "base.h"

template<uint64_t LF_>
struct Robin_Hood {

    static constexpr uint64_t EMPTY = UINT64_MAX;
    static constexpr double LF = static_cast<double>(LF_) / 100.0;

    Robin_Hood() {
        size_ = 0;
        max_probe = 0;
        capacity = 8;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }
    ~Robin_Hood() { __aligned_free(data); }

    // assumes key is not in the map
    void insert(uint64_t key, uint64_t value) {
        if(size_ >= capacity * LF) grow();
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t dist = 0;
        size_++;
        for(;;) {
            if(data[index].key == EMPTY) {
                data[index].key = key;
                data[index].value = value;
                max_probe = std::max(max_probe, dist);
                return;
            }
            uint64_t desired = index_for(data[index].key);
            uint64_t cur_dist = (index + capacity - desired) & (capacity - 1);
            if(cur_dist < dist) {
                std::swap(key, data[index].key);
                std::swap(value, data[index].value);
                max_probe = std::max(max_probe, dist);
                dist = cur_dist;
            }
            dist++;
            index = (index + 1) & (capacity - 1);
        }
    }

    uint64_t find(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t dist = 0;
        for(;;) {
            if(data[index].key == key) return data[index].value;
            (*steps)++;
            dist++;
            index = (index + 1) & (capacity - 1);
        }
    }

    bool contains(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t dist = 0;
        while(dist <= max_probe) {
            if(data[index].key == key) return true;
            (*steps)++;
            dist++;
            index = (index + 1) & (capacity - 1);
        }
        return false;
    }

    void erase(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        for(;;) {
            if(data[index].key == key) {
                data[index].key = EMPTY;
                size_--;
                return;
            }
            index = (index + 1) & (capacity - 1);
        }
    }

    void grow() {
        uint64_t old_capacity = capacity;
        Slot* old_data = data;
        size_ = 0;
        capacity *= 2;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
        for(uint64_t i = 0; i < old_capacity; i++) {
            if(old_data[i].key < EMPTY) insert(old_data[i].key, old_data[i].value);
        }
        __aligned_free(old_data);
    }

    void clear() {
        size_ = 0;
        max_probe = 0;
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }

    uint64_t index_for(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        return index;
    }
    uint64_t prefetch(uint64_t key) {
        uint64_t index = index_for(key);
        ::prefetch(&data[index]);
        return index;
    }
    uint64_t find_indexed(uint64_t key, uint64_t index, uint64_t* steps) {
        uint64_t dist = 0;
        for(;;) {
            if(data[index].key == key) return data[index].value;
            (*steps)++;
            dist++;
            index = (index + 1) & (capacity - 1);
        }
    }

    uint64_t size() { return size_; }

    uint64_t memory_usage() { return sizeof(Slot) * capacity + sizeof(Robin_Hood); }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            if(data[i].key < EMPTY) sum += data[i].value;
        }
        return sum;
    }

    struct Slot {
        uint64_t key, value;
    };
    Slot* data;
    uint64_t capacity;
    uint64_t size_;
    uint64_t max_probe;
};