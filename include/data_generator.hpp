#pragma once

#include "types.hpp"
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
#include <iterator>

class DataGenerator {
public:
    static std::vector<Tuple> generate_uniform(size_t num_rows, int64_t min_key, int64_t max_key, uint64_t seed = 42) {
        std::vector<Tuple> table;
        table.reserve(num_rows);

        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int64_t> key_dist(min_key, max_key);
        std::uniform_int_distribution<int64_t> val_dist(100, 999999);

        for (size_t i = 0; i < num_rows; ++i) {
            table.push_back(Tuple{
                .id = static_cast<int64_t>(i + 1),
                .key = key_dist(rng),
                .payload = val_dist(rng)
            });
        }
        return table;
    }

    static std::vector<Tuple> generate_zipfian(size_t num_rows, int64_t max_key, double skew_factor = 1.0, uint64_t seed = 42) {
        std::vector<Tuple> table;
        if (num_rows == 0 || max_key <= 0) return table;
        table.reserve(num_rows);

        std::vector<double> cdf(max_key);
        double sum = 0.0;
        for (int64_t i = 1; i <= max_key; ++i) {
            sum += 1.0 / std::pow(static_cast<double>(i), skew_factor);
            cdf[i - 1] = sum;
        }
        for (double& val : cdf) {
            val /= sum;
        }

        std::mt19937_64 rng(seed);
        std::uniform_real_distribution<double> prob_dist(0.0, 1.0);
        std::uniform_int_distribution<int64_t> val_dist(100, 999999);

        for (size_t i = 0; i < num_rows; ++i) {
            double p = prob_dist(rng);
            auto it = std::lower_bound(cdf.begin(), cdf.end(), p);
            int64_t key = static_cast<int64_t>(std::distance(cdf.begin(), it)) + 1;

            table.push_back(Tuple{
                .id = static_cast<int64_t>(i + 1),
                .key = key,
                .payload = val_dist(rng)
            });
        }
        return table;
    }

    static std::vector<Tuple> generate_constant_key(size_t num_rows, int64_t constant_key, uint64_t seed = 42) {
        std::vector<Tuple> table;
        table.reserve(num_rows);

        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<int64_t> val_dist(100, 999999);

        for (size_t i = 0; i < num_rows; ++i) {
            table.push_back(Tuple{
                .id = static_cast<int64_t>(i + 1),
                .key = constant_key,
                .payload = val_dist(rng)
            });
        }
        return table;
    }

    static std::vector<Tuple> generate_empty() {
        return {};
    }
};