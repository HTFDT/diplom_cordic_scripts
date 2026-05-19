#include "cli_common.h"
#include "json.h"
#include "common_utils.h"
#include "converter_ln.h"
#include "const_generator_ln.h"
#include "cordic_ln.h"

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <iostream>
#include <vector>
#include <string>
#include <sstream>

enum class Verbosity { BRIEF, VERBOSE };
enum class InputType { REAL, BINARY };

struct LnState : ReplState {
    Verbosity verbosity = Verbosity::VERBOSE;
};

struct LnArgs : CommonArgs {
    InputType input_type = InputType::REAL;
    std::string in_file;
    int iterations = 32;
    std::vector<std::string> values;
};

static std::string results_to_json(const std::vector<LnResult>& results, bool include_iterations, int bits) {
    ordered_json out = ordered_json::array();

    for (auto &r : results) {
        ordered_json item = {
            {"arg", to_bin_string(r.arg_fixed, bits)},
            {"arg_double", r.arg_double},
            {"result", to_bin_string(r.result_fixed, bits)},
            {"result_double", r.result_double},
            {"reference", to_bin_string(r.reference_fixed, bits)},
            {"reference_diff", to_bin_string(r.diff_fixed, bits)}
        };

        if (include_iterations) {
            ordered_json it = ordered_json::object();
            for (size_t k = 0; k < r.iterations.size(); k++) {
                const auto &st = r.iterations[k];
                it[std::to_string(k)] = {
                    {"shift", st.i},
                    {"x", to_bin_string(st.x, bits)},
                    {"y", to_bin_string(st.y, bits)},
                    {"z", to_bin_string(st.z, bits)}
                };
            }
            item["cordic_iterations"] = it;
        }

        out.push_back(item);
    }

    return out.dump(2) + "\n";
}

struct LnShell : AppShell<LnArgs, LnState> {
public:
    LnShell() : AppShell(
        "cordic_ln.exe",
        "Вычисляет ln(1+x) методом hyperbolic CORDIC (vectoring)\n"
    ) {
        auto *real = app_.add_flag(
            "--real",
            [this](int64_t){ args_.input_type = InputType::REAL; },
            "Вход: вещественные числа a в [0.5,1) (по умолчанию)"
        );

        auto *bin = app_.add_flag(
            "--bin",
            [this](int64_t){ args_.input_type = InputType::BINARY; },
            "Вход: двоичные слова (signed fixed)"
        );

        real->excludes(bin);
        bin->excludes(real);

        app_.add_option("-i,--in", args_.in_file, "Читать значения из файла")
            ->check(CLI::ExistingFile);

        app_.add_option("-n,--iterations", args_.iterations,
            "Кол-во итераций CORDIC, не включая повторы (по умолчанию 32)")->check(CLI::Range(2, 62));

        app_.add_option("values", args_.values, "Входные значения")->expected(-1);
    }

protected:
    bool is_batch() { 
        return !args_.values.empty(); 
    }

    void load_args(int argc, char* argv[]) {
        AppShell<LnArgs, LnState>::load_args(argc, argv);
        if (!args_.in_file.empty()) {
            auto v = read_values_from_file(args_.in_file);
            args_.values.insert(args_.values.end(), v.begin(), v.end());
        }
    }

    void load_repl_input(const std::string& line) {
        std::vector<std::string> v;
        std::istringstream iss(line);
        std::string t;
        while (iss >> t) v.push_back(t);
        args_.values = v;
    }

    std::string compute() {
        auto schedule = generate_hyperbolic_schedule(args_.iterations);
        auto atanh_table = generate_atanh_table(args_.bits, schedule);

        // int64_t invKh = generate_inv_Kh(args_.bits, schedule);

        std::vector<LnResult> results;
        results.reserve(args_.values.size());

        for (auto &s : args_.values) {
            int64_t a_fixed = 0;
            double a_double = 0;

            if (args_.input_type == InputType::REAL) {
                a_double = parse_number(s);
                a_fixed = ln_real_to_fixed(a_double, args_.bits);
                // regenerate a_double from fixed to ensure exact match to what goes into core
                a_double = ln_fixed_to_real(a_fixed, args_.bits);
            } else {
                a_fixed = parse_fixed_bin(s, args_.bits);
                a_fixed = truncate(a_fixed, args_.bits);
                a_double = ln_fixed_to_real(a_fixed, args_.bits);
                if (a_fixed < 0)
                    throw std::invalid_argument("аргумент должен принадлежать диапазону [0, 1). Получено: " + std::to_string(a_double));
            }

            results.push_back(compute_ln(
                a_fixed, a_double, args_.bits,
                schedule, atanh_table
            ));
        }

        return results_to_json(results, state_.verbosity == Verbosity::VERBOSE, args_.bits);
    }

    std::string format_text(const std::string& json_result) {
        return json_result;
    }

    void print_repl_status() {
        std::cout
            << "  Режим: " << (args_.input_type == InputType::REAL ? "real" : "bin") << "\n"
            << "  Разрядность: " << args_.bits << "\n"
            << "  Кол-во итераций (без повторов): " << args_.iterations << "\n"
            << "  Вывод: " << (args_.out_file.empty() ? "console" : args_.out_file) << "\n\n";
    }

    bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        bool handled = AppShell<LnArgs, LnState>::handle_repl_command(cmd, arg);
        if (handled) 
            return true;

        if (cmd == "real") {
            args_.input_type = InputType::REAL;
            std::cout << "  Режим: real\n";
        } else if (cmd == "bin") {
            args_.input_type = InputType::BINARY;
            std::cout << "  Режим: bin\n";
        } else if (cmd == "iter") {
            if (arg.empty()) {
                std::cout << "  Кол-во итераций: " << args_.iterations << "\n";
            } else {
                int iter = std::stoi(arg);
                if (iter < 4 || iter > 62)
                    std::cout << "  Ошибка: кол-во итераций должно быть в диапазоне [4..62]\n";
                else { 
                    args_.iterations = iter;
                    std::cout << "  Кол-во итераций: " << iter << "\n"; 
                }
            }
        } else if (cmd == "verbose") {
            state_.verbosity = Verbosity::VERBOSE;
            std::cout << "  Детализация: подробная\n";
        } else if (cmd == "brief") {
            state_.verbosity = Verbosity::BRIEF;
            std::cout << "  Детализация: краткая\n";
        } else {
            return false;
        }

        return true;
    }

    void print_repl_help() {
        std::cout
            << "Команды:\n"
            << "  :real           вход real a в [0.5,1)\n"
            << "  :bin            вход двоичные слова\n"
            << "  :iter N         кол-во итераций (без повторов)\n"
            << "  :bits N         разрядность\n"
            << "  :verbose        Подробный вывод (с итерациями)\n"
            << "  :brief          Краткий вывод\n"
            << "  :out FILE / :out -\n"
            << "  quit\n\n";
    }
};

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    LnShell app;
    return app.run(argc, argv);
}
