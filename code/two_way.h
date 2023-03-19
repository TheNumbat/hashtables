#pragma once

#include "base.h"

template<uint64_t BUCKET>
struct Two_Way {

    static constexpr uint64_t EMPTY = UINT64_MAX;

    Two_Way() {
        size_ = 0;
        capacity = 8;
        data = reinterpret_cast<Slot*>(__aligned_alloc(CACHE_LINE, sizeof(Slot) * capacity));
        std::memset(data, 0xff, sizeof(Slot) * capacity);
    }
    ~Two_Way() { __aligned_free(data); }

    // assumes key is not in the map
    void insert(uint64_t key, uint64_t value) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        Slot* slot_2 = &data[index_2];
        uint64_t n_1 = 0;
        uint64_t n_2 = 0;
        while(slot_1->keys[n_1] < EMPTY && n_1 < BUCKET) n_1++;
        while(slot_2->keys[n_2] < EMPTY && n_2 < BUCKET) n_2++;
        if(n_1 == BUCKET && n_2 == BUCKET) {
            grow();
            insert(key, value);
            return;
        }
        if(n_1 <= n_2) {
            slot_1->keys[n_1] = key;
            slot_1->values[n_1] = value;
        } else {
            slot_2->keys[n_2] = key;
            slot_2->values[n_2] = value;
        }
        size_++;
    }

    uint64_t find(uint64_t key, uint64_t* steps) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        Slot* slot_2 = &data[index_2];
        for(uint64_t i = 0; i < BUCKET; i++) {
            if(slot_1->keys[i] == key) return slot_1->values[i];
            (*steps)++;
            if(slot_2->keys[i] == key) return slot_2->values[i];
            (*steps)++;
        }
        assert(false);
        return 0;
    }

    bool contains(uint64_t key, uint64_t* steps) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        Slot* slot_2 = &data[index_2];
        for(uint64_t i = 0; i < BUCKET && (slot_1->keys[i] < EMPTY || slot_2->keys[i] < EMPTY);
            i++) {
            if(slot_1->keys[i] == key) return true;
            (*steps)++;
            if(slot_2->keys[i] == key) return true;
            (*steps)++;
        }
        return false;
    }

    void erase(uint64_t key) {
        uint64_t hash_1 = squirrel3(key);
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        Slot* slot_2 = &data[index_2];
        for(uint64_t i = 0; i < BUCKET && (slot_1->keys[i] < EMPTY || slot_2->keys[i] < EMPTY);
            i++) {
            if(slot_1->keys[i] == key) {
                for(uint64_t j = i; j < BUCKET - 1; j++) {
                    slot_1->keys[j] = slot_1->keys[j + 1];
                    slot_1->values[j] = slot_1->values[j + 1];
                }
                slot_1->keys[BUCKET - 1] = EMPTY;
                size_--;
                return;
            }
            if(slot_2->keys[i] == key) {
                for(uint64_t j = i; j < BUCKET - 1; j++) {
                    slot_2->keys[j] = slot_2->keys[j + 1];
                    slot_2->values[j] = slot_2->values[j + 1];
                }
                slot_2->keys[BUCKET - 1] = EMPTY;
                size_--;
                return;
            }
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
            for(uint64_t j = 0; j < BUCKET && slot->keys[j] != EMPTY; j++) {
                insert(slot->keys[j], slot->values[j]);
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
        uint64_t hash_2 = squirrel3(key);
        uint64_t index_1 = hash_1 & (capacity - 1);
        uint64_t index_2 = hash_2 & (capacity - 1);
        ::prefetch(data[index_1].keys);
        ::prefetch(data[index_1].values);
        ::prefetch(data[index_2].keys);
        ::prefetch(data[index_2].values);
        return index_1;
    }
    uint64_t find_indexed(uint64_t key, uint64_t index_1, uint64_t* steps) {
        uint64_t hash_2 = squirrel3_2(key);
        uint64_t index_2 = hash_2 & (capacity - 1);
        Slot* slot_1 = &data[index_1];
        Slot* slot_2 = &data[index_2];
        for(uint64_t i = 0; i < BUCKET; i++) {
            if(slot_1->keys[i] == key) return slot_1->values[i];
            (*steps)++;
            if(slot_2->keys[i] == key) return slot_2->values[i];
            (*steps)++;
        }
        assert(false);
        return 0;
    }

    uint64_t size() { return size_; }

    uint64_t memory_usage() { return sizeof(Slot) * capacity + sizeof(Two_Way); }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            Slot* slot = &data[i];
            for(uint64_t j = 0; j < BUCKET && slot->keys[j] < EMPTY; j++) {
                sum += slot->values[j];
            }
        }
        return sum;
    }

    struct Slot {
        uint64_t keys[BUCKET];
        uint64_t values[BUCKET];
    };
    Slot* data;
    uint64_t capacity;
    uint64_t size_;
};