#pragma once

#include "types.hpp"
#include "data_generator.hpp"
#include "nested_loop_join.hpp"
#include "seq_hash_join.hpp"
#include <vector>
#include <algorithm>
#include <iostream>
#include <string>
#include <cassert>

class Verification {
private:
    static bool verify_case(
        const std::string& test_name,
        const std::vector<Tuple>& R,
        const std::vector<Tuple>& S
    ) {
        std::vector<JoinResult> expected = NestedLoopJoin::join(R, S);
        JoinMetrics metrics;
        std::vector<JoinResult> actual = SeqHashJoin::join(R, S, &metrics);

        std::sort(expected.begin(), expected.end());
        std::sort(actual.begin(), actual.end());

        bool passed = (expected == actual);

        std::cout << "[TEST] " << test_name << ": "
                  << (passed ? "PROSAO" : "PAO")
                  << " | Pronadjeno parova: " << actual.size()
                  << " | HashJoin vrijeme: " << metrics.total_time_ms << " ms\n";

        return passed;
    }

public:
    static bool run_all_seq_tests() {
        std::cout << "=== POKRETANJE TESTOVA TACNOSTI  ===\n";
        bool all_passed = true;

        auto r_empty = DataGenerator::generate_empty();
        auto s_small = DataGenerator::generate_uniform(100, 1, 50, 1);
        all_passed &= verify_case("1. Prazna tabela R", r_empty, s_small);
        all_passed &= verify_case("2. Prazna tabela S", s_small, r_empty);

        auto r_no_match = DataGenerator::generate_uniform(1000, 1, 500, 2);
        auto s_no_match = DataGenerator::generate_uniform(2000, 1000, 2000, 3);
        all_passed &= verify_case("3. Nepostojeci kljucevi (0 poklapanja)", r_no_match, s_no_match);

        auto r_dup = DataGenerator::generate_constant_key(200, 7, 4);
        auto s_dup = DataGenerator::generate_constant_key(300, 7, 5);
        all_passed &= verify_case("4. 100% duplikati (200 x 300 = 60.000 parova)", r_dup, s_dup);

        auto r_unif = DataGenerator::generate_uniform(5000, 1, 2500, 6);
        auto s_unif = DataGenerator::generate_uniform(20000, 1, 2500, 7);
        all_passed &= verify_case("5. Uniformni skup (5k x 20k)", r_unif, s_unif);

        auto r_swap = DataGenerator::generate_uniform(15000, 1, 3000, 8);
        auto s_swap = DataGenerator::generate_uniform(3000, 1, 3000, 9);
        all_passed &= verify_case("6. Obrnute velicine (R vece od S: 15k x 3k)", r_swap, s_swap);

        auto r_skew = DataGenerator::generate_zipfian(5000, 1000, 1.2, 10);
        auto s_skew = DataGenerator::generate_zipfian(20000, 1000, 1.2, 11);
        all_passed &= verify_case("7. Skewed / Zipfian skup (5k x 20k)", r_skew, s_skew);

        std::cout << "============================================\n";
        std::cout << (all_passed ? "SVI TESTOVI SU USPJESNO PROSLI!\n\n" : "NEKI TESTOVI SU PALI!\n\n");
        return all_passed;
    }
};