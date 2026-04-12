// sin_cw_cordic.cpp
// Cody–Waite + CORDIC для sin(x)
//
// Входные данные:
//   x_in: int32 доп. код, fixed-point Q24.7
//   ограничения: |x| < 2^24
//
// Выходные данные:
//   sin(x) как int32 доп. код, fixed-point Q1.30

#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>
#include <limits>
#include <cmath>
#include <iomanip>

using std::int32_t;
using std::int64_t;
using std::uint32_t;
using std::uint64_t;

// Константы

// Итерации CORDIC
static constexpr int CORDIC_ITERS = 38;
// Размерность дробной части для внутренних вычислений
static constexpr int ANG_FRAC = 38;

// 2/pi
static constexpr uint64_t INVPIO2_Q32 = 0x00000000A2F9836EULL;
// pi/2 Q31
static constexpr uint32_t PIO2_HI_Q31 = 0xC90FDAA2U;
// pi/2 Q63
static constexpr uint32_t PIO2_LO_Q63_32 = 0x2168C000U;
// pi/2 Q38
static constexpr int64_t  PIO2_Q38 = (int64_t)0x0000006487ED5111LL;
// Масштабный коэф. для CORDIC, Q38
static constexpr int64_t  KINV_Q38 = (int64_t)0x00000026DD3B6A11LL;

// Таблиц atan, Q38
static constexpr int64_t ATAN_Q38[CORDIC_ITERS] = {
  (int64_t)0x0000003243F6A888LL,
  (int64_t)0x0000001DAC670562LL,
  (int64_t)0x0000000FADBAFC96LL,
  (int64_t)0x00000007F56EA6ABLL,
  (int64_t)0x00000003FEAB76E6LL,
  (int64_t)0x00000001FFD55BBBLL,
  (int64_t)0x00000000FFFAAADELL,
  (int64_t)0x000000007FFF5557LL,
  (int64_t)0x000000003FFFEAABLL,
  (int64_t)0x000000001FFFFD55LL,
  (int64_t)0x000000000FFFFFABLL,
  (int64_t)0x0000000007FFFFF5LL,
  (int64_t)0x0000000003FFFFFFLL,
  (int64_t)0x0000000002000000LL,
  (int64_t)0x0000000001000000LL,
  (int64_t)0x0000000000800000LL,
  (int64_t)0x0000000000400000LL,
  (int64_t)0x0000000000200000LL,
  (int64_t)0x0000000000100000LL,
  (int64_t)0x0000000000080000LL,
  (int64_t)0x0000000000040000LL,
  (int64_t)0x0000000000020000LL,
  (int64_t)0x0000000000010000LL,
  (int64_t)0x0000000000008000LL,
  (int64_t)0x0000000000004000LL,
  (int64_t)0x0000000000002000LL,
  (int64_t)0x0000000000001000LL,
  (int64_t)0x0000000000000800LL,
  (int64_t)0x0000000000000400LL,
  (int64_t)0x0000000000000200LL,
  (int64_t)0x0000000000000100LL,
  (int64_t)0x0000000000000080LL,
  (int64_t)0x0000000000000040LL,
  (int64_t)0x0000000000000020LL,
  (int64_t)0x0000000000000010LL,
  (int64_t)0x0000000000000008LL,
  (int64_t)0x0000000000000004LL,
  (int64_t)0x0000000000000002LL
};

// дробная часть входа
static constexpr int FX_IN_FRAC = 7;
// дробная часть выхода
static constexpr int OUT_FRAC   = 30;

static bool parse_bin32(const std::string& s, int32_t& out) {
    if (s.size() != 32) return false;
    uint32_t u = 0;
    for (char c : s) {
        if (c != '0' && c != '1') return false;
        u = (u << 1) | (uint32_t)(c - '0');
    }
    out = (int32_t)u;
    return true;
}

static std::string to_bin32(int32_t x) {
    uint32_t u = (uint32_t)x;
    std::string s(32, '0');
    for (int i = 31; i >= 0; --i)
        s[31 - i] = ((u >> i) & 1u) ? '1' : '0';
    return s;
}

static long double q_to_ld(int32_t v, int frac) {
    return (long double)v / ldexpl(1.0L, frac);
}

// long double -> int32_t
static int32_t ld_to_q1_30(long double x) {
    // round-to-nearest
    long double scaled = x * ldexpl(1.0L, OUT_FRAC);
    long long q = llroundl(scaled);

    if (q > std::numeric_limits<int32_t>::max()) q = std::numeric_limits<int32_t>::max();
    if (q < std::numeric_limits<int32_t>::min()) q = std::numeric_limits<int32_t>::min();
    return (int32_t)q;
}

static inline uint64_t abs_i32_to_u64(int32_t x) {
    if (x == std::numeric_limits<int32_t>::min()) {
        throw std::runtime_error("Out of range: require |x| < 2^24 (i.e. |raw| < 2^31).");
    }
    return (uint64_t)(x < 0 ? -(int64_t)x : (int64_t)x);
}

struct Reduced {
    int64_t r_q38;    // редуцированный угол в Q38, [0, PIO2)
    int64_t k;        // k = floor(|x| * 2/pi)
    bool sign_neg; // знак x
};

// Cody–Waite редукция до [0, pi/2) в Q38.
static Reduced reduce_cw(int32_t x_in) {
    Reduced out{};
    out.sign_neg = (x_in < 0);

    uint64_t ax = abs_i32_to_u64(x_in);

    // Валидация |x| < 2^24
    if (ax >= (1ull << 31)) {
        throw std::runtime_error("Out of range: require |x| < 2^24 (i.e. |raw| < 2^31).");
    }

    // k = floor(|x| * 2/pi)
    uint64_t prod = ax * INVPIO2_Q32; // вмещается в 63 бита: <2^31 * <2^32 => <2^63
    uint64_t k = (uint64_t)(prod >> (32 + FX_IN_FRAC)); // >>39, 24 бита

    // Перевод |x| в Q38: ax << (38-7)
    int64_t x_q38 = (int64_t)ax << (ANG_FRAC - FX_IN_FRAC); // <<31

    // term1 = k * PIO2_HI_Q31
    // Перевод Q31 -> Q38: <<7
    uint64_t term1_u = (uint64_t)k * (uint64_t)PIO2_HI_Q31; // вмещается в 56 бит (24+32)
    int64_t term1_q38 = (int64_t)(term1_u << (ANG_FRAC - 31)); // <<7, 63 бита

    // term2 = k * PIO2_LO_Q63_32
    // Перевод Q63 -> Q38: >>25
    uint64_t term2_u63 = (uint64_t)k * (uint64_t)PIO2_LO_Q63_32; // вмещается в 56 бит (24+32)
    int64_t term2_q38 = (int64_t)(term2_u63 >> (63 - ANG_FRAC)); // >>25

    int64_t r = x_q38 - term1_q38 - term2_q38;

    // Коррекция r в [0, PIO2)
    if (r < 0) {
        k -= 1;
        r += PIO2_Q38;
    } else if (r >= PIO2_Q38) {
        k += 1;
        r -= PIO2_Q38;
    }

    out.r_q38 = r;
    out.k = k;
    return out;
}

struct CordicOut { int64_t c_q38, s_q38; };

// CORDIC режим вращения для угла в Q38
static CordicOut cordic(int64_t z_q38) {
    int64_t x = KINV_Q38;
    int64_t y = 0;
    int64_t z = z_q38;

    for (int i = 0; i < CORDIC_ITERS; ++i) {
        int64_t x_sh = x >> i;
        int64_t y_sh = y >> i;
        int64_t a = ATAN_Q38[i];

        if (z >= 0) {
            // d = +1
            int64_t xn = x - y_sh;
            int64_t yn = y + x_sh;
            int64_t zn = z - a;
            x = xn; y = yn; z = zn;
        } else {
            // d = -1
            int64_t xn = x + y_sh;
            int64_t yn = y - x_sh;
            int64_t zn = z + a;
            x = xn; y = yn; z = zn;
        }
    }
    return {x, y};
}

static int32_t sin_fixed(int32_t x_in) {
    Reduced red = reduce_cw(x_in);

    CordicOut cs = cordic(red.r_q38);

    // выделеник квадранта
    int q = (int)(red.k & 3);

    int64_t s_q38;
    switch (q) {
        case 0: s_q38 = cs.s_q38;  break; // sin(r)
        case 1: s_q38 = cs.c_q38;  break; // sin(pi/2 + r) = cos(r)
        case 2: s_q38 = -cs.s_q38; break; // sin(pi + r) = -sin(r)
        case 3: s_q38 = -cs.c_q38; break; // sin(3pi/2 + r) = -cos(r)
        default: s_q38 = 0; break;
    }

    // Восстановление знака: sin(-x) = -sin(x)
    if (red.sign_neg)
        s_q38 = -s_q38;

    // Перевод Q38 -> Q30
    int64_t s_q30 = s_q38 >> (ANG_FRAC - OUT_FRAC); // trunc

    // Насыщение (на всякий случай)
    if (s_q30 > std::numeric_limits<int32_t>::max()) s_q30 = std::numeric_limits<int32_t>::max();
    if (s_q30 < std::numeric_limits<int32_t>::min()) s_q30 = std::numeric_limits<int32_t>::min();

    return (int32_t)s_q30;
}


int main() {
    std::cout << "Введите x как 32-битное двоичное число в доп. коде, Q24.7.\n";
    std::cout << "Ограничения: |x| < 2^24 (real).\n";
    std::cout << "Вывод: sin(x) in Q1.30.\n";
    std::cout << "Введите 'q' чтобы выйти.\n\n";

    for (;;) {
        std::cout << "x[32b]> ";
        std::string s;
        if (!(std::cin >> s)) return 0;
        if (s == "q" || s == "Q" || s == "quit") return 0;

        int32_t x_in;
        if (!parse_bin32(s, x_in)) {
            std::cout << "Неправильный ввод: нужно точно 32 бита.\n\n";
            continue;
        }

        try {
            int32_t y = sin_fixed(x_in);

            // decimal
            long double x_ld = q_to_ld(x_in, FX_IN_FRAC);
            long double y_ld = q_to_ld(y, OUT_FRAC);

            // reference
            long double ref_ld = sinl(x_ld);
            int32_t ref_q30 = ld_to_q1_30(ref_ld);

            // diff in Q1.30 LSB
            int64_t diff = (int64_t)y - (int64_t)ref_q30;

            std::cout << std::setprecision(18);
            std::cout << "x (Q24.7)         = " << (double)x_ld << "\n";

            std::cout << "sin_cordic (Q1.30) = " << (double)y_ld << "\n";
            std::cout << "  bin             = " << to_bin32(y) << "\n";

            std::cout << "sin_ref (Q1.30)    = " << (double)q_to_ld(ref_q30, OUT_FRAC) << "\n";
            std::cout << "  bin             = " << to_bin32(ref_q30) << "\n";

            std::cout << "diff (cordic-ref)  = " << (long long)diff << " LSB (Q1.30)\n\n";
        } catch (const std::exception& e) {
            std::cout << "Ошибка: " << e.what() << "\n\n";
        }
    }
}
