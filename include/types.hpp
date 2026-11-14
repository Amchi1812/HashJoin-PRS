#pragma once

#include <cstdint>
#include <compare>

struct Tuple {
    int64_t id;
    int64_t key;
    int64_t payload;

    bool operator==(const Tuple& other) const = default;
};

struct JoinResult {
    int64_t r_id;
    int64_t r_payload;
    int64_t s_id;
    int64_t s_payload;

    auto operator<=>(const JoinResult&) const = default;
};