#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

#include "int_utils.h"

struct LnIteration {
    int i;
    int64_t x;
    int64_t y;
    int64_t z;
};

struct LnResult {
    int64_t arg_fixed;
    double arg_double;

    int64_t result_fixed;
    double result_double;

    int64_t reference_fixed;
    double reference_double;

    int64_t diff_fixed;

    std::vector<LnIteration> iterations;
};

inline LnResult compute_ln(
    int64_t a_fixed,                    // signed Q1.(bits-1), a в диапазоне [0.5,1)
    double a_double,
    int bits,
    const std::vector<int>& schedule,   // длина = кол-во шагов
    const std::vector<int64_t>& atanh_table
) {
    LnResult res;
    res.arg_fixed = a_fixed;
    res.arg_double = a_double;

    if ((int)schedule.size() != (int)atanh_table.size())
        throw std::invalid_argument("размеры schedule и atanh_table не совпадают");
    
    // 0.5 в Q1.(bits-1)
    const int64_t HALF = (int64_t)(1LL << (bits - 2));

    // a_half = a/2
    int64_t a_half = asr(a_fixed, 1);

    int64_t x = truncate(a_half + HALF, bits); // (a+1)/2
    int64_t y = truncate(a_half - HALF, bits); // (a-1)/2
    int64_t z = 0;

    res.iterations.reserve(schedule.size() + 1);
    res.iterations.push_back({0, x, y, z});

    for (size_t k = 0; k < schedule.size(); k++) {
        int i = schedule[k];
        int64_t x_shift = asr(x, i);
        int64_t y_shift = asr(y, i);

        int64_t x_new, y_new, z_new;
        // d = +1 if y < 0 else -1
        if (y < 0) {
            x_new = x + y_shift;
            y_new = y + x_shift;
            z_new = z - atanh_table[k];
        } else {
            x_new = x - y_shift;
            y_new = y - x_shift;
            z_new = z + atanh_table[k];
        }
        
        x = truncate(x_new, bits);
        y = truncate(y_new, bits);
        z = truncate(z_new, bits);

        res.iterations.push_back({i, x, y, z});
    }

    // Для ln(a): z_out = atanh((a-1)/(a+1)), ln = 2*z_out
    int64_t ln_fixed = truncate(z << 1, bits);

    res.result_fixed  = ln_fixed;
    res.result_double = (double)ln_fixed / (double)(1LL << (bits - 1));

    double ref = std::log(a_double);
    res.reference_fixed  = saturate((int64_t)llround(ref * (double)(1LL << (bits - 1))), bits);
    res.reference_double = (double)res.reference_fixed / (double)(1LL << (bits - 1));
    res.diff_fixed = res.result_fixed - res.reference_fixed;

    return res;
}
