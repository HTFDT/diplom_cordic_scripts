#include "cli_common.h"
#include "json.h"
#include "common_utils.h"
#include "const_generator_ln.h"

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <iostream>

struct ConstLnState : ReplState { };

struct ConstLnArgs : CommonArgs {
    int iterations = 32;
};

struct ConstLnShell : AppShell<ConstLnArgs, ConstLnState> {
public:
    ConstLnShell() : AppShell(
        "const_generator_ln.exe",
        "Генерация констант для hyperbolic CORDIC ln(x): atanh table + 1/Kh\n"
        "Вывод в формате JSON.\n"
        "Формат вывода:\n"
        "Объект с полями:\n"
        "coef        - K_h в формате Q1.(bits-1) (со знаком)\n"
        "coef_double - K_h в десятичном представлении (double)\n"
        "iterations  - список arctanh(2^{-i}), каждый элемент в формате Q1.(bits-1) (со знаком)\n"
    ) {
        app_.add_option("-n,--iterations", args_.iterations,
            "Кол-во итераций CORDIC, не включая повторы (по умолчанию 32)")->check(CLI::Range(4, 62));
    }

protected:
    bool is_batch() {
         return true; 
    } 

    std::string compute() {
        auto schedule = generate_hyperbolic_schedule(args_.iterations);
        auto atanh_table = generate_atanh_table(args_.bits, schedule);
        double Kh = compute_Kh(schedule);
        int64_t invKh = generate_inv_Kh(args_.bits, schedule);

        ordered_json out;
        out["coef"] = to_bin_string(invKh, args_.bits);
        out["coef_double"] = 1.0 / Kh;

        ordered_json iters = ordered_json::object();
        for (size_t k = 0; k < schedule.size(); k++) {
            ordered_json item;
            item["shift"] = schedule[k];
            item["atanh"] = to_bin_string(atanh_table[k], args_.bits);
            item["atanh_double"] = (double)atanh_table[k] / (double)(1LL << (args_.bits - 1));
            iters[std::to_string(k)] = item;
        }
        out["iterations"] = iters;

        return out.dump(2) + "\n";
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
    ConstLnShell app;
    return app.run(argc, argv);
}
