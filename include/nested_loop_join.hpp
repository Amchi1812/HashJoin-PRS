#pragma once

#include "types.hpp"
#include <vector>

class NestedLoopJoin {
public:
    static std::vector<JoinResult> join(const std::vector<Tuple>& R, const std::vector<Tuple>& S) {
        std::vector<JoinResult> results;

        for (const auto& r : R) {
            for (const auto& s : S) {
                if (r.key == s.key) {
                    results.push_back(JoinResult{
                        .r_id = r.id,
                        .r_payload = r.payload,
                        .s_id = s.id,
                        .s_payload = s.payload
                    });
                }
            }
        }

        return results;
    }
};