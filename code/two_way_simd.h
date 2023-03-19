#pragma once

#include "base.h"

#if defined(__clang__) || (not defined(_MSC_VER) && defined(__GNUC__))
#include <immintrin.h>
inline int __ctz(int32_t x) { return __builtin_ctz(x); }
inline uint64_t __extract(__m256i& vec, int index) {
    switch(index) {
    case 0: return _mm256_extract_epi64(vec, 0);
    case 1: return _mm256_extract_epi64(vec, 1);
    case 2: return _mm256_extract_epi64(vec, 2);
    case 3: return _mm256_extract_epi64(vec, 3);
    }
    assert(false);
    return 0;
}
inline void __insert(__m256i& vec, uint64_t value, int index) {
    switch(index) {
    case 0: vec = _mm256_insert_epi64(vec, value, 0); break;
    case 1: vec = _mm256_insert_epi64(vec, value, 1); break;
    case 2: vec = _mm256_insert_epi64(vec, value, 2); break;
    case 3: vec = _mm256_insert_epi64(vec, value, 3); break;
    default: assert(false);
    }
}
#else
#include <intrin.h>
int __ctz(int32_t x) {
    unsigned long index;
    _BitScanForward(&index, x);
    return index;
}
inline uint64_t __extract(__m256i& vec, int index) { return vec.m256i_u64[index]; }
inline void __insert(__m256i& vec, uint64_t value, int index) { vec.m256i_u64[index] = value; }
#endif

struct Two_Way_SIMD {

    static constexpr int BUCKET = 4;
    static constexpr uint64_t EMPTY = UINT64_MAX;
    static inline const __m256i EMPTY256 = _mm256_set1_epi64x(EMPTY);

    Two_Way_SIMD() {
        size_ = 0;
        capacity = 8;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }
    ~Two_Way_SIMD() { __aligned_free(data); }

    // assumes key is not in the map
    void insert(uint64_t key, uint64_t value) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        Slot* slot_2 = &data[index_2];
        __m256i cmp_1 = _mm256_cmpeq_epi64(slot_1->keys, EMPTY256);
        __m256i cmp_2 = _mm256_cmpeq_epi64(slot_2->keys, EMPTY256);
        int32_t mask_1 = _mm256_movemask_epi8(cmp_1);
        int32_t mask_2 = _mm256_movemask_epi8(cmp_2);
        int n_1 = mask_1 ? __ctz(mask_1) >> 3 : BUCKET;
        int n_2 = mask_2 ? __ctz(mask_2) >> 3 : BUCKET;
        if(n_1 == BUCKET && n_2 == BUCKET) {
            grow();
            insert(key, value);
            return;
        }
        if(n_1 <= n_2) {
            __insert(slot_1->keys, key, n_1);
            __insert(slot_1->values, value, n_1);
        } else {
            __insert(slot_2->keys, key, n_2);
            __insert(slot_2->values, value, n_2);
        }
        size_++;
    }

    uint64_t find(uint64_t key, uint64_t* steps) {
        __m256i key256 = _mm256_set1_epi64x(key);
        uint64_t hash_1 = squirrel3(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        __m256i cmp_1 = _mm256_cmpeq_epi64(slot_1->keys, key256);
        int32_t mask_1 = _mm256_movemask_epi8(cmp_1);
        if(mask_1) {
            int i = __ctz(mask_1) >> 3;
            return __extract(slot_1->values, i);
        }
        (*steps)++;
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_2 = &data[index_2];
        __m256i cmp_2 = _mm256_cmpeq_epi64(slot_2->keys, key256);
        int32_t mask_2 = _mm256_movemask_epi8(cmp_2);
        if(mask_2) {
            int i = __ctz(mask_2) >> 3;
            return __extract(slot_2->values, i);
        }
        assert(false);
        return 0;
    }

    bool contains(uint64_t key, uint64_t* steps) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        __m256i key256 = _mm256_set1_epi64x(key);
        __m256i cmp_1 = _mm256_cmpeq_epi64(slot_1->keys, key256);
        int32_t mask_1 = _mm256_movemask_epi8(cmp_1);
        if(mask_1) { return true; }
        (*steps)++;
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_2 = &data[index_2];
        __m256i cmp_2 = _mm256_cmpeq_epi64(slot_2->keys, key256);
        int32_t mask_2 = _mm256_movemask_epi8(cmp_2);
        if(mask_2) { return true; }
        return false;
    }

    void erase(uint64_t key) {
        __m256i key256 = _mm256_set1_epi64x(key);
        uint64_t hash_1 = squirrel3(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        __m256i cmp_1 = _mm256_cmpeq_epi64(slot_1->keys, key256);
        int32_t mask_1 = _mm256_movemask_epi8(cmp_1);
        if(mask_1) {
            int i = __ctz(mask_1) / 8;
            for(int j = i; j < BUCKET - 1; j++) {
                __insert(slot_1->keys, __extract(slot_1->keys, j + 1), j);
                __insert(slot_1->values, __extract(slot_1->values, j + 1), j);
            }
            __insert(slot_1->keys, EMPTY, BUCKET - 1);
            size_--;
            return;
        }
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_2 = &data[index_2];
        __m256i cmp_2 = _mm256_cmpeq_epi64(slot_2->keys, key256);
        int32_t mask_2 = _mm256_movemask_epi8(cmp_2);
        if(mask_2) {
            int i = __ctz(mask_2) / 8;
            for(int j = i; j < BUCKET - 1; j++) {
                __insert(slot_2->keys, __extract(slot_2->keys, j + 1), j);
                __insert(slot_2->values, __extract(slot_2->values, j + 1), j);
            }
            __insert(slot_2->keys, EMPTY, BUCKET - 1);
            size_--;
            return;
        }
        assert(false);
    }

    void grow() {
        uint64_t old_capacity = capacity;
        Slot* old_data = data;
        size_ = 0;
        capacity *= 2;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
        for(uint64_t i = 0; i < old_capacity; i++) {
            Slot* slot = &old_data[i];
            for(int j = 0; j < BUCKET; j++) {
                uint64_t k1 = __extract(slot->keys, j);
                if(k1 != EMPTY)
                    insert(k1, __extract(slot->values, j));
                else
                    break;
            }
        }
        __aligned_free(old_data);
    }

    void clear() {
        size_ = 0;
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }

    uint64_t index_for(uint64_t key) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        return index_1;
    }
    uint64_t prefetch(uint64_t key) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        ::prefetch(&data[index_1].keys);
        ::prefetch(&data[index_2].values);
        ::prefetch(&data[index_1].keys);
        ::prefetch(&data[index_2].values);
        return index_1;
    }
    uint64_t find_indexed(uint64_t key, uint64_t index_1, uint64_t* steps) {
        __m256i key256 = _mm256_set1_epi64x(key);
        Slot* slot_1 = &data[index_1];
        __m256i cmp_1 = _mm256_cmpeq_epi64(slot_1->keys, key256);
        int32_t mask_1 = _mm256_movemask_epi8(cmp_1);
        if(mask_1) {
            int i = __ctz(mask_1) >> 3;
            return __extract(slot_1->values, i);
        }
        (*steps)++;
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_2 = &data[index_2];
        __m256i cmp_2 = _mm256_cmpeq_epi64(slot_2->keys, key256);
        int32_t mask_2 = _mm256_movemask_epi8(cmp_2);
        if(mask_2) {
            int i = __ctz(mask_2) >> 3;
            return __extract(slot_2->values, i);
        }
        assert(false);
        return 0;
    }

    uint64_t size() { return size_; }

    uint64_t memory_usage() { return sizeof(Slot) * capacity + sizeof(Two_Way_SIMD); }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            Slot* slot = &data[i];
            for(int j = 0; j < BUCKET; j++) {
                uint64_t k1 = __extract(slot->keys, j);
                if(k1 != EMPTY)
                    sum += __extract(slot->values, j);
                else
                    break;
            }
        }
        return sum;
    }

    struct Slot {
        __m256i keys;
        __m256i values;
    };
    Slot* data;
    uint64_t capacity;
    uint64_t size_;
};