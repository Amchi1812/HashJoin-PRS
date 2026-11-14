#pragma once

#include "types.hpp"
#include "seq_hash_join.hpp"
#include <vector>
#include <cstdint>
#include <chrono>
#include <omp.h>

struct PartitionedJoinMetrics {
    double partition_time_ms = 0.0;
    double build_time_ms = 0.0;
    double probe_time_ms = 0.0;
    double total_time_ms = 0.0;
    size_t result_count = 0;
};

class ParallelPartitionedHashJoin {
private:
    static inline uint64_t hash_val(int64_t key) {
        uint64_t x = static_cast<uint64_t>(key);
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33;
        return x;
    }

    static size_t next_power_of_two(size_t n) {
        size_t power = 1;
        while (power < n) {
            power <<= 1;
        }
        return power;
    }

    static void radix_partition(
        const std::vector<Tuple>& input,
        std::vector<Tuple>& output,
        std::vector<size_t>& part_offsets,
        int radix_bits,
        int num_threads
    ) {
        const size_t num_parts = size_t{1} << radix_bits;
        const size_t part_mask = num_parts - 1;
        const size_t n = input.size();

        output.resize(n);
        part_offsets.assign(num_parts + 1, 0);

        std::vector<std::vector<size_t>> local_hist(num_threads, std::vector<size_t>(num_parts, 0));

        #pragma omp parallel num_threads(num_threads)
        {
            int tid = omp_get_thread_num();
            auto& hist = local_hist[tid];

            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; ++i) {
                size_t p = static_cast<size_t>(hash_val(input[i].key)) & part_mask;
                hist[p]++;
            }
        }

        std::vector<std::vector<size_t>> thread_offsets(num_threads, std::vector<size_t>(num_parts, 0));
        size_t running_offset = 0;
        for (size_t p = 0; p < num_parts; ++p) {
            part_offsets[p] = running_offset;
            for (int t = 0; t < num_threads; ++t) {
                thread_offsets[t][p] = running_offset;
                running_offset += local_hist[t][p];
            }
        }
        part_offsets[num_parts] = running_offset;

        #pragma omp parallel num_threads(num_threads)
        {
            int tid = omp_get_thread_num();
            auto& offsets = thread_offsets[tid];

            #pragma omp for schedule(static)
            for (size_t i = 0; i < n; ++i) {
                size_t p = static_cast<size_t>(hash_val(input[i].key)) & part_mask;
                output[offsets[p]++] = input[i];
            }
        }
    }

public:
    static std::vector<JoinResult> join(
        const std::vector<Tuple>& R,
        const std::vector<Tuple>& S,
        int num_threads,
        PartitionedJoinMetrics* metrics = nullptr,
        int radix_bits = 6
    ) {
        auto t_start = std::chrono::high_resolution_clock::now();
        std::vector<JoinResult> results;

        if (R.empty() || S.empty()) {
            if (metrics) *metrics = PartitionedJoinMetrics{};
            return results;
        }

        const bool r_is_smaller = R.size() <= S.size();
        const std::vector<Tuple>& build_rel = r_is_smaller ? R : S;
        const std::vector<Tuple>& probe_rel = r_is_smaller ? S : R;

        auto t_part_start = std::chrono::high_resolution_clock::now();

        std::vector<Tuple> build_parted;
        std::vector<size_t> build_offsets;
        radix_partition(build_rel, build_parted, build_offsets, radix_bits, num_threads);

        std::vector<Tuple> probe_parted;
        std::vector<size_t> probe_offsets;
        radix_partition(probe_rel, probe_parted, probe_offsets, radix_bits, num_threads);

        auto t_part_end = std::chrono::high_resolution_clock::now();

        const size_t num_parts = size_t{1} << radix_bits;
        std::vector<std::vector<JoinResult>> part_results(num_parts);
        std::vector<double> thread_build_ms(num_threads, 0.0);
        std::vector<double> thread_probe_ms(num_threads, 0.0);

        #pragma omp parallel num_threads(num_threads)
        {
            int tid = omp_get_thread_num();
            double local_build_time = 0.0;
            double local_probe_time = 0.0;

            #pragma omp for schedule(dynamic, 1)
            for (size_t p = 0; p < num_parts; ++p) {
                size_t b_start = build_offsets[p];
                size_t b_end = build_offsets[p + 1];
                size_t p_start = probe_offsets[p];
                size_t p_end = probe_offsets[p + 1];

                size_t b_size = b_end - b_start;
                size_t p_size = p_end - p_start;

                if (b_size == 0 || p_size == 0) {
                    continue;
                }

                auto tb1 = std::chrono::high_resolution_clock::now();

                size_t num_buckets = next_power_of_two(b_size);
                size_t bucket_mask = num_buckets - 1;
                std::vector<int32_t> head(num_buckets, -1);
                std::vector<int32_t> next(b_size, -1);

                for (size_t i = 0; i < b_size; ++i) {
                    uint64_t h = hash_val(build_parted[b_start + i].key) >> radix_bits;
                    size_t bucket = static_cast<size_t>(h) & bucket_mask;
                    next[i] = head[bucket];
                    head[bucket] = static_cast<int32_t>(i);
                }

                auto tb2 = std::chrono::high_resolution_clock::now();
                local_build_time += std::chrono::duration<double, std::milli>(tb2 - tb1).count();

                auto& out_vec = part_results[p];

                for (size_t i = 0; i < p_size; ++i) {
                    const Tuple& pr = probe_parted[p_start + i];
                    uint64_t h = hash_val(pr.key) >> radix_bits;
                    size_t bucket = static_cast<size_t>(h) & bucket_mask;
                    int32_t curr = head[bucket];

                    while (curr != -1) {
                        const Tuple& br = build_parted[b_start + curr];
                        if (br.key == pr.key) {
                            if (r_is_smaller) {
                                out_vec.push_back(JoinResult{
                                    .r_id = br.id,
                                    .r_payload = br.payload,
                                    .s_id = pr.id,
                                    .s_payload = pr.payload
                                });
                            } else {
                                out_vec.push_back(JoinResult{
                                    .r_id = pr.id,
                                    .r_payload = pr.payload,
                                    .s_id = br.id,
                                    .s_payload = br.payload
                                });
                            }
                        }
                        curr = next[curr];
                    }
                }

                auto tp2 = std::chrono::high_resolution_clock::now();
                local_probe_time += std::chrono::duration<double, std::milli>(tp2 - tb2).count();
            }

            thread_build_ms[tid] = local_build_time;
            thread_probe_ms[tid] = local_probe_time;
        }

        size_t total_found = 0;
        for (const auto& vec : part_results) {
            total_found += vec.size();
        }
        results.reserve(total_found);

        for (auto& vec : part_results) {
            results.insert(results.end(), vec.begin(), vec.end());
        }

        auto t_end = std::chrono::high_resolution_clock::now();

        if (metrics) {
            double max_build = 0.0;
            double max_probe = 0.0;
            for (int t = 0; t < num_threads; ++t) {
                if (thread_build_ms[t] > max_build) max_build = thread_build_ms[t];
                if (thread_probe_ms[t] > max_probe) max_probe = thread_probe_ms[t];
            }
            metrics->partition_time_ms = std::chrono::duration<double, std::milli>(t_part_end - t_part_start).count();
            metrics->build_time_ms = max_build;
            metrics->probe_time_ms = max_probe;
            metrics->total_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            metrics->result_count = results.size();
        }

        return results;
    }
};