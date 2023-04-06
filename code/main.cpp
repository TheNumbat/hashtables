
#include <array>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>

#include "base.h"
#include "chaining.h"
#include "double.h"
#include "linear.h"
#include "linear_simd_find.h"
#include "linear_with_deletion.h"
#include "linear_with_rehashing.h"
#include "quadratic.h"
#include "robin_hood.h"
#include "robin_hood_with_deletion.h"
#include "robin_hood_with_desired.h"
#include "two_way.h"
#include "two_way_simd.h"

constexpr uint64_t CAPACITY = 1024 * 1024 * 8;
// constexpr uint64_t CAPACITY = 5000000;
// constexpr uint64_t CAPACITY = 1000;
// constexpr uint64_t CAPACITY = 10;
constexpr bool CSV = true;
// constexpr bool CSV = false;

template<typename T>
concept Hashtable =
    requires(T map, uint64_t key, uint64_t value, uint64_t prefetched, uint64_t* probes) {
        // may assume key is not in the map
        { map.insert(key, value) } -> std::same_as<void>;
        // may assume key is in the map
        { map.find(key, probes) } -> std::same_as<uint64_t>;
        // may assume key is in the map
        { map.erase(key) } -> std::same_as<void>;

        { map.contains(key, probes) } -> std::same_as<bool>;

        { map.sum_all_values() } -> std::same_as<uint64_t>;

        { map.prefetch(key) } -> std::same_as<uint64_t>;
        { map.index_for(key) } -> std::same_as<uint64_t>;
        // may assume key is in the map
        { map.find_indexed(key, prefetched, probes) } -> std::same_as<uint64_t>;

        { map.clear() } -> std::same_as<void>;
        { map.memory_usage() } -> std::same_as<uint64_t>;
        { map.size() } -> std::same_as<uint64_t>;
    };

template<typename T>
struct Std_Map_ {

    void insert(uint64_t key, uint64_t value) { map.insert({key, value}); }
    uint64_t find(uint64_t key, uint64_t*) { return map.find(key)->second; }
    void erase(uint64_t key) { assert(map.erase(key) > 0); }
    void clear() { map.clear(); }
    uint64_t memory_usage() { return 0; }
    uint64_t size() { return map.size(); }
    bool contains(uint64_t key, uint64_t*) { return map.contains(key); }

    uint64_t sum_all_values() {
        uint64_t sum = 0;
        for(const auto& [_, value] : map) { sum += value; }
        return sum;
    }

    uint64_t prefetch(uint64_t) { return 0; }
    uint64_t index_for(uint64_t) { return 0; }
    uint64_t find_indexed(uint64_t key, uint64_t, uint64_t* probes) {
        return map.find(key)->second;
    }

    std::unordered_map<uint64_t, uint64_t, T> map;
};
using Std_Map = Std_Map_<std::hash<uint64_t>>;

struct Squirrel3_Hash {
    uint64_t operator()(uint64_t key) const { return squirrel3(key); }
};
using Std_Map_Squirrel3 = Std_Map_<Squirrel3_Hash>;

template<Hashtable Map, uint64_t LF, uint64_t UNROLL>
auto throughput() -> std::unordered_map<std::string, uint64_t> {
    Map map;
    std::unordered_map<std::string, uint64_t> results;

    std::mt19937 rng{};

    constexpr uint64_t N =
        static_cast<uint64_t>(static_cast<double>(CAPACITY) * static_cast<double>(LF) / 100.0) - 1;

    // make satollo cycle
    std::vector<uint64_t> next(N);
    {
        for(uint64_t i = 0; i < N; ++i) { next[i] = i; }
        for(uint64_t i = 0; i < N - 1; i++) {
            uint64_t j = i + 1 + rng() % (N - i - 1);
            std::swap(next[i], next[j]);
        }
    }

    {
        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) { map.insert(i, next[i]); }
        const auto end = std::chrono::high_resolution_clock::now();

        results["insert_1"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["insert_1_memory"] = map.memory_usage();
    }
    assert(map.size() == N);

    // traverse full cycle (cannot use ILP)
    {
        uint64_t n = 0;
        uint64_t total_probe_length = 0, max_probe_length = 0;

        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) {
            uint64_t probe_length = 0;
            n = map.find(n, &probe_length);
            total_probe_length += probe_length;
            max_probe_length = std::max(max_probe_length, probe_length);
        }
        const auto end = std::chrono::high_resolution_clock::now();

        assert(n == 0);
        results["find_satollo"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["find_satollo_probes"] = total_probe_length;
        results["find_satollo_max_probes"] = max_probe_length;
    }
    assert(map.size() == N);

    // traverse linear (will automatically use ILP)
    {
        uint64_t total_probe_length = 0, max_probe_length = 0;

        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) {
            uint64_t probe_length = 0;
            assert(map.find(i, &probe_length) == next[i]);
            total_probe_length += probe_length;
            max_probe_length = std::max(max_probe_length, probe_length);
        };
        const auto end = std::chrono::high_resolution_clock::now();

        results["find_linear"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["find_linear_probes"] = total_probe_length;
        results["find_linear_max_probes"] = max_probe_length;
    }

    // traverse linear (manually unroll)
    {
        uint64_t total_probe_length = 0, max_probe_length = 0;
        uint64_t indexed[UNROLL];

        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t stop_at = N - (N % UNROLL);
        for(uint64_t i = 0; i < stop_at; i += UNROLL) {
            for(uint64_t j = 0; j < UNROLL; ++j) { indexed[j] = map.index_for(i + j); }
            for(uint64_t j = 0; j < UNROLL; ++j) {
                uint64_t probe_length = 0;
                assert(map.find_indexed(i + j, indexed[j], &probe_length) == next[i + j]);
                total_probe_length += probe_length;
                max_probe_length = std::max(max_probe_length, probe_length);
            }
        };
        for(uint64_t i = stop_at; i < N; ++i) {
            uint64_t probe_length = 0;
            assert(map.find(i, &probe_length) == next[i]);
            total_probe_length += probe_length;
            max_probe_length = std::max(max_probe_length, probe_length);
        }
        const auto end = std::chrono::high_resolution_clock::now();

        results["find_unroll"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["find_unroll_probes"] = total_probe_length;
        results["find_unroll_max_probes"] = max_probe_length;
    }

    // traverse linear (manually unroll + prefetch)
    {
        uint64_t total_probe_length = 0, max_probe_length = 0;
        uint64_t prefetched[UNROLL];

        const auto start = std::chrono::high_resolution_clock::now();
        uint64_t stop_at = N - (N % UNROLL);
        for(uint64_t i = 0; i < stop_at; i += UNROLL) {
            for(uint64_t j = 0; j < UNROLL; ++j) { prefetched[j] = map.prefetch(i + j); }
            for(uint64_t j = 0; j < UNROLL; ++j) {
                uint64_t probe_length = 0;
                assert(map.find_indexed(i + j, prefetched[j], &probe_length) == next[i + j]);
                total_probe_length += probe_length;
                max_probe_length = std::max(max_probe_length, probe_length);
            }
        };
        for(uint64_t i = stop_at; i < N; ++i) {
            uint64_t probe_length = 0;
            assert(map.find(i, &probe_length) == next[i]);
            total_probe_length += probe_length;
            max_probe_length = std::max(max_probe_length, probe_length);
        }
        const auto end = std::chrono::high_resolution_clock::now();

        results["find_unroll_prefetch"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["find_unroll_prefetch_probes"] = total_probe_length;
        results["find_unroll_prefetch_max_probes"] = max_probe_length;
    }

    // traverse linear (all elements directly)
    {
        const auto start = std::chrono::high_resolution_clock::now();
        assert(map.sum_all_values() == N * (N - 1) / 2);
        const auto end = std::chrono::high_resolution_clock::now();

        results["iterate_all_structure_aware"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

    // erase all elements
    {
        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) { map.erase(i); }
        const auto end = std::chrono::high_resolution_clock::now();

        results["erase"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["erase_memory"] = map.memory_usage();
    }
    assert(map.size() == 0);

    // insert new keys in random order
    {
        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) { map.insert(N + next[i], i); }
        const auto end = std::chrono::high_resolution_clock::now();

        results["insert_2"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["insert_2_memory"] = map.memory_usage();
    }
    assert(map.size() == N);

    // traverse new keys
    {
        uint64_t total_probe_length = 0, max_probe_length = 0;

        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) {
            uint64_t probe_length = 0;
            assert(map.find(N + next[i], &probe_length) == i);
            total_probe_length += probe_length;
            max_probe_length = std::max(max_probe_length, probe_length);
        };
        const auto end = std::chrono::high_resolution_clock::now();

        results["find_new"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["find_new_probes"] = total_probe_length;
        results["find_new_max_probes"] = max_probe_length;
    }

    // attempt to find missing keys
    {
        uint64_t total_probe_length = 0, max_probe_length = 0;

        const auto start = std::chrono::high_resolution_clock::now();
        for(uint64_t i = 0; i < N; ++i) {
            uint64_t probe_length = 0;
            assert(!map.contains(i, &probe_length));
            total_probe_length += probe_length;
            max_probe_length = std::max(max_probe_length, probe_length);
        }
        const auto end = std::chrono::high_resolution_clock::now();

        results["find_missing"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["find_missing_probes"] = total_probe_length;
        results["find_missing_max_probes"] = max_probe_length;
    }

    // clear map
    {
        const auto start = std::chrono::high_resolution_clock::now();
        map.clear();
        const auto end = std::chrono::high_resolution_clock::now();

        results["clear"] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        results["clear_memory"] = map.memory_usage();
    }
    assert(map.size() == 0);

    return results;
}

template<Hashtable Map, uint64_t LF, uint64_t UNROLL = 10>
void benchmark(std::string name, std::ostream& out) {

    constexpr uint64_t N =
        static_cast<uint64_t>(static_cast<double>(CAPACITY) * static_cast<double>(LF) / 100.0) - 1;
    constexpr uint64_t COUNT = 10;

    std::unordered_map<std::string, uint64_t> results;

    for(uint64_t i = 0; i < COUNT; ++i) {
        auto results_i = throughput<Map, LF, UNROLL>();

        for(const auto& [key, value] : results_i) {
            if(key == "find_satollo_max_probes" || key == "find_unroll_max_probes" ||
               key == "find_new_max_probes" || key == "find_unroll_prefetch_max_probes" ||
               key == "find_missing_max_probes" || key == "find_linear_max_probes")
                results[key] = std::max(results[key], value);
            else
                results[key] += value;
        }
    }

    for(auto& [key, value] : results) {
        if(key != "find_satollo_max_probes" && key != "find_unroll_max_probes" &&
           key != "find_new_max_probes" && key != "find_unroll_prefetch_max_probes" &&
           key != "find_missing_max_probes" && key != "find_linear_max_probes")
            value /= COUNT;
    }

    constexpr double Nd = static_cast<double>(N);

    if constexpr(CSV) {
        out << name << "," << results["insert_1"] / Nd << ","
            << results["insert_1_memory"] / (1024 * 1024) << "," << results["find_satollo"] / Nd
            << "," << results["find_satollo_probes"] / Nd << ","
            << results["find_satollo_max_probes"] << "," << results["find_linear"] / Nd << ","
            << results["find_linear_probes"] / Nd << "," << results["find_linear_max_probes"] << ","
            << results["find_unroll"] / Nd << "," << results["find_unroll_probes"] / Nd << ","
            << results["find_unroll_max_probes"] << "," << results["find_unroll_prefetch"] / Nd
            << "," << results["find_unroll_prefetch_probes"] / Nd << ","
            << results["find_unroll_prefetch_max_probes"] << "," << results["find_new"] / Nd << ","
            << results["find_new_probes"] / Nd << "," << results["find_new_max_probes"] << ","
            << results["find_missing"] / Nd << "," << results["find_missing_probes"] / Nd << ","
            << results["find_missing_max_probes"] << "," << results["erase"] / Nd << ","
            << results["erase_memory"] / (1024 * 1024) << "," << results["insert_2"] / Nd << ","
            << results["insert_2_memory"] / (1024 * 1024) << "," << results["clear"] / Nd << ","
            << results["clear_memory"] / (1024 * 1024) << "," << results["insert_1_memory"] / Nd
            << "," << results["iterate_all_structure_aware"] / Nd << std::endl;

    } else {
        out << "insert: " << results["insert_1"] / Nd
            << " ns/ins | mem: " << results["insert_1_memory"] / (1024 * 1024) << " mb"
            << std::endl;
        out << "bytes per element: " << results["insert_1_memory"] / Nd << std::endl;

        out << "find no-unroll: " << results["find_satollo"] / Nd
            << " ns/find | avg probe: " << results["find_satollo_probes"] / Nd
            << " | max probe: " << results["find_satollo_max_probes"] << std::endl;
        out << "find linear: " << results["find_linear"] / Nd
            << " ns/find | avg probe: " << results["find_linear_probes"] / Nd
            << " | max probe: " << results["find_linear_max_probes"] << std::endl;
        out << "find unroll: " << results["find_unroll"] / Nd
            << " ns/find | avg probe: " << results["find_unroll_probes"] / Nd
            << " | max probe: " << results["find_unroll_max_probes"] << std::endl;
        out << "find unroll prefetch: " << results["find_unroll_prefetch"] / Nd
            << " ns/find | avg probe: " << results["find_unroll_prefetch_probes"] / Nd
            << " | max probe: " << results["find_unroll_prefetch_max_probes"] << std::endl;
        out << "sum all values: " << results["iterate_all_structure_aware"] / Nd << " ns/val"
            << std::endl;

        out << "erase: " << results["erase"] / Nd
            << " ns/erase | mem: " << results["erase_memory"] / (1024 * 1024) << " mb" << std::endl;
        out << "insert after erase: " << results["insert_2"] / Nd
            << " ns/ins | mem: " << results["insert_2_memory"] / (1024 * 1024) << " mb"
            << std::endl;
        out << "find new: " << results["find_new"] / Nd
            << " ns/find | avg probe: " << results["find_new_probes"] / Nd
            << " | max probe: " << results["find_new_max_probes"] << std::endl;
        out << "find missing: " << results["find_missing"] / Nd
            << " ns/find | avg probe: " << results["find_missing_probes"] / Nd
            << " | max probe: " << results["find_missing_max_probes"] << std::endl;

        out << "clear: " << results["clear"] / (1000.0 * 1000.0)
            << "ms | mem: " << results["clear_memory"] / (1024 * 1024) << " mb" << std::endl;
    }
}

int main(int argc, char** argv) {

    std::map<std::string, std::function<void(std::string, std::ostream&)>> benchmarks = {
        {"chaining_50", benchmark<Chaining<50>, 50>},
        {"chaining_100", benchmark<Chaining<100>, 100>},
        {"chaining_200", benchmark<Chaining<200>, 200>},
        {"chaining_500", benchmark<Chaining<500>, 500>},
        {"two_way_2", benchmark<Two_Way<2>, 100>},
        {"two_way_4", benchmark<Two_Way<4>, 100>},
        {"two_way_8", benchmark<Two_Way<8>, 100>},
        {"two_way_simd", benchmark<Two_Way_SIMD, 100>},
        {"robin_hood_50", benchmark<Robin_Hood<50>, 50>},
        {"robin_hood_75", benchmark<Robin_Hood<75>, 75>},
        {"robin_hood_90", benchmark<Robin_Hood<90>, 90>},
        {"robin_hood_with_deletion_50", benchmark<Robin_Hood_With_Deletion<50>, 50>},
        {"robin_hood_with_deletion_75", benchmark<Robin_Hood_With_Deletion<75>, 75>},
        {"robin_hood_with_deletion_90", benchmark<Robin_Hood_With_Deletion<90>, 90>},
        {"robin_hood_with_desired_50", benchmark<Robin_Hood_With_Desired<50>, 50>},
        {"robin_hood_with_desired_75", benchmark<Robin_Hood_With_Desired<75>, 75>},
        {"robin_hood_with_desired_90", benchmark<Robin_Hood_With_Desired<90>, 90>},
        {"linear_with_deletion_50", benchmark<Linear_With_Deletion<50>, 50>},
        {"linear_with_deletion_75", benchmark<Linear_With_Deletion<75>, 75>},
        {"linear_with_deletion_90", benchmark<Linear_With_Deletion<90>, 90>},
        {"linear_with_rehash_50", benchmark<Linear_With_Rehash<50, 50>, 50>},
        {"linear_with_rehash_75", benchmark<Linear_With_Rehash<75, 50>, 75>},
        {"linear_with_rehash_90", benchmark<Linear_With_Rehash<90, 50>, 90>},
        {"linear_50", benchmark<Linear<50>, 50>},
        {"linear_75", benchmark<Linear<75>, 75>},
        {"linear_90", benchmark<Linear<90>, 90>},
        {"linear_simd_50", benchmark<Linear_SIMD<50>, 50>},
        {"linear_simd_75", benchmark<Linear_SIMD<75>, 75>},
        {"linear_simd_90", benchmark<Linear_SIMD<90>, 90>},
        {"quadratic_50", benchmark<Quadratic<50, 50>, 50>},
        {"quadratic_75", benchmark<Quadratic<75, 50>, 75>},
        {"quadratic_90", benchmark<Quadratic<90, 50>, 90>},
        {"double_50", benchmark<Double<50, 50>, 50>},
        {"double_75", benchmark<Double<75, 50>, 75>},
        {"double_90", benchmark<Double<90, 50>, 90>},
        {"stdumap", benchmark<Std_Map, 100>},
        {"stdumap_squirrel", benchmark<Std_Map_Squirrel3, 100>},
    };

    std::vector<std::string> run;
    if(argc == 1) {
        for(auto& b : benchmarks) { run.push_back(b.first); }
    } else {
        for(int i = 1; i < argc; ++i) { run.push_back(argv[i]); }
    }

    if constexpr(CSV) {
        std::ofstream out("results.csv", std::ios::out | std::ios::trunc);
        out << "table,insert_1,insert_1_memory,find_satollo,find_satollo_probes,find_satollo_max_"
               "probes,find_linear,find_linear_probes,find_linear_max_probes,"
               "find_unroll,find_unroll_probes,find_unroll_max_probes,find_unroll_prefetch,find_"
               "unroll_prefetch_probes,find_unroll_prefetch_max_probes,find_new,find_new_probes,"
               "find_new_max_probes,find_missing,find_missing_probes,find_missing_max_probes,erase,"
               "erase_memory,insert_2,insert_2_memory,clear,clear_memory,bytes_per_value,iterate_"
               "all_structure_aware"
            << std::endl;
        for(auto& b : run) {
            if(benchmarks.find(b) != benchmarks.end()) { 
                std::cout << "Running " << b << "..." << std::endl;
                benchmarks[b](b, out); 
            }
        }
    } else {
        for(auto& b : run) {
            std::cout << "Benchmark: " << b << std::endl;
            if(benchmarks.find(b) != benchmarks.end()) { 
                std::cout << "Running " << b << "..." << std::endl;
                benchmarks[b](b, std::cout); 
            }
            std::cout << std::endl;
        }
    }
}
