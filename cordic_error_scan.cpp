// cordic_error_scan.cpp
// Полный перебор всех 2^32 входов (BAM: 2 бита квадранта + 30 бита угла),
// вычисление CORDIC sin как в cordic_sin.cpp, но с k_inv -= 5.
// Выводит только прогресс в stderr и итоговую статистику ошибки в конце.
//
// Требует твои заголовки/реализации:
//   - converter.h (fixed_result_to_double, to_bin_string если нужно)
//   - const_generator.h (generate_atan_table, generate_k_inv)
//   - PI() где-то доступна (как в твоём проекте)
//
// Сборка будет долгой по времени выполнения: 2^32 * 32 итерации.

#include "converter.h"
#include "const_generator.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>

// ============================================================
//  Целочисленная арифметика для моделирования Минитеры
// ============================================================

static inline int64_t asr(int64_t val, int shift) {
    return val >> shift;
}

static inline int64_t truncate(int64_t val, int bits) {
    uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
    val &= (int64_t)mask;
    int64_t sign_bit = 1LL << (bits - 1);
    if (val & sign_bit) {
        val |= ~(int64_t)mask;
    }
    return val;
}

// насыщение оставляем только для эталона, как в исходнике
static inline int64_t saturate(int64_t val, int bits) {
    int64_t max_val = (1LL << (bits - 1)) - 1;
    int64_t min_val = -(1LL << (bits - 1));
    return std::max(min_val, std::min(max_val, val));
}

static inline std::string to_bin32(uint32_t x) {
    std::string s(32, '0');
    for (int i = 31; i >= 0; --i) {
        s[31 - i] = ((x >> i) & 1u) ? '1' : '0';
    }
    return s;
}

// ============================================================
//  Структуры
// ============================================================

struct CordicIteration {
    int64_t cos_val;   // x_i
    int64_t sin_val;   // y_i
    int64_t z_val;     // z_i
};

struct CordicResult {
    int64_t arg_fixed;
    double  arg_rad;
    int64_t result_fixed;
    double  result_double;
    int64_t reference_fixed;
    double  reference_double;
    int64_t diff_fixed;
    std::vector<CordicIteration> iterations;
};

// ============================================================
//  ОРИГИНАЛЬНАЯ compute_sin (с небольшой правкой: вход rad считаем по fixed)
// ============================================================

static CordicResult compute_sin(
    int64_t input_fixed,               // вход беззнаковый в Q0.(bits)
    int     bits,
    const std::vector<int64_t>& atan_table,
    int64_t k_inv)
{
    CordicResult res;
    res.arg_fixed = input_fixed;

    // вычислим rad из fixed (в оригинале это приходило снаружи)
    res.arg_rad = angle_fixed_to_rad(input_fixed, bits);

    int num_iterations = (int)atan_table.size();

    int quadrant_bits = bits - 2;   // 30 для 32

    int64_t quadrant = (input_fixed >> quadrant_bits) & 0b11;
    int64_t Q0 = quadrant & 1;           // выбор sin/cos
    int64_t Q1 = (quadrant >> 1) & 1;    // инверсия знака

    int64_t theta = input_fixed & ((1LL << quadrant_bits) - 1);
    int64_t z0 = theta << 1;

    int64_t x = k_inv;
    int64_t y = 0;
    int64_t z = z0;

    res.iterations.reserve(num_iterations + 1);
    res.iterations.push_back({x, y, z});

    for (int i = 0; i < num_iterations; i++) {
        int64_t x_shifted = asr(x, i);
        int64_t y_shifted = asr(y, i);

        int64_t x_new, y_new, z_new;

        if (z >= 0) {
            x_new = x - y_shifted;
            y_new = y + x_shifted;
            z_new = z - atan_table[i];
        } else {
            x_new = x + y_shifted;
            y_new = y - x_shifted;
            z_new = z + atan_table[i];
        }

        // ВАЖНО: saturate на итерациях НЕ применяем (по твоей задаче)
        x_new = saturate(x_new, bits);
        y_new = saturate(y_new, bits);
        // z_new = saturate(z_new, bits);

        x = truncate(x_new, bits);
        y = truncate(y_new, bits);
        z = truncate(z_new, bits);

        res.iterations.push_back({x, y, z});
    }

    int64_t selected = Q0 ? x : y;
    if (Q1) selected = -selected;

    res.result_fixed  = truncate(selected, bits);
    res.result_double = fixed_result_to_double(res.result_fixed, bits);

    // Эталон
    double ref_sin = std::sin(res.arg_rad);
    res.reference_fixed = saturate((int64_t)llround(ref_sin * (double)(1LL << (bits - 1))), bits);
    res.reference_double = fixed_result_to_double(res.reference_fixed, bits);
    res.diff_fixed = res.result_fixed - res.reference_fixed;

    return res;
}

static inline std::string to_bin32_q131(int64_t v) {
    uint32_t u = (uint32_t)(v & 0xFFFFFFFFULL);
    std::string s(32, '0');
    for (int i = 31; i >= 0; --i) {
        s[31 - i] = ((u >> i) & 1u) ? '1' : '0';
    }
    return s;
}


// ============================================================
// main: полный перебор
// ============================================================

int main() {
    const int64_t PRINT_THRESHOLD = 20; // например: печатать abs(diff_fixed) > 21

    const int bits = 32;
    const int iterations = 32;

    auto atan_table = generate_atan_table(bits, iterations);
    int64_t k_inv   = generate_k_inv(bits, iterations);

    // искусственно уменьшаем, как ты нашёл
    k_inv -= 5;
    if (k_inv < 0) k_inv = 0;

    const uint64_t total = (1ULL << 32);

    int64_t max_abs_err = 0;
    uint32_t arg_at_max = 0;
    int64_t sum_abs_err = 0; // может переполниться, если хранить в int64; поэтому будем суммировать в long double отдельно
    long double sum_abs_err_ld = 0.0L;

    for (uint64_t u = 0; u < total; u++) {
        uint32_t in_u32 = (uint32_t)u;
        int64_t in_fixed = (int64_t)in_u32;

        CordicResult r = compute_sin(in_fixed, bits, atan_table, k_inv);

        int64_t err = r.diff_fixed;
        int64_t abs_err = (err >= 0) ? err : -err;

        sum_abs_err_ld += (long double)abs_err;

        if (abs_err > PRINT_THRESHOLD) {
            // input_fixed — это беззнаковый угол, но ты просил "в формате Q1.31".
            // Здесь выводим 32 бита входного слова как есть (MSB-first).
            std::cout << "in(Q1.31 bits)=" << to_bin32_q131(in_fixed)
                    << " abs_err=" << abs_err
                    << " diff=" << r.diff_fixed
                    << "\n";
        }


        if (abs_err > max_abs_err) {
            max_abs_err = abs_err;
            arg_at_max = in_u32;
        }

        // прогресс раз в ~16 млн (2^24)
        if ((u & 0x00FFFFFFULL) == 0) {
            std::cerr << "progress: " << u << "/" << total
                      << " (" << std::fixed << std::setprecision(2)
                      << (100.0 * (double)u / (double)total) << "%)"
                      << " max_abs_err=" << max_abs_err
                      << "\n";
        }
    }

    long double mean_abs_err = sum_abs_err_ld / (long double)total;

    std::cerr << "DONE\n";
    std::cerr << "k_inv_used=" << k_inv << "\n";
    std::cerr << "max_abs_err_LSB=" << max_abs_err
          << " at in=" << to_bin32(arg_at_max)
          << "\n";
    std::cerr << "mean_abs_err_LSB=" << (double)mean_abs_err << "\n";

    return 0;
}
