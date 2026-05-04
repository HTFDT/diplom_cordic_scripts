#include "converter.h"
#include "const_generator.h"
#include "json.h"
#include "cli_common.h"
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

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

// Насышение 
static inline int64_t saturate(int64_t val, int bits) {
    int64_t max_val = (1LL << (bits - 1)) - 1;
    int64_t min_val = -(1LL << (bits - 1));
    return std::max(min_val, std::min(max_val, val));
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


static std::string results_to_json(const std::vector<CordicResult>& results, bool include_iterations, int bits)
{
    ordered_json output = ordered_json::array();

    for (const auto& res : results) {
        ordered_json item = {
            {"arg",              to_bin_string(res.arg_fixed, bits)},
            {"arg_deg",          res.arg_deg},
            {"arg_rad",          res.arg_rad},
            {"result",           to_bin_string(res.result_fixed, bits)},
            {"result_double",    res.result_double},
            {"reference",        to_bin_string(res.reference_fixed, bits)},
            {"reference_diff",   to_bin_string(res.diff_fixed, bits)}
        };

        if (include_iterations){
            ordered_json iter_obj = ordered_json::object();
            for (int i = 0; i < res.iterations.size(); i++) {
                iter_obj[std::to_string(i)] = {
                    {"cos", to_bin_string(res.iterations[i].cos_val, bits)},
                    {"sin", to_bin_string(res.iterations[i].sin_val, bits)},
                    {"z",   to_bin_string(res.iterations[i].z_val, bits)}
                };
            }
            item["cordic_iterations"] = iter_obj;
        }

        output.push_back(item);
    }

    return output.dump(2) + "\n";
}

enum class Verbosity {
    BRIEF,
    VERBOSE
};

enum class InputType {
    DEGREES,
    RADIANS,
    BINARY
};

struct SinReplState : ReplState {
    Verbosity verbosity = Verbosity::VERBOSE;
};

struct SinArgs : CommonArgs {
    InputType input_type = InputType::DEGREES;
    std::string in_file;
    int iterations = 32;
    std::vector<std::string> values;
};

struct SinShell : AppShell<SinArgs, SinReplState> {
public:
    SinShell() : AppShell(
        "cordic_sin.exe", 
        "Вычисляет sin(x) методом CORDIC в целочисленной арифметике\n"
        "с фиксированной точкой.\n"
    ) {
        // --rad
        auto *rad = app_.add_flag(
            "--rad",
            [this](int64_t) { args_.input_type = InputType::RADIANS; },
            "Вход в радианах"
        );

        // --deg
        auto *deg = app_.add_flag(
            "--deg",
            [this](int64_t) { args_.input_type = InputType::DEGREES; },
            "Вход в градусах (по умолчанию)"
        );

        // --bin
        auto *bin = app_.add_flag(
            "--bin",
            [this](int64_t) { args_.input_type = InputType::BINARY; },
            "Вход в виде 32-битных двоичных слов (BAM: 2 бита квадранта + 30 бита угла)"
        );

        // взаимоисключение
        rad->excludes(deg)->excludes(bin);
        deg->excludes(rad)->excludes(bin);
        bin->excludes(rad)->excludes(deg);

        app_.add_option("-i,--in", args_.in_file,
            "Читать значения из файла")
            ->check(CLI::ExistingFile);

        app_.add_option("-n,--iterations", args_.iterations,
            "Кол-во итераций (по умолчанию: 32)")
            ->check(CLI::Range(4, 62));

        app_.add_option("values", args_.values,
            "Входные значения")
            ->expected(-1);
    }

protected:
    virtual bool is_batch() {
        return !args_.values.empty();
    }

    virtual void load_args(int argc, char* argv[]) {
        AppShell<SinArgs, SinReplState>::load_args(argc, argv);

        // Чтение значений из файла, если указан
        if (!args_.in_file.empty()) {
            auto file_vals = read_values_from_file(args_.in_file);
            args_.values.insert(
                args_.values.end(),
                file_vals.begin(), file_vals.end());
        }
    }

    virtual void load_repl_input(const std::string& line) {
        // Разбиваем строку на токены
        std::vector<std::string> values;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) {
            values.push_back(token);
        }

        if (values.empty())
            return;

        args_.values = values;
    }

    virtual std::string compute() {
        // ---- Генерация констант CORDIC ----
        auto atan_table = generate_atan_table(args_.bits, args_.iterations);
        int64_t k_inv   = generate_k_inv(args_.bits, args_.iterations);

        // ---- Вычисление ----
        std::vector<CordicResult> results;
        results.reserve(args_.values.size());

        for (size_t i = 0; i < args_.values.size(); i++) {
            int64_t fixed;
            double angle_deg = 0;
            double angle_rad = 0;

            if (args_.input_type == InputType::DEGREES || args_.input_type == InputType::RADIANS) {
                double angle;
                try {
                    angle = parse_number(args_.values[i]);
                } catch (const std::exception& e) {
                    throw std::invalid_argument("Ошибка разбора '" + args_.values[i] + "': " + e.what());
                }

                angle_deg = args_.input_type == InputType::RADIANS ? rad_to_deg(angle) : angle;
                angle_rad = args_.input_type == InputType::RADIANS ? angle : deg_to_rad(angle);

                fixed = angle_rad_to_fixed(angle_rad, args_.bits);
            }
            else {
                fixed = parse_fixed_bin(args_.values[i], args_.bits);
                angle_rad = angle_fixed_to_rad(fixed, args_.bits);
                angle_deg = rad_to_deg(angle_rad);
            }

            results.push_back(compute_sin(fixed, angle_deg, angle_rad, args_.bits, atan_table, k_inv));
        }

        // ---- Вывод JSON ----
        std::string json = results_to_json(results, state_.verbosity == Verbosity::VERBOSE, args_.bits);

        return json;
    }

    virtual std::string format_text(const std::string& json_result) {
        json j = json::parse(json_result);

        if (!j.is_array()) {
            throw std::invalid_argument("Получена неверная json-строка, ожидался массив: " + json_result);
        }

        // TODO

        return json_result;
    }

    virtual void print_repl_status() {
        std::cout << "  Режим: " << (args_.input_type == InputType::RADIANS ? "радианы" 
            : args_.input_type == InputType::DEGREES ? "градусы" : "двоичные числа") << "\n"
                  << "  Разрядность: " << args_.bits << "\n"
                  << "  Кол-во итераций: " << args_.iterations << "\n"
                  << "  Формат: " << (state_.format == OutputFormat::JSON ? "json" : "text") << "\n"
                  << "  Детализация: " << (state_.verbosity == Verbosity::BRIEF ? "краткая" : "подробная") << "\n"
                  << "  Вывод: " << (args_.out_file.empty() ? "консоль" : args_.out_file) << "\n"
                  << "\n";
    }

    virtual bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        bool handled = AppShell<SinArgs, SinReplState>::handle_repl_command(cmd, arg);

        if (handled)
            return true;
        
        if (cmd == "deg") {
            args_.input_type = InputType::DEGREES;
            std::cout << "  Режим: градусы\n";
        } else if (cmd == "rad") {
            args_.input_type = InputType::RADIANS;
            std::cout << "  Режим: радианы\n";
        } else if (cmd == "bin") {
            args_.input_type = InputType::BINARY;
            std::cout << "  Режим: двоичные числа\n";
        } else if (cmd == "iter") {
            if (arg.empty()) {
                std::cout << "  Кол-во итераций: "<< args_.iterations << "\n";
            } else {
                try {
                    int iter = std::stoi(arg);
                    if (iter < 4 || iter > 62) {
                        std::cout << "  Ошибка: кол-во итераций должно быть в диапазоне [4, 62]\n";
                    } else {
                        args_.iterations = iter;
                        std::cout << "  Кол-во итераций: "
                                  << args_.iterations << "\n";
                    }
                } catch (...) {
                    std::cout << "  Ошибка: некорректное значение: " << arg << "\n";
                }
            }
        } else if (cmd == "verbose") {
            state_.verbosity = Verbosity::VERBOSE;
            std::cout << "  Детализация: подробная\n";
        } else if (cmd == "brief") {
            state_.verbosity = Verbosity::BRIEF;
            std::cout << "  Детализация: краткая\n";
        }
        else {
            return false;
        }

        return true;
    }

    virtual void print_repl_help() {
        std::cout
            << "Команды:\n"
            << "  :deg            Переключить на градусы\n"
            << "  :rad            Переключить на радианы\n"
            << "  :bin            Переключить на двоичные числа\n"
            << "  :bits N         Установить разрядность\n"
            << "  :iter N         Установить кол-во итераций\n"
            << "  :json           Вывод в формате JSON\n"
            << "  :text           Вывод в текстовом формате\n"
            << "  :verbose        Подробный вывод (с итерациями)\n"
            << "  :brief          Краткий вывод\n"
            << "  :out ФАЙЛ       Записывать результат в файл\n"
            << "  :out -          Вывод на консоль\n"
            << "  :status         Показать текущие настройки\n"
            << "  :help           Показать эту справку\n"
            << "  quit            Выход\n"
            << "\n"
            << "Ввод значений:\n"
            << "  Одно или несколько чисел через пробел.\n"
            << "  Десятичный разделитель: точка или запятая.\n"
            << "  Пример: 45 90.5 -135,7\n"
            << "\n";
    }
};


int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    SinShell shell;

    return shell.run(argc, argv);
}
