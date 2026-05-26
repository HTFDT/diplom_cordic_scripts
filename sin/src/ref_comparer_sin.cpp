#include "common_utils.h"
#include "converter_sin.h"
#include "const_generator_sin.h"
#include "cordic_sin.h"
#include "json.h"
#include "cli_common.h"

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <cstdint>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

// ------------------------------------------------------------
// Вспомогательные функции для работы с битами и бинарными файлами
// ------------------------------------------------------------

// Инвертирование порядка бит внутри байта
static inline uint8_t reverse_bits8(uint8_t b) {
    b = (uint8_t)((b & 0xF0) >> 4) | (uint8_t)((b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2) | (uint8_t)((b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1) | (uint8_t)((b & 0x55) << 1);
    return b;
}

struct FileBits {
    std::vector<uint8_t> bytes; // байты после восстановления порядка бит внутри каждого байта
};

// Чтение бинарного файла и восстановление порядка бит внутри каждого байта
static FileBits load_file_bits(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Не удалось открыть файл: " + filename);
    }

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    for (auto& b : buf) {
        b = reverse_bits8(b);
    }

    return FileBits{std::move(buf)};
}

// Извлечение бита из байта по индексу
static inline int get_bit(const std::vector<uint8_t>& data, uint64_t bit_index) {
    uint64_t byte_idx = bit_index >> 3;
    uint64_t bit_in_byte = bit_index & 7ULL; // 0..7

    if (byte_idx >= data.size()) {
        return 0;
    }

    uint8_t byte = data[(size_t)byte_idx];
    int shift = 7 - (int)bit_in_byte;
    return (byte >> shift) & 1;
}

// Автопоиск offset: первое появление единицы в битовом потоке
static std::optional<uint64_t> find_first_bit(const FileBits& fb) {
    uint64_t total_bits = (uint64_t)fb.bytes.size() * 8ULL;
    for (uint64_t i = 0; i < total_bits; i++) {
        if (get_bit(fb.bytes, i) == 1) {
            return i;
        }
    }
    return std::nullopt;
}

// Извлечение одного слова:
// - слово всегда начинается со старт-бита в offset
// - после старт-бита идут bits бит данных
static int64_t extract_word_tc(const FileBits& fb, uint64_t offset_bits, int bits) {
    uint64_t data_start = offset_bits + 1ULL; // старт-бит всегда присутствует

    std::vector<int> bits_time_order;
    bits_time_order.reserve((size_t)bits);

    for (int i = 0; i < bits; i++) {
        int bit = get_bit(fb.bytes, data_start + (uint64_t)i);
        bits_time_order.push_back(bit);
    }

    // В файле данные LSB-first, разворачиваем в MSB-first
    std::reverse(bits_time_order.begin(), bits_time_order.end());

    std::string word_msb;
    word_msb.reserve((size_t)bits);
    for (int b : bits_time_order) {
        word_msb.push_back(b ? '1' : '0');
    }

    int64_t u = parse_fixed_bin(word_msb, bits);
    return truncate(u, bits);
}

// ------------------------------------------------------------
// Выбор компоненты
// ------------------------------------------------------------

enum class CompareComponent {
    RESULT,
    X,
    Y,
    Z
};

static inline std::string component_to_string(CompareComponent c) {
    switch (c) {
        case CompareComponent::RESULT: return "result";
        case CompareComponent::X:      return "x";
        case CompareComponent::Y:      return "y";
        case CompareComponent::Z:      return "z";
    }
    return "result";
}

static inline CompareComponent parse_component(const std::string& s) {
    std::string t = s;
    std::transform(t.begin(), t.end(), t.begin(), ::tolower);

    if (t == "result") return CompareComponent::RESULT;
    if (t == "x")      return CompareComponent::X;
    if (t == "y")      return CompareComponent::Y;
    if (t == "z")      return CompareComponent::Z;

    throw std::invalid_argument("Некорректная component: " + s + " (ожидается result/x/y/z)");
}

// ------------------------------------------------------------
// Аргументы и состояние
// ------------------------------------------------------------

struct RefComparerSinArgs : CommonArgs {
    int iterations = 32;
    CompareComponent component = CompareComponent::RESULT;
    std::optional<int> iter_num;
    std::optional<uint64_t> offset;
    std::string input_bin;
    std::string value_file;
};

struct RefComparerSinState : ReplState {
    // Значения по умолчанию для REPL
    std::string default_input_bin;
    std::string default_value_file;
    std::optional<uint64_t> default_offset;
    CompareComponent default_component = CompareComponent::RESULT;
    std::optional<int> default_iter_num;
};

// ------------------------------------------------------------
// Основной класс приложения
// ------------------------------------------------------------

struct RefComparerSinShell : AppShell<RefComparerSinArgs, RefComparerSinState> {
public:
    RefComparerSinShell()
        : AppShell(
            "ref_comparer_sin.exe",
            "Сравнение выходного значения из макроса IDE с эталоном CORDIC sin\n"
        )
    {
        app_.add_option("-n,--iterations", args_.iterations,
            "Количество итераций CORDIC (по умолчанию: 32)")
            ->check(CLI::Range(4, 64));

        app_.add_option_function<std::string>("--component",
            [this](const std::string& s) { args_.component = parse_component(s); },
            "Компонента для сравнения: result/x/y/z");

        app_.add_option("--iter-num", args_.iter_num,
            "Номер итерации (обязателен для x/y/z)");

        app_.add_option("--offset", args_.offset,
            "Смещение старт-бита в бинарном файле (если не задано — первое найденное значение)");

        app_.add_option("--input", args_.input_bin,
            "Входное значение (двоичная строка)");

        app_.add_option("value", args_.value_file,
            "Бинарный файл со значением")
            ->expected(1)
            ->check(CLI::ExistingFile);
    }

protected:
    bool is_batch() {
        return !args_.value_file.empty();
    }

    void load_repl_input(const std::string& line) {
        args_.value_file = trim(line);
    }

    void validate_args() {
        AppShell<RefComparerSinArgs, RefComparerSinState>::validate_args();

        if (args_.input_bin.empty()) {
            throw std::invalid_argument("Не задан входной аргумент --input.");
        }

        if (args_.value_file.empty()) {
            throw std::invalid_argument("Не задан файл.");
        }

        if (args_.component != CompareComponent::RESULT && !args_.iter_num.has_value()) {
            throw std::invalid_argument("Для component = x/y/z необходимо указать --iter-num.");
        }
    }

    std::string compute() {
        int64_t input_fixed = parse_fixed_bin(args_.input_bin, args_.bits);
        double angle_rad = angle_fixed_to_rad(input_fixed, args_.bits);
        double angle_deg = rad_to_deg(angle_rad);

        auto atan_table = generate_atan_table(args_.bits, args_.iterations);
        int64_t k_inv = generate_k_inv(args_.bits, args_.iterations);

        CordicResult ref = compute_sin(
            input_fixed,
            angle_deg,
            angle_rad,
            args_.bits,
            atan_table,
            k_inv
        );

        // Выбираем эталонное значение
        int64_t ref_value = 0;
        if (args_.component == CompareComponent::RESULT) {
            ref_value = ref.result_fixed;
        } else {
            int it = *args_.iter_num;
            if (it < 0 || it >= (int)ref.iterations.size()) {
                throw std::invalid_argument("iter-num выходит за диапазон эталонных итераций.");
            }

            const auto& iter = ref.iterations[(size_t)it];
            switch (args_.component) {
                case CompareComponent::X: ref_value = iter.cos_val; break;
                case CompareComponent::Y: ref_value = iter.sin_val; break;
                case CompareComponent::Z: ref_value = iter.z_val; break;
                default: break;
            }
        }

        // Читаем бинарный файл
        FileBits fb = load_file_bits(args_.value_file);

        uint64_t used_offset = 0;
        std::string offset_mode;

        if (args_.offset.has_value()) {
            used_offset = *args_.offset;
            offset_mode = "manual";
        } else {
            auto found = find_first_bit(fb);
            if (found.has_value()) {
                used_offset = *found;
                offset_mode = "auto";
            } else {
                used_offset = 0;
                offset_mode = "auto_not_found";
            }
        }

        int64_t file_value = extract_word_tc(fb, used_offset, args_.bits);

        bool ok = (truncate(file_value, args_.bits) == truncate(ref_value, args_.bits));

        ordered_json out;
        out["bits"] = args_.bits;
        out["iterations"] = args_.iterations;
        out["component"] = component_to_string(args_.component);
        if (args_.component != CompareComponent::RESULT) {
            out["iter_num"] = *args_.iter_num;
        }
        out["input"] = args_.input_bin;
        out["value_file"] = args_.value_file;
        out["offset"] = used_offset;
        out["offset_mode"] = offset_mode;
        out["status"] = ok ? "OK" : "ERR";
        out["reference"] = to_bin_string(ref_value, args_.bits);
        out["value"] = to_bin_string(file_value, args_.bits);

        return out.dump(2) + "\n";
    }

    std::string format_text(const std::string& json_result) {
        return json_result;
    }

    void print_repl_status() {
        std::cout
            << "  Разрядность: " << args_.bits << "\n"
            << "  Кол-во итераций: " << args_.iterations << "\n"
            << "  Компонента по умолчанию: " << component_to_string(state_.default_component) << "\n"
            << "  Номер итерации по умолчанию: "
            << (state_.default_iter_num.has_value() ? std::to_string(*state_.default_iter_num) : "-") << "\n"
            << "  Смещение по умолчанию: "
            << (state_.default_offset.has_value() ? std::to_string(*state_.default_offset) : "auto") << "\n"
            << "  Входное значение по умолчанию: "
            << (state_.default_input_bin.empty() ? "-" : state_.default_input_bin) << "\n"
            << "  Файл по умолчанию: "
            << (state_.default_value_file.empty() ? "-" : state_.default_value_file) << "\n"
            << "  Вывод: " << (args_.out_file.empty() ? "консоль" : args_.out_file) << "\n\n";
    }

    bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        bool handled = AppShell<RefComparerSinArgs, RefComparerSinState>::handle_repl_command(cmd, arg);
        if (handled)
            return true;

        if (cmd == "iter") {
            if (arg.empty()) {
                std::cout << "  Кол-во итераций: " << args_.iterations << "\n";
            } else {
                try {
                    int v = std::stoi(arg);
                    if (v < 4 || v > 64) {
                        std::cout << "  Ошибка: кол-во итераций должно быть в диапазоне [4, 64]\n";
                    } else {
                        args_.iterations = v;
                        std::cout << "  Кол-во итераций: " << args_.iterations << "\n";
                    }
                } catch (...) {
                    std::cout << "  Ошибка: некорректное значение iterations\n";
                }
            }
            return true;
        }

        if (cmd == "comp") {
            if (arg.empty()) {
                std::cout << "  Компонента по умолчанию: " << component_to_string(state_.default_component) << "\n";
            } else {
                try {
                    state_.default_component = parse_component(arg);
                    std::cout << "  Компонента по умолчанию: " << component_to_string(state_.default_component) << "\n";
                } catch (const std::exception& e) {
                    std::cout << "  Ошибка: " << e.what() << "\n";
                }
            }
            return true;
        }

        if (cmd == "iter_num") {
            if (arg.empty()) {
                std::cout << "  Номер итерации по умолчанию: "
                          << (state_.default_iter_num.has_value() ? std::to_string(*state_.default_iter_num) : "-")
                          << "\n";
            } else {
                try {
                    int v = std::stoi(arg);
                    if (v < 0 || v > args_.iterations)
                        throw std::invalid_argument("Некорректный номер итерации");
                    state_.default_iter_num = v;
                    std::cout << "  Номер итерации по умолчанию: " << *state_.default_iter_num << "\n";
                } catch (...) {
                    std::cout << "  Ошибка: некорректный номер итерации\n";
                }
            }
            return true;
        }

        if (cmd == "offset") {
            if (arg.empty()) {
                std::cout << "  Смещение по умолчанию: "
                          << (state_.default_offset.has_value() ? std::to_string(*state_.default_offset) : "auto")
                          << "\n";
            } else if (arg == "auto") {
                state_.default_offset.reset();
                std::cout << "  Смещение по умолчанию: auto\n";
            } else {
                try {
                    int v = std::stoi(arg);
                    if (v < 0)
                        throw std::invalid_argument("Некорректное смещение");
                    state_.default_offset = (uint64_t)v;
                    std::cout << "  Смещение по умолчанию: " << *state_.default_offset << "\n";
                } catch (...) {
                    std::cout << "  Ошибка: некорректное смещение\n";
                }
            }
            return true;
        }

        if (cmd == "input") {
            if (arg.empty()) {
                std::cout << "  Входное значение по умолчанию: "
                          << (state_.default_input_bin.empty() ? "-" : state_.default_input_bin)
                          << "\n";
            } else {
                try {
                    parse_fixed_bin(arg, args_.bits);
                    state_.default_input_bin = arg;
                    std::cout << "  Входное значение по умолчанию: " << state_.default_input_bin << "\n";
                } catch (const std::exception& e) {
                    std::cerr << "Ошибка: " << e.what() << "\n";
                }
            }
            return true;
        }

        if (cmd == "value") {
            if (arg.empty()) {
                std::cout << "  Файл по умолчанию: "
                          << (state_.default_value_file.empty() ? "-" : state_.default_value_file)
                          << "\n";
            } else {
                state_.default_value_file = arg;
                std::cout << "  Файл по умолчанию: " << state_.default_value_file << "\n";
            }
            return true;
        }

        return false;
    }

    void print_repl_help() {
        std::cout
            << "Команды:\n"
            << "  :bits N         Установить разрядность\n"
            << "  :iter N         Установить количество итераций\n"
            << "  :comp NAME      Компонента по умолчанию: result/x/y/z\n"
            << "  :iter_num N     Номер итерации по умолчанию\n"
            << "  :offset N       Смещение по умолчанию\n"
            << "  :offset auto    Смещение по умолчанию = автопоиск\n"
            << "  :input BIN      Входное слово по умолчанию\n"
            << "  :value FILE     Бинарный файл по умолчанию\n"
            << "  :out FILE       Записать результат в файл\n"
            << "  :out -          Выводить в консоль\n"
            << "  :status         Показать текущие настройки\n"
            << "  :help           Показать справку\n"
            << "  quit            Выход\n\n"
            << "Обычный ввод в REPL запускает интерактивный опрос параметров сравнения.\n";
    }

    int run_repl() {
        print_repl_header();

        while (true) {
            std::cout << "\nНовый запуск сравнения? [Enter = да, quit = выход, :help = помощь]\n> ";
            std::cout.flush();

            std::string line;
            if (!std::getline(std::cin, line)) {
                std::cout << "\n";
                break;
            }

            line = trim(line);

            if (line == "quit" || line == "exit" || line == "q") {
                break;
            }

            if (!line.empty() && line[0] == ':') {
                std::string arg;
                std::string cmd = parse_cmd(line, arg);
                bool handled = handle_repl_command(cmd, arg);
                if (!handled) {
                    std::cout << "  Неизвестная команда. Введите :help для справки.\n";
                }
                continue;
            }

            try {
                ask_run_parameters();
                std::string result = compute();

                if (!args_.out_file.empty()) {
                    write_to_file(args_.out_file, result);
                    std::cout << "  Результат записан в " << args_.out_file << "\n";
                } else {
                    std::cout << result << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cout << "  Ошибка: " << e.what() << "\n";
            }
        }

        return 0;
    }

private:
    // --------------------------------------------------------
    // Вспомогательный ввод с дефолтом
    // --------------------------------------------------------
    static std::string ask_with_default(const std::string& prompt, const std::string& def) {
        std::cout << prompt;
        if (!def.empty()) {
            std::cout << " [" << def << "]";
        }
        std::cout << ": ";
        std::cout.flush();

        std::string line;
        std::getline(std::cin, line);
        line = trim(line);
        if (line.empty()) 
            return def;
        return line;
    }

    static std::string ask_with_default_opt(const std::string& prompt, const std::optional<std::string>& def) {
        return ask_with_default(prompt, def.has_value() ? *def : "");
    }

    void ask_run_parameters() {
        // component
        {
            std::string s = ask_with_default(
                "Компонента (result/x/y/z)",
                component_to_string(state_.default_component)
            );
            args_.component = parse_component(s);
            state_.default_component = args_.component;
        }

        // iter_num
        if (args_.component != CompareComponent::RESULT) {
            std::string def = state_.default_iter_num.has_value()
                ? std::to_string(*state_.default_iter_num)
                : "";
            std::string s = ask_with_default("Номер итерации", def);
            if (s.empty()) {
                throw std::invalid_argument("Для component = x/y/z необходимо указать номер итерации");
            }
            int v = std::stoi(s);
            if (v < 0 || v > args_.iterations)
                throw std::invalid_argument("Некорректный номер итерации");
            args_.iter_num = v;
            state_.default_iter_num = args_.iter_num;
        }

        // offset
        {
            std::string def = state_.default_offset.has_value()
                ? std::to_string(*state_.default_offset)
                : "auto";
            std::string s = ask_with_default("offset (или auto)", def);
            if (s == "auto" || s.empty()) {
                args_.offset.reset();
                state_.default_offset.reset();
            } else {
                int v = std::stoi(s);
                if (v < 0)
                    throw std::invalid_argument("Некорректное смещение");
                args_.offset = (uint64_t)v;
                state_.default_offset = args_.offset;
            }
        }

        // input
        {
            std::string s = ask_with_default("Входное значение (bin)", state_.default_input_bin);
            if (s.empty()) {
                throw std::invalid_argument("Не задано входное значение");
            }
            parse_fixed_bin(s, args_.bits);
            args_.input_bin = s;
            state_.default_input_bin = s;
        }

        // value
        {
            std::string s = ask_with_default("Бинарный файл", state_.default_value_file);
            if (s.empty()) {
                throw std::invalid_argument("Не задан файл");
            }
            args_.value_file = s;
            state_.default_value_file = s;
        }
    }

public:
    int run(int argc, char* argv[]) {
        try {
            load_args(argc, argv);
        } catch (const CLI::ParseError& e) {
            return app_.exit(e);
        } catch (const std::exception& e) {
            std::cout << e.what() << std::endl;
            return 1;
        }

        if (is_batch()) {
            return run_batch();
        } else {
            return run_repl();
        }
    }
};

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    RefComparerSinShell shell;
    return shell.run(argc, argv);
}
