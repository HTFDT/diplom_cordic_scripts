#pragma once

#include <vector>
#include <cstdint>
#include <cmath>

#include "converter_sin.h"

// ============================================================
// Генерация таблицы arctan(2^{-i}) для CORDIC
// Формат: знаковые Q1.(bits-1)
// Возвращает вектор из iterations констант (i = 0 .. iterations-1)
// ============================================================
inline std::vector<int64_t> generate_atan_table(int bits, int iterations) {
    std::vector<int64_t> table(iterations);
    double scale = (double)(1LL << (bits - 1));
    double half_pi = PI() / 2.0;
    for (int i = 0; i < iterations; i++) {
        double atan_val = atan(pow(2.0, -(double)i));
        double normalized = atan_val / half_pi;  // доля от pi/2
        table[i] = (int64_t)llround(normalized * scale);
    }
    return table;
}

static inline unsigned ceil_log2(uint64_t x) {
    if (x == 0) throw std::invalid_argument("ceil_log2 undefined for x=0");
    unsigned k = 0;
    uint64_t p = 1;
    while (p < x) { p <<= 1; ++k; }
    return k;
}

// ============================================================
// Генерация K_inv (обратный коэффициент усиления CORDIC)
// K = П_{i=0}^{n-1} sqrt(1 + 2^{-2i})
// K_inv = 1/K ≈ 0.6073 (для большого n)
// Формат: знаковый Q1.(bits-1)
// ============================================================
inline int64_t generate_k_inv(int bits, int iterations) {
    double K = 1.0;
    for (int i = 0; i < iterations; i++) {
        K *= sqrt(1.0 + pow(2.0, -2.0 * i));
    }
    double K_inv = 1.0 / K;
    double scale = (double)(1LL << (bits - 1));
    return (int64_t)llround(K_inv * scale) - ceil_log2(bits);
}
