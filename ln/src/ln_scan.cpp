#include <chrono>   // Для таймера
#include <iomanip>  // Для setw, setprecision

#include "cli_common.h"
#include "json.h"
#include "common_utils.h"
#include "const_generator_ln.h"
#include "converter_ln.h"
#include "cordic_ln.h"

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <limits>

// Проверка на переполнение при сложении/вычитании в знаковом формате bits
static inline bool check_overflow(int64_t a, int64_t b, int64_t res, int bits) {
    // Переполнение происходит, если знаки операндов одинаковы, а знак результата отличается
    int64_t sign_bit = 1LL << (bits - 1);
    
    // Нормализуем знаки к 0 или 1 (старший бит)
    int sa = (a & sign_bit) ? 1 : 0;
    int sb = (b & sign_bit) ? 1 : 0;
    int sr = (res & sign_bit) ? 1 : 0;

    if (sa == sb && sa != sr) return true;
    return false;
}

struct ScanArgs : CommonArgs {
    int iterations = 32;
    std::string out_file_stats; // файл для статистики
};

struct LnScanShell : AppShell<ScanArgs, ReplState> {
public:
    LnScanShell() : AppShell(
        "ln_scan.exe",
        "Сканирование диапазона [0.5, 1) для hyperbolic CORDIC ln(x)\n"
        "Поиск переполнений и оценка точности.\n"
    ) {
        app_.add_option("-n,--iterations", args_.iterations,
            "Макс. индекс i (базовые итерации 1..N)")->check(CLI::Range(1, 62));

            
        app_.add_option("--out-stats", args_.out_file_stats,
            "Записать подробную статистику ошибок в файл (JSON)");
    }

protected:
    bool is_batch() 
    { 
        return true; 
    }

    void load_repl_input(const std::string&) {
        throw std::logic_error("Not implemented");
    }

    std::string compute() {
        // Генерация расписания и таблиц
        auto schedule = generate_hyperbolic_schedule(args_.iterations);
        auto atanh_table = generate_atanh_table(args_.bits, schedule);
        
        int64_t min_val = -(1LL << (args_.bits - 1));
        int64_t max_val = (1LL << (args_.bits - 1)) - 1;
        
        // Диапазон входа [0.5, 1) в signed Q1.(bits-1)
        int64_t start_code = 1LL << (args_.bits - 2); 
        int64_t end_code = max_val;

        uint64_t total_inputs = (uint64_t)(end_code - start_code + 1);
        uint64_t inputs_with_overflow = 0;
        uint64_t total_overflow_events = 0;
        
        long double sum_abs_err = 0.0L;
        int64_t max_abs_err = 0;
        int64_t arg_at_max_err = start_code;
        
        // Для детальной статистики
        std::vector<ordered_json> error_details;
        bool save_details = !args_.out_file_stats.empty();
        const uint64_t MAX_DETAILS = 500; 

        std::cerr << "Starting scan..." << std::endl;
        std::cerr << "Range: [" << to_bin_string(start_code, args_.bits) << ", " << to_bin_string(end_code, args_.bits) << ") (" 
                  << total_inputs << " values)" << std::endl;
        std::cerr << "Config: bits=" << args_.bits << ", max_iter_idx=" << args_.iterations 
                  << " (steps=" << schedule.size() << ")" << std::endl;
        std::cerr << "----------------------------------------" << std::endl;

        auto start_time = std::chrono::high_resolution_clock::now();
        uint64_t last_report_percent = 0;

        for (int64_t a_fixed = start_code; a_fixed <= end_code; ++a_fixed) {
            uint64_t current_idx = (uint64_t)(a_fixed - start_code);
            
            // --- Progress Report (каждый 1% или в конце) ---
            uint64_t percent = (total_inputs > 0) ? (current_idx * 100 / total_inputs) : 0;
            
            if (percent >= last_report_percent + 1 || current_idx == total_inputs - 1) {
                auto now = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                
                // Оценка оставшегося времени (если прошло больше 5 сек и хоть 1%)
                long long eta_seconds = -1;
                if (duration > 5 && percent > 0) {
                    eta_seconds = (long long)((double)duration / percent * (100 - percent));
                }

                std::cerr << "[" << std::setw(3) << percent << "%] "
                          << "Processed: " << current_idx << "/" << total_inputs;
                
                if (inputs_with_overflow > 0) {
                    std::cerr << " | Overflows: " << inputs_with_overflow 
                              << " (" << total_overflow_events << " events)";
                } else {
                    std::cerr << " | Overflows: 0";
                }

                std::cerr << " | Max Err: " << max_abs_err << " LSB";
                
                if (eta_seconds >= 0) {
                    std::cerr << " | ETA: ~" << eta_seconds << "s";
                }
                std::cerr << std::endl;

                last_report_percent = percent;
            }
            // -----------------------------------------------

            double a_double = ln_fixed_to_real(a_fixed, args_.bits);
            
            // Вызываем основную функцию вычисления с сохранением итераций
            LnResult res = compute_ln(
                a_fixed, a_double, args_.bits,
                schedule, atanh_table
            );

            bool input_has_overflow = false;
            std::string overflow_val;
            int overflow_on_iter = -1;

            // Анализ итераций на предмет переполнения
            for (size_t k = 1; k < res.iterations.size(); k++) {
                const auto& prev = res.iterations[k-1];
                const auto& curr = res.iterations[k];
                int i_shift = schedule[k-1];

                // Проверка X
                int d = (prev.y < 0) ? 1 : -1;
                int64_t y_sh = prev.y >> i_shift;
                int64_t contrib_x = d * y_sh;
                
                // Проверка переполнения сложения для X
                if ((prev.x > 0 && contrib_x > 0 && curr.x < 0) ||
                    (prev.x < 0 && contrib_x < 0 && curr.x > 0)) {
                    input_has_overflow = true;
                    overflow_val = "X";
                }

                // Проверка для Y
                int64_t x_sh = prev.x >> i_shift;
                int64_t contrib_y = d * x_sh;
                if ((prev.y > 0 && contrib_y > 0 && curr.y < 0) ||
                    (prev.y < 0 && contrib_y < 0 && curr.y > 0)) {
                    input_has_overflow = true;
                    overflow_val = "Y";
                }
                
                // Проверка для Z (реже, но возможно)
                int64_t atan_val = atanh_table[k-1];
                int64_t contrib_z = -d * atan_val;
                if ((prev.z > 0 && contrib_z > 0 && curr.z < 0) ||
                    (prev.z < 0 && contrib_z < 0 && curr.z > 0)) {
                    input_has_overflow = true;
                    overflow_val = "Z";
                }

                if (input_has_overflow) {
                    overflow_on_iter = k;
                    break;
                }
            }

            if (input_has_overflow) {
                inputs_with_overflow++;
                total_overflow_events += 1;
            }

            // Расчет ошибки
            double ref = std::log(a_double);
            double err = res.result_double - ref;
            double abs_err = std::fabs(err);
            sum_abs_err += (long double)abs_err;

            int64_t diff_fixed = res.result_fixed - res.reference_fixed;
            int64_t abs_diff_fixed = (diff_fixed >= 0) ? diff_fixed : -diff_fixed;

            if (abs_diff_fixed > max_abs_err) {
                max_abs_err = abs_diff_fixed;
                arg_at_max_err = a_fixed;
            }

            if (save_details && error_details.size() < MAX_DETAILS && (input_has_overflow || abs_diff_fixed > 4)) {
                ordered_json item;
                item["input_fixed"] = to_bin_string(a_fixed, args_.bits);
                item["input_real"] = a_double;
                item["result_fixed"] = to_bin_string(res.result_fixed, args_.bits);
                item["reference_fixed"] = to_bin_string(res.reference_fixed, args_.bits);
                item["error_lsb"] = diff_fixed;
                item["has_overflow"] = input_has_overflow;
                item["overflow_val"] = overflow_val;
                item["overflow_on_iter"] = overflow_on_iter;
                error_details.push_back(item);
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

        // Формирование итогового отчета
        ordered_json report;
        report["config"] = {
            {"bits", args_.bits},
            {"max_iteration_index", args_.iterations},
            {"schedule_length", (int)schedule.size()}
        };

        report["range"] = {
            {"start_fixed", to_bin_string(start_code, args_.bits)},
            {"end_fixed", to_bin_string(end_code, args_.bits)},
            {"total_values", total_inputs}
        };

        report["overflow_stats"] = {
            {"inputs_with_overflow", inputs_with_overflow},
            {"percentage", (double)inputs_with_overflow / (double)total_inputs * 100.0},
            {"total_overflow_events", total_overflow_events}
        };

        double mean_err = (double)(sum_abs_err / (long double)total_inputs);
        double mean_err_lsb = mean_err * (double)(1LL << (args_.bits - 1));

        report["error_stats"] = {
            {"max_error_lsb", max_abs_err},
            {"max_error_arg_fixed", to_bin_string(arg_at_max_err, args_.bits)},
            {"max_error_arg_real", ln_fixed_to_real(arg_at_max_err, args_.bits)},
            {"mean_absolute_error_lsb", mean_err_lsb}
        };
        
        report["execution_time_seconds"] = total_duration;

        if (save_details) {
            report["sample_errors"] = error_details;
            try {
                write_to_file(args_.out_file_stats, report.dump(2));
                std::cerr << "\nDetailed stats saved to: " << args_.out_file_stats << std::endl;
            } catch (...) {}
        }

        // Финальный вывод в консоль
        std::ostringstream oss;
        oss << "\n=== LN SCAN COMPLETE ===\n";
        oss << "Time elapsed: " << total_duration << "s\n\n";
        
        oss << "Configuration:\n";
        oss << "  Bits: " << args_.bits << ", Max Iter Index: " << args_.iterations 
            << " (Steps: " << schedule.size() << ")\n";
        
        oss << "Overflow Statistics:\n";
        oss << "  Inputs with overflow: " << inputs_with_overflow 
            << " (" << std::fixed << std::setprecision(4) 
            << (double)inputs_with_overflow / (double)total_inputs * 100.0 << "%)\n";
        oss << "  Total overflow events: " << total_overflow_events << "\n\n";

        oss << "Accuracy Statistics:\n";
        oss << "  Max Error: " << max_abs_err << " LSB\n";
        oss << "  At input: " << to_bin_string(arg_at_max_err, args_.bits) 
            << " (~" << ln_fixed_to_real(arg_at_max_err, args_.bits) << ")\n";
        oss << "  Mean Abs Error: " << std::setprecision(4) << mean_err_lsb << " LSB\n";
        
        return oss.str();
    }

    std::string format_text(const std::string& json_result) { 
        throw std::logic_error("Not implemented");
    }
    
    void print_repl_status() {
        throw std::logic_error("Not implemented");
    }
    bool handle_repl_command(const std::string&, const std::string&) {
         throw std::logic_error("Not implemented");
    }
    void print_repl_help() {
        throw std::logic_error("Not implemented");
    }
};

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    LnScanShell app;
    return app.run(argc, argv);
}
