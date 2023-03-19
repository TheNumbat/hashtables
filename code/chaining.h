#pragma once

#include "base.h"

template<uint64_t LF_>
struct Chaining {

    static constexpr double LF = static_cast<double>(LF_) / 100.0;

    Chaining() {
        size_ = 0;
        capacity = 8;
        data = reinterpret_cast<Slot**>(__aligned_alloc(CACHE_LINE, sizeof(Slot*) * capacity));
        std::memset(data, 0, sizeof(Slot*) * capacity);
    }
    ~Chaining() {
        for(uint64_t i = 0; i < capacity; i++) {
            Slot* s = data[i];
            while(s) {
                Slot* next = s->next;
                delete s;
                s = next;
            }
        }
        __aligned_free(data);
    }

    // assumes key is not in the map
    void insert(uint64_t key, uint64_t value) {
        if(size_ >= capacity * LF) grow();
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        Slot* s = new Slot;
        s->key = key;
        s->value = value;
        s->next = data[index];
        data[index] = s;
        size_++;
    }

    uint64_t find(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        Slot* s = data[index];
        while(s) {
            (*steps)++;
            if(s->key == key) return s->value;
            s = s->next;
        }
        assert(false);
        return 0;
    }

    bool contains(uint64_t key, uint64_t* steps) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        Slot* s = data[index];
        while(s) {
            (*steps)++;
            if(s->key == key) return true;
            s = s->next;
        }
        return false;
    }

    void erase(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        Slot* s = data[index];
        Slot* prev = nullptr;
        while(s) {
            if(s->key == key) {
                if(prev)
                    prev->next = s->next;
                else
                    data[index] = s->next;
                delete s;
                size_--;
                return;
            }
            prev = s;
            s = s->next;
        }
    }

    void grow() {
        uint64_t old_capacity = capacity;
        Slot** old_data = data;
        capacity *= 2;
        data = reinterpret_cast<Slot**>(__aligned_alloc(CACHE_LINE, sizeof(Slot*) * capacity));
        std::memset(data, 0, sizeof(Slot*) * capacity);
        for(uint64_t i = 0; i < old_capacity; i++) {
            Slot* s = old_data[i];
            while(s) {
                Slot* next = s->next;
                uint64_t hash = squirrel3(s->key);
                uint64_t index = hash & (capacity - 1);
                s->next = data[index];
                data[index] = s;
                s = next;
            }
        }
        __aligned_free(old_data);
    }

    void clear() {
        size_ = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            Slot* s = data[i];
            while(s) {
                Slot* next = s->next;
                delete s;
                s = next;
            }
            data[i] = nullptr;
        }
    }

    uint64_t index_for(uint64_t key) {
        uint64_t hash = squirrel3(key);
        uint64_t index = hash & (capacity - 1);
        return index;
    }
    uint64_t prefetch(uint64_t key) {
        uint64_t index = index_for(key);
        ::prefetch(data[index]);
        return index;
    }
    uint64_t find_indexed(uint64_t key, uint64_t index, uint64_t* steps) {
        Slot* s = data[index];
        while(s) {
            (*steps)++;
            if(s->key == key) return s->value;
            s = s->next;
        }
        assert(false);
        return 0;
    }

    uint64_t size() { return size_; }

    uint64_t memory_usage() {
        return sizeof(Slot*) * capacity + sizeof(Slot) * size_ + sizeof(Chaining);
    }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(uint64_t i = 0; i < capacity; i++) {
            Slot* s = data[i];
            while(s) {
                sum += s->value;
                s = s->next;
            }
        }
        return sum;
    }

    struct Slot {
        uint64_t key, value;
        Slot* next;
    };
    Slot** data;
    uint64_t capacity;
    uint64_t size_;
};