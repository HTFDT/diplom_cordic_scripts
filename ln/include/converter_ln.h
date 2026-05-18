#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <cmath>


inline int64_t ln_real_to_fixed(double a, int bits) {
    // Требование: a in [0.5, 1)
    if (!(a >= 0.5 && a < 1.0)) {
        throw std::invalid_argument("аргумент должен принадлежать диапазону [0.5, 1). Получено: " + std::to_string(a));
    }

    // Формат: signed Q1.(bits-1), т.е. scale = 2^(bits-1)
    double scale = (double)(1LL << (bits - 1));
    int64_t fixed = (int64_t)llround(a * scale);

    // Клиппинг
    int64_t max_val = (1LL << (bits - 1)) - 1;
    int64_t min_val = -(1LL << (bits - 1));

    if (fixed > max_val) fixed = max_val;
    if (fixed < min_val) fixed = min_val;

    return fixed;
}

inline double ln_fixed_to_real(int64_t a_fixed, int bits) {
    // Интерпретация как Q1.(bits-1)
    double scale = (double)(1LL << (bits - 1));
    return (double)a_fixed / scale;
}
