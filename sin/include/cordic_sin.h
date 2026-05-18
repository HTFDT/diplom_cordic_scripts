#include "int_utils.h"

// ============================================================
//  Структуры для хранения результатов
// ============================================================

struct CordicIteration {
    int64_t cos_val;   // x_i
    int64_t sin_val;   // y_i
    int64_t z_val;     // z_i (остаток угла)
};

struct CordicResult {
    int64_t arg_fixed;         // аргумент в Q1.(bits-1)
    double  arg_deg;           // аргумент в градусах
    double  arg_rad;           // аргумент в радианах
    int64_t result_fixed;      // результат sin в Q1.(bits-1)
    double  result_double;     // результат sin как double
    int64_t reference_fixed;   // эталон sin в Q1.(bits-1)
    double  reference_double;  // эталон sin как double
    int64_t diff_fixed;        // разница result - reference
    std::vector<CordicIteration> iterations;
};

// ============================================================
//  Ядро CORDIC
// ============================================================

static CordicResult compute_sin(
    int64_t input_fixed,               // вход беззнаковый в Q0.(bits)
    double  input_deg,
    double  input_rad,
    int     bits,
    const std::vector<int64_t>& atan_table,
    int64_t k_inv)
{
    CordicResult res;
    res.arg_fixed = input_fixed;
    res.arg_deg   = input_deg;
    res.arg_rad   = input_rad;

    int num_iterations = (int)atan_table.size();

    // ----------------------------------------------------------
    // Шаг 1: Определение квадранта (два старших бита полного угла)
    //        и извлечение угла внутри квадранта
    // ----------------------------------------------------------
    // angle_bits бит, старшие 2 — квадрант,
    // младшие (bits-2) — угол в пределах квадранта
    int quadrant_bits = bits - 2;   // 30 для angle_bits=32

    int64_t quadrant = (input_fixed >> quadrant_bits) & 0b11;
    int64_t Q0 = quadrant & 1;           // бит выбора sin/cos
    int64_t Q1 = (quadrant >> 1) & 1;    // бит инверсии знака

    // Угол внутри квадранта [0, pi/2)
    int64_t theta = input_fixed & ((1LL << quadrant_bits) - 1);

    // ----------------------------------------------------------
    // Шаг 2: Масштабирование угла для CORDIC
    //   theta сейчас: (bits-2) бит, представляет [0, pi/2)
    //   Нужно: bits бит (Q1.(bits-1))
    //   Сдвиг влево на 1 бит, остается знаковый 0 и 31 бит дробной части
    // ----------------------------------------------------------
    int64_t z0 = theta << 1;

    // ----------------------------------------------------------
    // Шаг 3: Итерации CORDIC (режим вращения)
    //   x0 = K_inv,  y0 = 0,  z0 = theta_scaled
    //   На каждой итерации:
    //     d = (z >= 0) ? +1 : -1
    //     x' = x - d * (y >> i)
    //     y' = y + d * (x >> i)
    //     z' = z - d * atan_table[i]
    // ----------------------------------------------------------
    int64_t x = k_inv;
    int64_t y = 0;
    int64_t z = z0;

    // Сохраняем начальное состояние как «итерацию 0 на входе»
    res.iterations.reserve(num_iterations + 1);
    res.iterations.push_back({x, y, z});

    for (int i = 0; i < num_iterations; i++) {
        int64_t x_shifted = asr(x, i);
        int64_t y_shifted = asr(y, i);

        int64_t x_new, y_new, z_new;

        if (z >= 0) {
            // d = +1: вращение против часовой стрелки
            x_new = x - y_shifted;
            y_new = y + x_shifted;
            z_new = z - atan_table[i];
        } else {
            // d = -1: вращение по часовой стрелке
            x_new = x + y_shifted;
            y_new = y - x_shifted;
            z_new = z + atan_table[i];
        }

        // Обрезаем до bits
        x = truncate(x_new, bits);
        y = truncate(y_new, bits);
        z = truncate(z_new, bits);

        res.iterations.push_back({x, y, z});
    }

    // ----------------------------------------------------------
    // Шаг 4: Выбор sin/cos по квадранту и применение знака
    //   Q0 = 0 -> берём y (sin),  Q0 = 1 → берём x (cos)
    //   Q1 = 1 -> инвертируем знак
    // ----------------------------------------------------------
    int64_t selected = Q0 ? x : y;

    if (Q1) selected = -selected;

    res.result_fixed  = truncate(selected, bits);
    res.result_double = fixed_result_to_double(res.result_fixed, bits);

    // Эталонное значение
    double ref_sin = std::sin(input_rad);
    res.reference_fixed = saturate((int64_t)llround(ref_sin * (double)(1LL << (bits - 1))), bits);
    res.reference_double = fixed_result_to_double(res.reference_fixed, bits);
    res.diff_fixed = res.result_fixed - res.reference_fixed;

    return res;
}