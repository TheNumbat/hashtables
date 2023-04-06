#pragma once

#include <cstdint>
#include <cstring>

#define CACHE_LINE 64

#ifdef _WIN32
#include <intrin.h>
inline void prefetch(const void* ptr) { _mm_prefetch((const char*)ptr, _MM_HINT_NTA); }
inline void assert(bool val) {
    if(!val) {
        std::cout << "assertion failed" << std::endl;
        __debugbreak();
        std::exit(1);
    }
}
inline void* __aligned_alloc(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}
inline void __aligned_free(void* ptr) { return _aligned_free(ptr); }
#else
#include <csignal>
#include <cstdlib>
#include <immintrin.h>
inline void prefetch(const void* ptr) { __builtin_prefetch(ptr, 0, 0); }
inline void assert(bool val) {
    if(!val) {
        std::cout << "assertion failed" << std::endl;
        raise(SIGTRAP);
        std::exit(1);
    }
}
inline void* __aligned_alloc(size_t alignment, size_t size) {
    return std::aligned_alloc(alignment, size);
}
inline void __aligned_free(void* ptr) { return std::free(ptr); }
#endif

// These constants are all large primes

inline uint64_t squirrel3(uint64_t at) {
    constexpr uint64_t BIT_NOISE1 = 0x9E3779B185EBCA87ULL;
    constexpr uint64_t BIT_NOISE2 = 0xC2B2AE3D27D4EB4FULL;
    constexpr uint64_t BIT_NOISE3 = 0x27D4EB2F165667C5ULL;
    at *= BIT_NOISE1;
    at ^= (at >> 8);
    at += BIT_NOISE2;
    at ^= (at << 8);
    at *= BIT_NOISE3;
    at ^= (at >> 8);
    return at;
}
