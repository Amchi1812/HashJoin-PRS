#pragma once

#include "types.hpp"
#include "seq_hash_join.hpp"
#include <vector>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <omp.h>

class ParallelSharedHashJoin {
private:
    static inline size_t hash_key(int64_t key, size_t mask) {
        uint64_t x = static_cast<uint64_t>(key);
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return static_cast<size_t>(x) & mask;
    }

    static size_t next_power_of_two(size_t n) {
        size_t power = 1;
        while (power < n) {
            power <<= 1;
        }
        return power;
    }

public:
    static std::vector<JoinResult> join(
        const std::vector<Tuple>& R,
        const std::vector<Tuple>& S,
        int num_threads,
        JoinMetrics* metrics = nullptr
    ) {
        auto t_start = std::chrono::high_resolution_clock::now();
        std::vector<JoinResult> results;

        if (R.empty() || S.empty()) {
            if (metrics) *metrics = JoinMetrics{};
            return results;
        }

        const bool r_is_smaller = R.size() <= S.size();
        const std::vector<Tuple>& build_rel = r_is_smaller ? R : S;
        const std::vector<Tuple>& probe_rel = r_is_smaller ? S : R;

        size_t num_buckets = next_power_of_two(build_rel.size());
        size_t mask = num_buckets - 1;

        std::vector<std::atomic<int32_t>> head(num_buckets);
        #pragma omp parallel for num_threads(num_threads) schedule(static)
        for (size_t b = 0; b < num_buckets; ++b) {
            head[b].store(-1, std::memory_order_relaxed);
        }

        std::vector<int32_t> next(build_rel.size(), -1);

        auto t_build_start = std::chrono::high_resolution_clock::now();

        #pragma omp parallel for num_threads(num_threads) schedule(static)
        for (size_t i = 0; i < build_rel.size(); ++i) {
            size_t bucket = hash_key(build_rel[i].key, mask);
            next[i] = head[bucket].exchange(static_cast<int32_t>(i), std::memory_order_relaxed);
        }

        auto t_build_end = std::chrono::high_resolution_clock::now();

        std::vector<std::vector<JoinResult>> thread_results(num_threads);

        #pragma omp parallel num_threads(num_threads)
        {
            int tid = omp_get_thread_num();
            auto& local_vec = thread_results[tid];

            #pragma omp for schedule(dynamic, 4096)
            for (size_t i = 0; i < probe_rel.size(); ++i) {
                const Tuple& p = probe_rel[i];
                size_t bucket = hash_key(p.key, mask);
                int32_t curr = head[bucket].load(std::memory_order_relaxed);

                while (curr != -1) {
                    const Tuple& b = build_rel[curr];
                    if (b.key == p.key) {
                        if (r_is_smaller) {
                            local_vec.push_back(JoinResult{
                                .r_id = b.id,
                                .r_payload = b.payload,
                                .s_id = p.id,
                                .s_payload = p.payload
                            });
                        } else {
                            local_vec.push_back(JoinResult{
                                .r_id = p.id,
                                .r_payload = p.payload,
                                .s_id = b.id,
                                .s_payload = b.payload
                            });
                        }
                    }
                    curr = next[curr];
                }
            }
        }

        size_t total_found = 0;
        for (const auto& vec : thread_results) {
            total_found += vec.size();
        }
        results.reserve(total_found);

        for (auto& vec : thread_results) {
            results.insert(results.end(), vec.begin(), vec.end());
        }

        auto t_end = std::chrono::high_resolution_clock::now();

        if (metrics) {
            metrics->build_time_ms = std::chrono::duration<double, std::milli>(t_build_end - t_build_start).count();
            metrics->probe_time_ms = std::chrono::duration<double, std::milli>(t_end - t_build_end).count();
            metrics->total_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            metrics->result_count = results.size();
        }

        return results;
    }
};