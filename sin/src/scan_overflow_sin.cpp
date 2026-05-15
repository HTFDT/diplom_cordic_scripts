// scan_overflow_quadrant0.cpp
// Перебор только квадранта 00: меняем лишь 30 младших бит (theta), 2 старших = 0
// Ищем переполнение в x (cos) или y (sin) на любой итерации, без saturate.

#include <cstdint>
#include <iostream>
#include <iomanip>
#include <vector>
#include <fstream>

#include "const_generator_sin.h"

static inline int64_t asr(int64_t v, int sh) { return v >> sh; }

static inline int64_t truncate(int64_t val, int bits) {
    uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
    val &= (int64_t)mask;
    int64_t sign_bit = 1LL << (bits - 1);
    if (val & sign_bit) val |= ~(int64_t)mask;
    return val;
}

static inline bool add_overflow_tc(int64_t a, int64_t b, int bits, int64_t& full) {
    full = a + b;
    const int64_t maxv = (1LL << (bits - 1)) - 1;
    const int64_t minv = -(1LL << (bits - 1));
    return (full > maxv) || (full < minv);
}

static inline bool sub_overflow_tc(int64_t a, int64_t b, int bits, int64_t& full) {
    full = a - b;
    const int64_t maxv = (1LL << (bits - 1)) - 1;
    const int64_t minv = -(1LL << (bits - 1));
    return (full > maxv) || (full < minv);
}

enum class Comp { X, Y };

struct OverflowEvent {
    uint32_t input;  // полный 32-битный вход (квадрант уже зашит в 00)
    int iter;
    Comp comp;
};

static inline std::string to_bin32(uint32_t x)
{
    std::string s(32, '0');
    for (int i = 31; i >= 0; --i) {
        s[31 - i] = ((x >> i) & 1u) ? '1' : '0';
    }
    return s;
}

static inline void print_event_slim(OverflowEvent& ev, std::ostream& out)
{
    out
        << "in=" << to_bin32(ev.input)
        << " iter=" << ev.iter
        << " comp=" << (ev.comp == Comp::X ? "cos(x)" : "sin(x)") << "\n";
}

static bool cordic_overflow_quadrant0(
    uint32_t theta30,
    int bits,
    const std::vector<int64_t>& atan_table,
    int64_t k_inv,
    OverflowEvent& out)
{
    // Формируем полный вход: quadrant=00, theta=30 бит
    // input_fixed: [q1 q0 | theta(29..0)]
    const uint32_t input_u32 = (theta30 & ((1u << 30) - 1u));
    const int64_t input_fixed = (int64_t)input_u32;

    // theta внутри квадранта
    int64_t theta = (int64_t)(input_u32 & ((1u << 30) - 1u));
    int64_t z0 = theta << 1; // как в твоём compute_sin

    int64_t x = k_inv;
    int64_t y = 0;
    int64_t z = z0;

    const int niter = (int)atan_table.size();

    for (int i = 0; i < niter; i++) {
        int64_t x_shifted = asr(x, i);
        int64_t y_shifted = asr(y, i);

        int64_t x_full = 0, y_full = 0, z_full = 0;
        bool ofx = false, ofy = false;

        if (z >= 0) {
            ofx = sub_overflow_tc(x, y_shifted, bits, x_full); // x - y>>i
            ofy = add_overflow_tc(y, x_shifted, bits, y_full); // y + x>>i
            z_full = z - atan_table[i];
        } else {
            ofx = add_overflow_tc(x, y_shifted, bits, x_full); // x + y>>i
            ofy = sub_overflow_tc(y, x_shifted, bits, y_full); // y - x>>i
            z_full = z + atan_table[i];
        }

        if (ofx) {
            out = OverflowEvent{input_u32, i, Comp::X};
            return true;
        }
        if (ofy) {
            out = OverflowEvent{input_u32, i, Comp::Y};
            return true;
        }

        x = truncate(x_full, bits);
        y = truncate(y_full, bits);
        z = truncate(z_full, bits);
    }

    return false;
}

int main() {
    const int bits = 32;
    const int iterations = 32;

    auto atan_table = generate_atan_table(bits, iterations);
    int64_t k_inv   = generate_k_inv(bits, iterations);
    // k_inv -= 5;
    // std::cerr << "k_inv adjusted: " << k_inv << "\n";

    const uint64_t total = (1ULL << 30);
    uint64_t found = 0;

    OverflowEvent ev{};

    std::ofstream out("overflow_cases.txt", std::ios::out | std::ios::trunc);
    if(!out) {
        std::cerr << "cannot open overflow_cases.txt\n";
        return 1;
    }

    for (uint64_t t = 0; t < total; t++) {
        uint32_t theta30 = (uint32_t)t;

        if (cordic_overflow_quadrant0(theta30, bits, atan_table, k_inv, ev)) {
            print_event_slim(ev, out);
            found++;
        }

        // прогресс раз в ~16 млн
        if ((t & 0x00FFFFFFULL) == 0) {
            std::cerr << "progress: " << t << "/" << total
                      << " (" << std::fixed << std::setprecision(2)
                      << (100.0 * (double)t / (double)total) << "%)"
                      << " found=" << found << "\n";
        }
    }

    std::cerr << "done. found=" << found << "\n";
    return 0;
}
