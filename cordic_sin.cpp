#include "converter.h"
#include "const_generator.h"
#include "json.h"
using json = nlohmann::json;

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cmath>

// ============================================================
//  Целочисленная арифметика для моделирования Минитеры
//  Все вычисления CORDIC — только сдвиги и сложение/вычитание
// ============================================================

// Арифметический сдвиг вправо
static inline int64_t asr(int64_t val, int shift) {
    return val >> shift;
}

// Усечение значения до bits бит с расширением знака.
// Моделирует поведение bits-разрядной арифметики в доп. коде.
static inline int64_t truncate(int64_t val, int bits) {
    // Маска младших bits бит
    uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
    val &= (int64_t)mask;
    // Расширение знака
    int64_t sign_bit = 1LL << (bits - 1);
    if (val & sign_bit) {
        val |= ~(int64_t)mask;
    }
    return val;
}

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
//  Ядро CORDIC — только целочисленные операции
// ============================================================

static CordicResult compute_sin(
    int64_t input_fixed,               // вход в Q1.(bits-1), масштаб 2π
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

    int frac_bits = bits - 1;
    int num_iterations = (int)atan_table.size();

    // ----------------------------------------------------------
    // Шаг 1: Извлечение знака и взятие модуля
    // ----------------------------------------------------------
    int64_t input_sign;  // +1 или -1
    int64_t abs_arg;

    if (input_fixed < 0) {
        input_sign = -1;
        abs_arg = -input_fixed;
    } else {
        input_sign = 1;
        abs_arg = input_fixed;
    }

    // Особый случай: минимальное значение -2^{bits-1}
    // Соответствует -2π ≡ 0, sin = 0
    int64_t overflow_val = 1LL << frac_bits;
    if (abs_arg >= overflow_val) {
        abs_arg = 0;
    }

    // ----------------------------------------------------------
    // Шаг 2: Определение квадранта (два старших бита модуля)
    //         и извлечение угла внутри квадранта
    // ----------------------------------------------------------
    // abs_arg: frac_bits бит, старшие 2 — квадрант,
    // младшие (frac_bits-2) — угол в пределах квадранта
    int quadrant_bits = frac_bits - 2;   // 29 для bits=32

    int64_t quadrant = (abs_arg >> quadrant_bits) & 0x3;
    int64_t Q0 = quadrant & 1;           // бит выбора sin/cos
    int64_t Q1 = (quadrant >> 1) & 1;    // бит инверсии знака

    // Угол внутри квадранта [0, pi/2)
    int64_t theta = abs_arg & ((1LL << quadrant_bits) - 1);

    // ----------------------------------------------------------
    // Шаг 3: Масштабирование угла для CORDIC
    //   theta сейчас: (frac_bits-2) бит, представляет [0, pi/2)
    //   Нужно: frac_bits бит (Q1.(bits-1)), где 2^(bits-1) = pi/2
    //   Сдвиг влево на 2 бита
    // ----------------------------------------------------------
    int64_t z0 = theta << 2;

    // ----------------------------------------------------------
    // Шаг 4: Итерации CORDIC (режим вращения)
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

        x = truncate(x_new, bits);
        y = truncate(y_new, bits);
        z = truncate(z_new, bits);

        res.iterations.push_back({x, y, z});
    }

    // ----------------------------------------------------------
    // Шаг 5: Выбор sin/cos по квадранту и применение знака
    //   Q0 = 0 -> берём y (sin),  Q0 = 1 → берём x (cos)
    //   Q1 = 1 -> инвертируем знак
    //   input_sign -> учёт исходного знака аргумента
    // ----------------------------------------------------------
    int64_t selected = Q0 ? x : y;

    if (Q1) selected = -selected;
    if (input_sign < 0) selected = -selected;

    res.result_fixed  = truncate(selected, bits);
    res.result_double = fixed_result_to_double(res.result_fixed, bits);

    // Эталонное значение
    res.reference_fixed = (int64_t)llround(std::sin(input_rad) * (double)(1LL << (bits - 1)));
    res.reference_double = std::sin(input_rad);
    res.diff_fixed = res.result_fixed - res.reference_fixed;

    return res;
}

// ============================================================
//  Форматирование JSON
// ============================================================

static std::string escape_json_string(const std::string& s) {
    // Для числовых значений экранирование не требуется,
    // но на всякий случай
    return s;
}


static std::string results_to_json(
    const std::vector<CordicResult>& results, int bits)
{
    json output = json::array();

    for (const auto& res : results) {
        json iter_array = json::array();
        for (const auto& it : res.iterations) {
            iter_array.push_back({
                {"cos", fixed_to_json(it.cos_val, bits)},
                {"sin", fixed_to_json(it.sin_val, bits)},
                {"z",   fixed_to_json(it.z_val, bits)}
            });
        }

        output.push_back({
            {"arg",              fixed_to_json(res.arg_fixed, bits)},
            {"arg_deg",          res.arg_deg},
            {"arg_rad",          res.arg_rad},
            {"result",           fixed_to_json(res.result_fixed, bits)},
            {"result_double",    res.result_double},
            {"reference",        fixed_to_json(res.reference_fixed, bits)},
            {"reference_diff",   fixed_to_json(res.diff_fixed, bits)},
            {"cordic_iterations", iter_array}
        });
    }

    return output.dump(2) + "\n";
}



// ============================================================
//  CLI
// ============================================================

static void print_usage() {
    std::cerr
        << "Использование: cordic_sin [ПАРАМЕТРЫ] [значение1 значение2 ...]\n"
        << "\n"
        << "Вычисляет sin(x) методом CORDIC в целочисленной арифметике\n"
        << "с фиксированной точкой.\n"
        << "\n"
        << "Параметры:\n"
        << "  --deg             Вход в градусах (по умолчанию)\n"
        << "  --rad             Вход в радианах\n"
        << "  --bits N          Разрядность (по умолчанию: 32)\n"
        << "  -i, --in ФАЙЛ    Читать значения из файла\n"
        << "  -o, --out ФАЙЛ   Записать результат JSON в файл\n"
        << "  -h, --help        Показать справку\n"
        << "\n"
        << "Формат вывода (JSON):\n"
        << "  Список объектов, по одному на каждое входное значение.\n"
        << "  Каждый объект содержит:\n"
        << "    arg               - аргумент в Q1.(bits-1), формат {dec, bin}\n"
        << "    arg_deg           - аргумент в градусах (double)\n"
        << "    arg_rad           - аргумент в радианах (double)\n"
        << "    result            - результат sin в Q1.(bits-1), формат {dec, bin}\n"
        << "    result_double     - результат sin (double)\n"
        << "    reference         - эталон std::sin() в Q1.(bits-1), формат {dec, bin}\n"
        << "    reference_diff    - разница result-reference в Q1.(bits-1), формат {dec, bin}\n"
        << "    cordic_iterations - список итераций CORDIC, каждая содержит:\n"
        << "        cos  - значение x (cos) на итерации, формат {dec, bin}\n"
        << "        sin  - значение y (sin) на итерации, формат {dec, bin}\n"
        << "        z    - остаток угла на итерации, формат {dec, bin}\n"
        << "\n"
        << "Пример вывода:\n"
        << "  [\n"
        << "    {\n"
        << "      \"arg\": {\"dec\": 536870912, \"bin\": \"00100000...\"},\n"
        << "      \"arg_deg\": 90.0,\n"
        << "      \"arg_rad\": 1.5707963267949,\n"
        << "      \"result\": {\"dec\": 2147483647, \"bin\": \"01111111...\"},\n"
        << "      \"result_double\": 0.99999999953,\n"
        << "      \"reference\": {\"dec\": 2147483647, \"bin\": \"01111111...\"},\n"
        << "      \"reference_diff\": {\"dec\": 0, \"bin\": \"00000000...\"},\n"
        << "      \"cordic_iterations\": [\n"
        << "        {\"cos\": {...}, \"sin\": {...}, \"z\": {...}},\n"
        << "        ...\n"
        << "      ]\n"
        << "    }\n"
        << "  ]\n";
}


int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    bool use_radians = false;
    int bits = 32;
    std::string in_file;
    std::string out_file;
    std::vector<std::string> raw_values;

    // ---- Разбор аргументов ----
    for (int a = 1; a < argc; a++) {
        std::string arg = argv[a];
        if (arg == "--deg") {
            use_radians = false;
        } else if (arg == "--rad") {
            use_radians = true;
        } else if (arg == "--bits") {
            if (++a >= argc) {
                std::cerr << "Ошибка: --bits требует значение\n";
                return 1;
            }
            bits = std::stoi(argv[a]);
            if (bits < 4 || bits > 62) {
                std::cerr << "Ошибка: bits должен быть в диапазоне [4, 62]\n";
                return 1;
            }
        } else if (arg == "-i" || arg == "--in") {
            if (++a >= argc) {
                std::cerr << "Ошибка: --in требует имя файла\n";
                return 1;
            }
            in_file = argv[a];
        } else if (arg == "-o" || arg == "--out") {
            if (++a >= argc) {
                std::cerr << "Ошибка: --out требует имя файла\n";
                return 1;
            }
            out_file = argv[a];
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else {
            raw_values.push_back(arg);
        }
    }

    // Чтение из файла
    if (!in_file.empty()) {
        auto file_vals = read_values_from_file(in_file);
        raw_values.insert(raw_values.end(),
                          file_vals.begin(), file_vals.end());
    }

    if (raw_values.empty()) {
        std::cerr << "Ошибка: не указаны входные значения\n";
        print_usage();
        return 1;
    }

    // ---- Генерация констант CORDIC ----
    int num_iterations = bits - 1;
    auto atan_table = generate_atan_table(bits, num_iterations);
    int64_t k_inv   = generate_k_inv(bits, num_iterations);

    // ---- Вычисление ----
    std::vector<CordicResult> results;
    results.reserve(raw_values.size());

    for (const auto& raw : raw_values) {
        double angle;
        try {
            angle = parse_number(raw);
        } catch (const std::exception& e) {
            std::cerr << "Ошибка разбора '" << raw
                      << "': " << e.what() << "\n";
            return 1;
        }

        double angle_deg = use_radians ? rad_to_deg(angle) : angle;
        double angle_rad = use_radians ? angle : deg_to_rad(angle);

        int64_t fixed = angle_rad_to_fixed(angle_rad, bits);

        results.push_back(
            compute_sin(fixed, angle_deg, angle_rad,
                        bits, atan_table, k_inv));
    }

    // ---- Вывод JSON ----
    std::string json = results_to_json(results, bits);

    if (!out_file.empty()) {
        write_to_file(out_file, json);
    } else {
        std::cout << json;
    }

    return 0;
}
