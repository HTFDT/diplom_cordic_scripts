#include "converter.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <stdexcept>

#include "cli_common.h"

// класс доп. аргументов командной строки
struct ConverterArgs : CommonArgs {
    bool use_radians = false;
    std::string in_file;
    std::vector<std::string> values;
};

// класс с функциональностью cli и repl для converter.exe
struct ConverterShell : AppShell<ConverterArgs> {
public:
    ConverterShell() : AppShell(
        "converter.exe", 
        "Преобразует углы (градусы или радианы) в формат Q0.(bits)\n"
        "с фиксированной точкой. Диапазон [-2pi, 2pi) отображается\n"
        "на [0, 2^(bits)).\n"
    ) {
        app_.add_flag("--rad", args_.use_radians,
            "Вход в радианах (по умолчанию: градусы)");

        bool placeholder;
        app_.add_flag("--deg", placeholder,
            "Вход в градусах (по умолчанию)")
            ->excludes("--rad");

        app_.add_option("-i,--in", args_.in_file,
            "Читать значения из файла")
            ->check(CLI::ExistingFile);

        app_.add_option("values", args_.values,
            "Входные значения")
            ->expected(-1);
    }

protected:
    virtual bool is_batch() {
        return !args_.values.empty();
    }

    virtual void load_args(int args, char* argv[]) {
        AppShell<ConverterArgs>::load_args(args, argv);

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
        // ---- Конвертация и формирование JSON ----
        json output = json::array();
        for (size_t i = 0; i < args_.values.size(); i++) {
            double angle;
            try {
                angle = parse_number(args_.values[i]);
            } catch (const std::exception& e) {
                throw std::invalid_argument("Ошибка разбора '" + args_.values[i] + "': " + e.what());
            }
            double angle_rad = args_.use_radians ? angle : deg_to_rad(angle);
            int64_t fixed = angle_rad_to_fixed(angle_rad, args_.bits);
            output.push_back(to_bin_string(fixed, args_.bits));
        }

        std::string json_str = output.dump(2);  // отступ 2 пробела

        return json_str;
    }

    virtual std::string format_text(const std::string& json_result) {
        json j = json::parse(json_result);

        if (!j.is_array()) {
            throw std::invalid_argument("Получена неверная json-строка, ожидался массив: " + json_result);
        }

        std::string s;
        bool f = true;
        for (const auto& el : j) {
            if (!f) s += "\n";
            f = false;
            s += el.get<std::string>();
        }

        return s;
    }

    virtual void print_repl_status() {
        std::cout << "  Режим: " << (args_.use_radians ? "радианы" : "градусы") << "\n"
                  << "  Разрядность: " << args_.bits << "\n"
                  << "  Формат: " << (state_.format == OutputFormat::JSON ? "json" : "text") << "\n"
                  << "  Вывод: " << (args_.out_file.empty() ? "консоль" : args_.out_file) << "\n"
                  << "\n";
    }

    virtual bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        bool handled = AppShell<ConverterArgs>::handle_repl_command(cmd, arg);

        if (handled)
            return true;
        
        if (cmd == "deg") {
            args_.use_radians = false;
            std::cout << "  Режим: градусы\n";
        } else if (cmd == "rad") {
            args_.use_radians = true;
            std::cout << "  Режим: радианы\n";
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
            << "  :bits N         Установить разрядность\n"
            << "  :json           Вывод в формате JSON\n"
            << "  :text           Вывод в текстовом формате\n"
            << "  :out ФАЙЛ       Записывать результат в файл\n"
            << "  :out -          Вывод на консоль\n"
            << "  :status         Показать текущие настройки\n"
            << "  :help           Показать эту справку\n"
            << "  quit            Выход\n"
            << "\n"
            << "Ввод значений:\n"
            << "  Одно или несколько чисел через пробел.\n"
            << "  Десятичный разделитель: точка или запятая.\n"
            << "  Пример: 45 90.5 -135,7\n";
    }
};

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    ConverterShell shell;

    return shell.run(argc, argv);
}
