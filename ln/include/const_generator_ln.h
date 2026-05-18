#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <algorithm>

inline std::vector<int> generate_hyperbolic_schedule(int iterations) {
    if (iterations < 1) throw std::invalid_argument("iterations должно быть >= 1");

    std::vector<int> schedule;
    schedule.reserve((size_t)iterations + 8); // грубый запас

    int r = 4;
    for (int i = 1; i <= iterations; i++) {
        schedule.push_back(i);

        if (i == r) {
            schedule.push_back(i);   // повтор
            r = r * 3 + 1;           // 4, 13, 40, 121, ...
        }
    }
    return schedule;
}


// atanh(2^-i) в signed Q1.(bits-1)
inline std::vector<int64_t> generate_atanh_table(int bits, const std::vector<int>& schedule) {
    std::vector<int64_t> table;
    table.reserve(schedule.size());

    double scale = (double)(1LL << (bits - 1));
    for (int i : schedule) {
        double val = std::atanh(std::pow(2.0, -(double)i));
        int64_t fx = (int64_t)llround(val * scale);
        table.push_back(fx);
    }
    return table;
}

// Kh = Π sqrt(1 - 2^(-2i))^{-1}  по шагам schedule (с повторами)
inline double compute_Kh(const std::vector<int>& schedule) {
    double Kh = 1.0L;
    for (int i : schedule) {
        // sqrt(1 - 2^{-2i})^{-1}
        double factor = std::sqrt(1.0 - std::pow(2.0, -2.0 * i));
        Kh *= 1.0 / factor;
    }
    return Kh;
}

// 1/Kh в signed Q1.(bits-1)
inline int64_t generate_inv_Kh(int bits, const std::vector<int>& schedule) {
    double Kh = compute_Kh(schedule);
    double inv = 1.0 / Kh;
    double scale = (double)(1LL << (bits - 1));
    int64_t fx = (int64_t)llround(inv * scale);

    // клиппинг
    int64_t max_val = (1LL << (bits - 1)) - 1;
    int64_t min_val = -(1LL << (bits - 1));
    if (fx > max_val) fx = max_val;
    if (fx < min_val) fx = min_val;

    return fx;
}
