#include <iostream>
#include <iomanip>
#include <chrono>
#include "data_generator.hpp"
#include "nested_loop_join.hpp"
#include "seq_hash_join.hpp"
#include "seq_hash_join_open_addr.hpp"

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

void run_exp2_chaining_vs_open(const std::string& label, const std::vector<Tuple>& R, const std::vector<Tuple>& S) {
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

int main() {
    std::cout << "=== EKSPERIMENT 1 (RELEASE): NESTED LOOP vs. SEQ HASH JOIN ===\n";
    run_exp1_nested_vs_hash(5000, 20000);
    run_exp1_nested_vs_hash(15000, 50000);
    run_exp1_nested_vs_hash(30000, 100000);

    std::cout << "=== EKSPERIMENT 2 (RELEASE): FLAT CHAINING vs. OPEN ADDRESSING ===\n";
    auto R_unif = DataGenerator::generate_uniform(1000000, 1, 1000000, 42);
    auto S_unif = DataGenerator::generate_uniform(5000000, 1, 1000000, 99);
    run_exp2_chaining_vs_open("Uniformni (1M x 5M)", R_unif, S_unif);

    auto R_skew = DataGenerator::generate_zipfian(50000, 10000, 0.9, 42);
    auto S_skew = DataGenerator::generate_zipfian(200000, 10000, 0.9, 99);
    run_exp2_chaining_vs_open("Skewed / Zipfian 0.9 (50k x 200k)", R_skew, S_skew);

    return 0;
}