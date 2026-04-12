#include <cstdint>
#include <cmath>
#include <cstdio>
#include <string>
#include <stdexcept>

static constexpr int ANG_FRAC = 38;
static constexpr int ITERS = 38;

static std::string bin_u32(uint32_t v) {
    std::string s; s.reserve(32);
    for (int i = 31; i >= 0; --i) s.push_back(((v >> i) & 1u) ? '1' : '0');
    return s;
}
static std::string bin_u64(uint64_t v) {
    std::string s; s.reserve(64);
    for (int i = 63; i >= 0; --i) s.push_back(((v >> i) & 1ull) ? '1' : '0');
    return s;
}
static std::string bin_i64(int64_t v) { return bin_u64((uint64_t)v); }

int main() {
    long double pi   = acosl(-1.0L);
    long double pio2 = pi / 2.0L;
    long double invpio2 = 2.0L / pi;

    // exact powers of two in long double domain
    long double S32 = ldexpl(1.0L, 32);
    long double S31 = ldexpl(1.0L, 31);
    long double S63 = ldexpl(1.0L, 63);
    long double S38 = ldexpl(1.0L, ANG_FRAC);

    // INVPIO2_Q32 = round((2/pi) * 2^32)  (unsigned)
    uint64_t INVPIO2_Q32 = (uint64_t) llroundl(invpio2 * S32);

    // ---- PIO2 split with TRUNC ----
    // HI: floor((pi/2)*2^31) fits uint32
    long double hi31_ld = floorl(pio2 * S31);
    if (!(hi31_ld >= 0.0L && hi31_ld <= 4294967295.0L)) {
        throw std::runtime_error("PIO2_HI_Q31 out of uint32 range");
    }
    uint32_t PIO2_HI_Q31 = (uint32_t)hi31_ld;

    long double pio2_hi = (long double)PIO2_HI_Q31 / S31;
    long double pio2_lo = pio2 - pio2_hi; // guaranteed >=0 and <2^-31

    // LO: floor(lo*2^63), must fit uint32 because lo < 2^-31 => lo*2^63 < 2^32
    long double lo63_ld = floorl(pio2_lo * S63);
    if (!(lo63_ld >= 0.0L && lo63_ld <= 4294967295.0L)) {
        throw std::runtime_error("PIO2_LO_Q63_32 out of uint32 range");
    }
    uint32_t PIO2_LO_Q63_32 = (uint32_t)lo63_ld;

    // Full pio2 in Q38 (for correction)
    int64_t PIO2_Q38 = (int64_t) llroundl(pio2 * S38);

    // atan table Q38
    int64_t ATAN_Q38[ITERS];
    long double ATAN_REAL[ITERS];
    for (int i = 0; i < ITERS; ++i) {
        long double a = atanl(ldexpl(1.0L, -i));
        ATAN_REAL[i] = a;
        ATAN_Q38[i]  = (int64_t) llroundl(a * S38);
    }

    // Kinv Q38
    long double kinv = 1.0L;
    for (int i = 0; i < ITERS; ++i) {
        long double factor = sqrtl(1.0L + ldexpl(1.0L, -2*i));
        kinv /= factor;
    }
    int64_t KINV_Q38 = (int64_t) llroundl(kinv * S38);

    // ---------------- Диаг ----------------
    std::printf("// --- Диагностика ---\n");
    std::printf("// pi              = %.36Lf\n", pi);
    std::printf("// 2/pi            = %.36Lf\n", invpio2);
    std::printf("// pi/2            = %.36Lf\n", pio2);
    std::printf("// ANG_FRAC        = %d\n", ANG_FRAC);
    std::printf("// ITERS           = %d\n\n", ITERS);

    {
        long double inv_q = (long double)INVPIO2_Q32 / S32;
        std::printf("// INVPIO2_Q32 (2/pi)\n");
        std::printf("//   int  = %llu\n", (unsigned long long)INVPIO2_Q32);
        std::printf("//   hex  = 0x%016llX\n", (unsigned long long)INVPIO2_Q32);
        std::printf("//   bin  = %s\n", bin_u64(INVPIO2_Q32).c_str());
        std::printf("//   dec  = %.36Lf\n", inv_q);
        std::printf("//   err  = %.36Le\n\n", (inv_q - invpio2));
    }

    {
        long double hi_q = (long double)PIO2_HI_Q31 / S31;
        long double lo_q = (long double)PIO2_LO_Q63_32 / S63;
        long double sum_q = hi_q + lo_q;

        std::printf("// PIO2 split with trunc HI/LO\n");
        std::printf("//   PIO2_HI_Q31:\n");
        std::printf("//     int = %u\n", PIO2_HI_Q31);
        std::printf("//     hex = 0x%08X\n", PIO2_HI_Q31);
        std::printf("//     bin = %s\n", bin_u32(PIO2_HI_Q31).c_str());
        std::printf("//     dec = %.36Lf\n", hi_q);

        std::printf("//   PIO2_LO_Q63_32:\n");
        std::printf("//     int = %u\n", PIO2_LO_Q63_32);
        std::printf("//     hex = 0x%08X\n", PIO2_LO_Q63_32);
        std::printf("//     bin = %s\n", bin_u32(PIO2_LO_Q63_32).c_str());
        std::printf("//     dec = %.36Lf\n", lo_q);

        std::printf("//   hi+lo           = %.36Lf\n", sum_q);
        std::printf("//   (hi+lo)-(pi/2)  = %.36Le\n\n", (sum_q - pio2));
    }

    {
        long double pio2_q = (long double)PIO2_Q38 / S38;
        std::printf("// PIO2_Q38 (for correction)\n");
        std::printf("//   int = %lld\n", (long long)PIO2_Q38);
        std::printf("//   hex = 0x%016llX\n", (unsigned long long)PIO2_Q38);
        std::printf("//   bin = %s\n", bin_i64(PIO2_Q38).c_str());
        std::printf("//   dec = %.36Lf\n", pio2_q);
        std::printf("//   err = %.36Le\n\n", (pio2_q - pio2));
    }

    {
        long double kinv_q = (long double)KINV_Q38 / S38;
        std::printf("// KINV (1/K)\n");
        std::printf("//   real     = %.36Lf\n", kinv);
        std::printf("//   int(Q38) = %lld\n", (long long)KINV_Q38);
        std::printf("//   hex      = 0x%016llX\n", (unsigned long long)KINV_Q38);
        std::printf("//   bin      = %s\n", bin_i64(KINV_Q38).c_str());
        std::printf("//   dec      = %.36Lf\n", kinv_q);
        std::printf("//   err      = %.36Le\n\n", (kinv_q - kinv));
    }

    std::printf("// atan table (Q38)\n");
    for (int i = 0; i < ITERS; ++i) {
        long double a_q = (long double)ATAN_Q38[i] / S38;
        std::printf("// i=%2d atan(2^-i)= %.36Lf ; Q38hex=0x%016llX ; err=%.3Le\n",
                    i, ATAN_REAL[i],
                    (unsigned long long)ATAN_Q38[i],
                    (a_q - ATAN_REAL[i]));
    }
    std::printf("\n");

    std::printf("#pragma once\n");
    std::printf("#include <cstdint>\n\n");
    std::printf("static constexpr int CORDIC_ITERS = %d;\n", ITERS);
    std::printf("static constexpr int ANG_FRAC = %d;\n\n", ANG_FRAC);

    std::printf("static constexpr uint64_t INVPIO2_Q32 = 0x%016llXULL;\n",
                (unsigned long long)INVPIO2_Q32);
    std::printf("static constexpr uint32_t PIO2_HI_Q31 = 0x%08XU;\n", PIO2_HI_Q31);
    std::printf("static constexpr uint32_t PIO2_LO_Q63_32 = 0x%08XU;\n", PIO2_LO_Q63_32);
    std::printf("static constexpr int64_t  PIO2_Q38 = (int64_t)0x%016llXLL;\n",
                (unsigned long long)PIO2_Q38);
    std::printf("static constexpr int64_t  KINV_Q38 = (int64_t)0x%016llXLL;\n\n",
                (unsigned long long)KINV_Q38);

    std::printf("static constexpr int64_t ATAN_Q38[CORDIC_ITERS] = {\n");
    for (int i = 0; i < ITERS; ++i) {
        std::printf("  (int64_t)0x%016llXLL%s\n",
                    (unsigned long long)ATAN_Q38[i],
                    (i + 1 < ITERS) ? "," : "");
    }
    std::printf("};\n");

    return 0;
}
