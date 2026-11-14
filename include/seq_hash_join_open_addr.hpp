#pragma once

#include "types.hpp"
#include "seq_hash_join.hpp"
#include <vector>
#include <cstdint>
#include <chrono>

class SeqHashJoinOpenAddr {
private:
    struct Slot {
        int64_t key;
        int32_t tuple_idx;
    };

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

        size_t num_slots = next_power_of_two(build_rel.size() * 2);
        size_t mask = num_slots - 1;

        std::vector<Slot> table(num_slots, Slot{0, -1});

        auto t_build_start = std::chrono::high_resolution_clock::now();

        for (size_t i = 0; i < build_rel.size(); ++i) {
            size_t pos = hash_key(build_rel[i].key, mask);
            while (table[pos].tuple_idx != -1) {
                pos = (pos + 1) & mask;
            }
            table[pos] = Slot{build_rel[i].key, static_cast<int32_t>(i)};
        }

        auto t_build_end = std::chrono::high_resolution_clock::now();

        for (const auto& p : probe_rel) {
            size_t pos = hash_key(p.key, mask);

            while (table[pos].tuple_idx != -1) {
                if (table[pos].key == p.key) {
                    const Tuple& b = build_rel[table[pos].tuple_idx];
                    if (r_is_smaller) {
                        results.push_back(JoinResult{
                            .r_id = b.id,
                            .r_payload = b.payload,
                            .s_id = p.id,
                            .s_payload = p.payload
                        });
                    } else {
                        results.push_back(JoinResult{
                            .r_id = p.id,
                            .r_payload = p.payload,
                            .s_id = b.id,
                            .s_payload = b.payload
                        });
                    }
                }
                pos = (pos + 1) & mask;
            }
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