#include "common_utils.h"
#include "const_generator_sin.h"
#include "converter_sin.h"
#include "json.h"
#include "cli_common.h"
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <cmath>

struct ConstGeneratorArgs : CommonArgs {
    int iterations = 32;
};

struct ConstGeneratorShell : AppShell<ConstGeneratorArgs> {
public:
    ConstGeneratorShell() : AppShell(
        "const_generator_sin.exe",
        "Генерирует константы CORDIC для вычисления sin: K_inv (коэффициент масштабирования) и таблицу arctan(2^{-i}) со значениями в диапазоне [0, pi/2).\n"
        "Вывод в формате JSON.\n"
        "Формат вывода:\n"
        "Объект с полями:\n"
        "coef        - K_inv в формате Q1.(bits-1) (со знаком)\n"
        "coef_double - K_inv в десятичном представлении (double)\n"
        "iterations  - список arctan(2^{-i}), каждый элемент в формате Q1.(bits-1) (со знаком)\n"
    ) { 
        app_.add_option("-n,--iterations", args_.iterations,
            "Кол-во итераций (по умолчанию: 32)")
            ->check(CLI::Range(4, 62));
    }

protected:
    virtual bool is_batch() {
        return true;
    }

    virtual std::string compute() {
         auto atan_table = generate_atan_table(args_.bits, args_.iterations);
        int64_t k_inv = generate_k_inv(args_.bits, args_.iterations);

        // K_inv как double
        double k_inv_double = (double)k_inv / (double)(1LL << (args_.bits - 1));

        // JSON
        ordered_json iter_obj = ordered_json::object();
        for (int i = 0; i < args_.iterations; ++i) {
            iter_obj[std::to_string(i)] = to_bin_string(atan_table[i], args_.bits);
        }

        ordered_json output = {
            {"coef", to_bin_string(k_inv, args_.bits)},
            {"coef_double", k_inv_double},
            {"iterations", iter_obj}
        };

        std::string json_str = output.dump(2);
        
        return json_str;
    }

    virtual void load_repl_input(const std::string& line) {
        throw std::logic_error("Not implemented");
    }
    
    virtual std::string format_text(const std::string& json_result) {
        throw std::logic_error("Not implemented");
    }

    virtual void print_repl_status() {
        throw std::logic_error("Not implemented");
    }

    virtual bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        throw std::logic_error("Not implemented");
    }

    virtual void print_repl_help() {
        throw std::logic_error("Not implemented");
    }
};


int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    ConstGeneratorShell shell;

    return shell.run(argc, argv);
}
