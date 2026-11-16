#include <iostream>
#include <iomanip>
#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include "data_generator.hpp"
#include "nested_loop_join.hpp"
#include "seq_hash_join.hpp"
#include "seq_hash_join_open_addr.hpp"
#include "parallel_shared_hash_join.hpp"
#include "parallel_partitioned_hash_join.hpp"
#include "verification.hpp"

void run_verification() {
    std::cout << "=== VERIFIKACIJA TACNOSTI SVIH VARIJANTI ===\n";
    if (!Verification::run_all_seq_tests()) {
        std::cerr << "GRESKA u sekvencijalnim testovima!\n";
        return;
    }

    auto R = DataGenerator::generate_uniform(10000, 1, 5000, 42);
    auto S = DataGenerator::generate_uniform(40000, 1, 5000, 99);

    auto seq_res = SeqHashJoin::join(R, S);
    std::sort(seq_res.begin(), seq_res.end());

    auto open_res = SeqHashJoinOpenAddr::join(R, S);
    std::sort(open_res.begin(), open_res.end());
    std::cout << "[TEST] Open Addressing (10k x 40k): "
              << (seq_res == open_res ? "PROSAO" : "GRESKA") << "\n";

    for (int threads : {1, 2, 4, 8}) {
        auto shared_res = ParallelSharedHashJoin::join(R, S, threads);
        std::sort(shared_res.begin(), shared_res.end());

        auto part_res = ParallelPartitionedHashJoin::join(R, S, threads, nullptr, 6);
        std::sort(part_res.begin(), part_res.end());

        std::cout << "[TEST] Paralelne verzije (" << threads << " niti): "
                  << "Shared=" << (seq_res == shared_res ? "PROSAO" : "GRESKA")
                  << " | Partitioned=" << (seq_res == part_res ? "PROSAO" : "GRESKA") << "\n";
    }
    std::cout << "============================================\n\n";
}

void run_exp1_nested_vs_hash(size_t r_size, size_t s_size) {
    auto R = DataGenerator::generate_uniform(r_size, 1, static_cast<int64_t>(r_size), 42);
    auto S = DataGenerator::generate_uniform(s_size, 1, static_cast<int64_t>(r_size), 99);

    auto t1 = std::chrono::high_resolution_clock::now();
    auto res_nl = NestedLoopJoin::join(R, S);
    auto t2 = std::chrono::high_resolution_clock::now();
    double nl_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    JoinMetrics hj_m;
    auto res_hj = SeqHashJoin::join(R, S, &hj_m);

    double speedup = nl_ms / hj_m.total_time_ms;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Skup (" << r_size << " x " << s_size << ") | Parova: " << res_hj.size() << "\n";
    std::cout << "  Nested Loop Join : " << nl_ms << " ms\n";
    std::cout << "  Seq Hash Join    : " << hj_m.total_time_ms << " ms "
              << "(Build: " << hj_m.build_time_ms << " ms, Probe: " << hj_m.probe_time_ms << " ms)\n";
    std::cout << "  Ubrzanje         : " << speedup << "x\n\n";
}

void run_exp2_chaining_vs_open(
    const std::string& label,
    size_t r_size,
    size_t s_size,
    int64_t max_key,
    bool is_skewed = false,
    double skew_theta = 0.0
) {
    std::vector<Tuple> R, S;
    if (is_skewed) {
        R = DataGenerator::generate_zipfian(r_size, max_key, skew_theta, 42);
        S = DataGenerator::generate_zipfian(s_size, max_key, skew_theta, 99);
    } else {
        R = DataGenerator::generate_uniform(r_size, 1, max_key, 42);
        S = DataGenerator::generate_uniform(s_size, 1, max_key, 99);
    }

    SeqHashJoin::join(R, S);
    SeqHashJoinOpenAddr::join(R, S);

    JoinMetrics m_chain;
    auto res_chain = SeqHashJoin::join(R, S, &m_chain);

    JoinMetrics m_open;
    auto res_open = SeqHashJoinOpenAddr::join(R, S, &m_open);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Scenario: " << label << " | Parova: " << res_chain.size() << "\n";
    std::cout << "  Flat Chaining   -> Ukupno: " << m_chain.total_time_ms << " ms "
              << "(Build: " << m_chain.build_time_ms << " ms, Probe: " << m_chain.probe_time_ms << " ms)\n";
    std::cout << "  Open Addressing -> Ukupno: " << m_open.total_time_ms << " ms "
              << "(Build: " << m_open.build_time_ms << " ms, Probe: " << m_open.probe_time_ms << " ms)\n\n";
}

void run_exp3_parallel_scaling(
    const std::string& label,
    size_t r_size,
    size_t s_size,
    int64_t max_key,
    bool is_skewed = false,
    double skew_theta = 0.0,
    int radix_bits = 6
) {
    std::vector<Tuple> R, S;
    if (is_skewed) {
        R = DataGenerator::generate_zipfian(r_size, max_key, skew_theta, 42);
        S = DataGenerator::generate_zipfian(s_size, max_key, skew_theta, 99);
    } else {
        R = DataGenerator::generate_uniform(r_size, 1, max_key, 42);
        S = DataGenerator::generate_uniform(s_size, 1, max_key, 99);
    }

    JoinMetrics seq_m;
    auto seq_res = SeqHashJoin::join(R, S, &seq_m);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Scenario: " << label << " | Parova: " << seq_res.size() << "\n";
    std::cout << "  Seq Baseline    -> Ukupno: " << seq_m.total_time_ms << " ms\n\n";

    for (int threads : {1, 2, 4, 8}) {
        JoinMetrics m_shared;
        ParallelSharedHashJoin::join(R, S, threads, &m_shared);

        PartitionedJoinMetrics m_part;
        ParallelPartitionedHashJoin::join(R, S, threads, &m_part, radix_bits);

        double sp_a = seq_m.total_time_ms / m_shared.total_time_ms;
        double ef_a = (sp_a / threads) * 100.0;
        double sp_b = seq_m.total_time_ms / m_part.total_time_ms;
        double ef_b = (sp_b / threads) * 100.0;

        std::cout << "  Niti: " << threads << "\n";
        std::cout << "    [A] Shared Table      -> Ukupno: " << m_shared.total_time_ms << " ms"
                  << " | Speedup: " << sp_a << "x | Efikasnost: " << ef_a << "%\n";
        std::cout << "    [B] Partitioned Radix -> Ukupno: " << m_part.total_time_ms << " ms"
                  << " (Part: " << m_part.partition_time_ms
                  << " ms, Build: " << m_part.build_time_ms
                  << " ms, Probe: " << m_part.probe_time_ms << " ms)"
                  << " | Speedup: " << sp_b << "x | Efikasnost: " << ef_b << "%\n";
    }
    std::cout << "\n";
}

int main() {
    run_verification();

    std::cout << "=== EKSPERIMENT 1: NESTED LOOP vs. SEQ HASH JOIN ===\n";
    run_exp1_nested_vs_hash(5000, 20000);
    run_exp1_nested_vs_hash(15000, 50000);
    run_exp1_nested_vs_hash(30000, 100000);

    std::cout << "=== EKSPERIMENT 2: FLAT CHAINING vs. OPEN ADDRESSING ===\n";
    run_exp2_chaining_vs_open("Uniformni (1M x 5M)", 1000000, 5000000, 1000000, false);
    run_exp2_chaining_vs_open("Skewed / Zipfian 0.9 (50k x 200k)", 50000, 200000, 10000, true, 0.9);

    std::cout << "=== EKSPERIMENT 3: PARALELNO SKALIRANJE (VARIJANTA A vs. VARIJANTA B) ===\n";
    run_exp3_parallel_scaling("Uniformni (1M x 5M)", 1000000, 5000000, 1000000, false);
    run_exp3_parallel_scaling("Skewed / Zipfian 1.1 (25k x 100k)", 25000, 100000, 5000, true, 1.1);

    return 0;
}