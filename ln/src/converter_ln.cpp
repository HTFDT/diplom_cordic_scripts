#include "cli_common.h"
#include "json.h"

#include "int_utils.h"
#include "common_utils.h"
#include "converter_ln.h"

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <iostream>
#include <vector>
#include <string>
#include <sstream>


struct LnConvState : ReplState { };

struct LnConvArgs : CommonArgs {
    std::string in_file;
    std::vector<std::string> values;
};

struct LnConverterShell : AppShell<LnConvArgs, LnConvState> {
public:
    LnConverterShell() : AppShell(
        "converter_ln.exe",
        "Конвертация входа ln(1+x): real x в [0,1) <-> fixed (Q1.(bits-1))\n"
    ) {
        app_.add_option("-i,--in", args_.in_file,
            "Читать значения из файла")->check(CLI::ExistingFile);

        app_.add_option("values", args_.values,
            "Входные значения")->expected(-1);
    }

protected:
    bool is_batch() { 
        return !args_.values.empty(); 
    }

    void load_args(int argc, char* argv[]) {
        AppShell<LnConvArgs, LnConvState>::load_args(argc, argv);
        if (!args_.in_file.empty()) {
            auto v = read_values_from_file(args_.in_file);
            args_.values.insert(args_.values.end(), v.begin(), v.end());
        }
    }

    void load_repl_input(const std::string& line) {
        std::vector<std::string> v;
        std::istringstream iss(line);
        std::string t;
        while (iss >> t)
            v.push_back(t);
        args_.values = v;
    }

    std::string compute() {
        ordered_json out = ordered_json::array();

        for (auto &s : args_.values) {
            ordered_json item;

            double a = parse_number(s);
            int64_t fx = ln_real_to_fixed(a, args_.bits);

            item["input_real"] = a;
            item["fixed"] = to_bin_string(fx, args_.bits);

            out.push_back(item);
        }

        return out.dump(2) + "\n";
    }

    std::string format_text(const std::string& json_result) {
        return json_result;
    }
        
    bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        return AppShell<LnConvArgs, LnConvState>::handle_repl_command(cmd, arg);
    }

    void print_repl_status() {
        std::cout << "  Разрядность: " << args_.bits << "\n"
                  << "  Вывод: " << (args_.out_file.empty() ? "console" : args_.out_file) << "\n\n";
    }

    void print_repl_help() {
        std::cout
            << "Команды:\n"
            << "  :bits N         Разрядность\n"
            << "  :out FILE       Записать вывод в файл\n"
            << "  :out -          Вывод в консоль\n"
            << "  :status         Статус\n"
            << "  :help           Справка\n"
            << "  quit            Выход\n\n";
    }
};

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    LnConverterShell app;
    return app.run(argc, argv);
}
