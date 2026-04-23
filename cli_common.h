#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <type_traits>

#include "cli11.h"

// ============================================================
// Общие параметры для всех скриптов
// ============================================================
struct CommonArgs {
    int bits = 32;
    std::string out_file;
};

// ============================================================
// Форматы вывода REPL
// ============================================================
enum class OutputFormat {
    TEXT,
    JSON
};

// ============================================================
// Состояние REPL-сессии
// ============================================================
struct ReplState {
    OutputFormat format = OutputFormat::JSON;
};

// ============================================================
// Основной класс приложения
// ============================================================
template <typename TArgs = CommonArgs, typename TReplState = ReplState>
struct AppShell {
    static_assert(std::is_base_of<CommonArgs, TArgs>::value,
                  "TArgs должен наследоваться от CommonArgs");
    static_assert(std::is_base_of<ReplState, TReplState>::value,
                  "TReplState должен наследоваться от ReplState");
                  
public:
    AppShell(const std::string& name, const std::string& description)
        : name_(name)
        , description_(description)
        , app_(description, name)
    {
        // Параметры, общие для всех скриптов
        app_.add_option("--bits", args_.bits,
            "Разрядность (по умолчанию: 32)")
            ->check(CLI::Range(4, 62));

        app_.add_option("-o,--out", args_.out_file,
            "Записать результат в файл");

        app_.set_help_flag("-h,--help", "Показать справку");
    }

    virtual ~AppShell() { }

    // ----------------------------------------------------------
    // Главный метод запуска
    // ----------------------------------------------------------
    virtual int run(int argc, char* argv[]) {
        try {
            load_args(argc, argv);
        } catch (const CLI::ParseError& e) {
            return app_.exit(e);
        } catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
            return 1;
        }
        
        // Определение режима: пакетный или интерактивный
        if (is_batch()) {
            return run_batch();
        } else {
            return run_repl();
        }
    }

protected:
    std::string name_;
    std::string description_;
    CLI::App app_;
    TReplState state_;
    TArgs args_;

    // ----------------------------------------------------------
    // Определяет режим: batch - true, repl - false
    // ----------------------------------------------------------
    virtual bool is_batch() = 0;

    // ----------------------------------------------------------
    // Загрузка аргументов в args_
    // ----------------------------------------------------------
    virtual void load_args(int argc, char* argv[]) {
        // Парсинг аргументов командной строки
        app_.parse(argc, argv);
    }

    // ----------------------------------------------------------
    // Обработка ввода repl и загрузка в args_
    // ----------------------------------------------------------
    virtual void load_repl_input(const std::string& line) = 0;

    // ----------------------------------------------------------
    // Callback для вычислений
    // Принимает значения и параметры, возвращает JSON-строку
    // ----------------------------------------------------------
    virtual std::string compute() = 0;
    
    // ----------------------------------------------------------
    // Callback для краткого текстового вывода в REPL
    // Принимает JSON-строку от ComputeFunc, возвращает
    // человекочитаемую строку
    // ----------------------------------------------------------
    virtual std::string format_text(const std::string& json_result) = 0;

    // ----------------------------------------------------------
    // Текущее состояние
    // ----------------------------------------------------------
    virtual void print_repl_status() = 0;

    // ----------------------------------------------------------
    // Обработка REPL-команд
    // ----------------------------------------------------------
    virtual bool handle_repl_command(const std::string& cmd, const std::string& arg) = 0;

    // ----------------------------------------------------------
    // Справка REPL
    // ----------------------------------------------------------
    virtual void print_repl_help() = 0;

    // ----------------------------------------------------------
    // Пакетный режим
    // ----------------------------------------------------------
    int run_batch() {
        std::string result;
        try {
            result = compute();
        } catch (const std::exception& e) {
            std::cerr << "Ошибка во время вычисления: " << e.what() << "\n";
            return 1;
        }

        if (!args_.out_file.empty()) {
            try {
                write_to_file(args_.out_file, result);
            }
            catch (const std::exception& e) {
                std::cout << e.what() << std::endl;
                return 1;
            }
        } else {
            std::cout << result << std::endl;
        }

        return 0;
    }

    // ----------------------------------------------------------
    // Интерактивный режим (REPL)
    // ----------------------------------------------------------
    int run_repl() {
        print_repl_header();

        std::string line;
        while (true) {
            std::cout << "> ";
            std::cout.flush();

            if (!std::getline(std::cin, line)) {
                // EOF (Ctrl+D / Ctrl+Z)
                std::cout << "\n";
                break;
            }

            // Убираем пробелы по краям
            line = trim(line);

            if (line.empty()) 
                continue;

            // Команды выхода
            if (line == "quit" || line == "exit" || line == "q") {
                break;
            }

            // REPL-команды начинаются с ':'
            if (line[0] == ':') {
                std::string arg;
                std::string cmd = parse_cmd(line, arg);
                bool handled = handle_repl_command(cmd, arg);
                if (!handled) {
                    std::cout << "  Неизвестная команда: :" << cmd
                              << "  (введите :help для справки)\n";
                }
                continue;
            }

            // Всё остальное — входные значения
            handle_repl_input(line);
        }

        return 0;
    }

    // ----------------------------------------------------------
    // Заголовок REPL
    // ----------------------------------------------------------
    virtual void print_repl_header() {
        std::cout << name_ << " — интерактивный режим\n";
        print_repl_status();
        std::cout << "Введите значения для вычисления, "
                  << "':help' для справки, 'quit' для выхода\n\n";
    }

    // ----------------------------------------------------------
    // Обработка ввода значений
    // ----------------------------------------------------------
    void handle_repl_input(const std::string& line) {
        load_repl_input(line);

        // Вычисление
        std::string result;
        try {
            result = compute();
        } catch (const std::exception& e) {
            std::cout << "  Ошибка: " << e.what() << "\n";
            return;
        }

        // Вывод результата
        std::string output;
        if (state_.format == OutputFormat::JSON) {
            output = result;
        } else {
            output = format_text(result);
        }

        if (!args_.out_file.empty()) {
            try {
                write_to_file(args_.out_file, output);
                std::cout << "  Результат записан в " << args_.out_file << "\n";
            }
            catch (const std::exception& e) {
                std::cout << e.what() << std::endl;
            }
        } else {
            std::cout << output << std::endl;
        }
    }

    // ----------------------------------------------------------
    // Утилита: убрать пробелы по краям
    // ----------------------------------------------------------
    static std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // ----------------------------------------------------------
    // Утилита: распарсить команду repl
    // ----------------------------------------------------------
    static std::string parse_cmd(const std::string& line, std::string& arg) {
        std::string cmd = line.substr(1);  // убираем ':'
        cmd = trim(cmd);

        // Разбиваем на команду и аргумент
        size_t space = cmd.find(' ');
        if (space != std::string::npos) {
            arg = trim(cmd.substr(space + 1));
            cmd = cmd.substr(0, space);
        }

        // Приводим к нижнему регистру
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
        return cmd;
    }
};


template <typename TArgs, typename TReplState>
bool AppShell<TArgs, TReplState>::handle_repl_command(const std::string& cmd, const std::string& arg) {
        if (cmd == "help" || cmd == "h") {
            print_repl_help();
        } else if (cmd == "bits" || cmd == "b") {
            if (arg.empty()) {
                std::cout << "  Разрядность: "
                          << args_.bits << "\n";
            } else {
                try {
                    int new_bits = std::stoi(arg);
                    if (new_bits < 4 || new_bits > 62) {
                        std::cout << "  Ошибка: разрядность должна быть"
                                  << "  в диапазоне [4, 62]\n";
                    } else {
                        args_.bits = new_bits;
                        std::cout << "  Разрядность: "
                                  << args_.bits << "\n";
                    }
                } catch (...) {
                    std::cout << "  Ошибка: некорректное значение: "
                              << arg << "\n";
                }
            }
        } else if (cmd == "json") {
            state_.format = OutputFormat::JSON;
            std::cout << "  Формат: json\n";
        } else if (cmd == "text") {
            state_.format = OutputFormat::TEXT;
            std::cout << "  Формат: text\n";
        } else if (cmd == "status" || cmd == "s") {
            print_repl_status();
        } else if (cmd == "out") {
            if (arg.empty()) {
                if (args_.out_file.empty()) {
                    std::cout << "  Вывод: консоль\n";
                } else {
                    std::cout << "  Вывод: "
                              << args_.out_file << "\n";
                }
            } else if (arg == "-" || arg == "console") {
                args_.out_file.clear();
                std::cout << "  Вывод: консоль\n";
            } else {
                args_.out_file = arg;
                std::cout << "  Вывод: " << args_.out_file << "\n";
            }
        } else {
            return false;
        }

        return true;
    }
