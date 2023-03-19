#pragma once

#include "base.h"

template<uint64_t LF_>
struct Linear_SIMD {

    static constexpr uint64_t EMPTY = UINT64_MAX;
    static constexpr uint64_t DELETED = UINT64_MAX - 1;
    static constexpr double LF = static_cast<double>(LF_) / 100.0;

    Linear_SIMD() {
        size_ = 0;
        capacity = 8;
        keys =
            reinterpret_cast<uint64_t*>(__aligned_alloc(CACHE_LINE, capacity * sizeof(uint64_t)));
        values =
            reinterpret_cast<uint64_t*>(__aligned_alloc(CACHE_LINE, capacity * sizeof(uint64_t)));
        std::memset(keys, 0xff, sizeof(uint64_t) * capacity);
    }
    ~Linear_SIMD() {
        __aligned_free(keys);
        __aligned_free(values);
    }

    // assumes key is not in the map
    void insert(uint64_t key, uint64_t value) {
        if(size_ >= capacity * LF) grow();
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        while(keys[index] < DELETED) { index = (index + 1) & (capacity - 1); }
        keys[index] = key;
        values[index] = value;
        size_++;
    }

    uint64_t find(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1) & ~3;
        __m256i key256 = _mm256_set1_epi64x(key);
        for(;;) {
            __m256i test = _mm256_loadu_si256(reinterpret_cast<__m256i*>(&keys[index]));
            __m256i cmp = _mm256_cmpeq_epi64(test, key256);
            int32_t mask = _mm256_movemask_epi8(cmp);
            if(mask & 0x000000ff) return values[index];
            if(mask & 0x0000ff00) return values[index + 1];
            if(mask & 0x00ff0000) return values[index + 2];
            if(mask & 0xff000000) return values[index + 3];
            *steps += 4;
            index = (index + 4) & (capacity - 1);
        }
    }

    bool contains(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        uint64_t dist = 0;
        while(keys[index] < EMPTY) {
            if(dist++ == capacity) return false;
            if(keys[index] == key) return true;
            (*steps)++;
            index = (index + 1) & (capacity - 1);
        }
        return false;
    }

    void erase(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        for(;;) {
            if(keys[index] == key) {
                keys[index] = DELETED;
                size_--;
                return;
            }
            index = (index + 1) & (capacity - 1);
        }
    }

    void grow() {
        uint64_t old_capacity = capacity;
        uint64_t* old_keys = keys;
        uint64_t* old_values = values;
        size_ = 0;
        capacity *= 2;
        keys =
            reinterpret_cast<uint64_t*>(__aligned_alloc(CACHE_LINE, capacity * sizeof(uint64_t)));
        values =
            reinterpret_cast<uint64_t*>(__aligned_alloc(CACHE_LINE, capacity * sizeof(uint64_t)));
        std::memset(keys, 0xff, sizeof(uint64_t) * capacity);
        for(uint64_t i = 0; i < old_capacity; i++) {
            if(old_keys[i] < DELETED) insert(old_keys[i], old_values[i]);
        }
        __aligned_free(old_keys);
        __aligned_free(old_values);
    }

    void clear() {
        size_ = 0;
        std::memset(keys, 0xff, sizeof(uint64_t) * capacity);
    }

    uint64_t index_for(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1) & ~3;
        return index;
    }
    uint64_t prefetch(uint64_t key) {
        uint64_t index = index_for(key);
        // prefetches on the same cache line get coalesced
        ::prefetch(&keys[index]);
        ::prefetch(&keys[index + 3]);
        ::prefetch(&values[index]);
        ::prefetch(&values[index + 3]);
        return index;
    }
    uint64_t find_indexed(uint64_t key, uint64_t index, uint64_t* steps) {
        __m256i key256 = _mm256_set1_epi64x(key);
        for(;;) {
            __m256i test = _mm256_loadu_si256(reinterpret_cast<__m256i*>(&keys[index]));
            __m256i cmp = _mm256_cmpeq_epi64(test, key256);
            int32_t mask = _mm256_movemask_epi8(cmp);
            if(mask & 0x000000ff) return values[index];
            if(mask & 0x0000ff00) return values[index + 1];
            if(mask & 0x00ff0000) return values[index + 2];
            if(mask & 0xff000000) return values[index + 3];
            *steps += 4;
            index = (index + 4) & (capacity - 1);
        }
    }

    uint64_t size() { return size_; }

    uint64_t memory_usage() { return 2 * sizeof(uint64_t) * capacity + sizeof(Linear_SIMD); }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            if(keys[i] < DELETED) sum += values[i];
        }
        return sum;
    }

    uint64_t* keys;
    uint64_t* values;
    uint64_t capacity;
    uint64_t size_;
};