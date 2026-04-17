#include "const_generator.h"
#include "converter.h"
#include "json.h"
using json = nlohmann::json;
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <iomanip>
#include <cmath>


static void print_usage() {
    std::cerr
        << "Использование: const_generator [ПАРАМЕТРЫ]\n"
        << "\n"
        << "Генерирует константы CORDIC: K_inv (коэффициент масштабирования) и таблицу arctan(2^{-i}).\n"
        << "Вывод в формате JSON.\n"
        << "\n"
        << "Параметры:\n"
        << "  --bits N             Разрядность констант (по умолчанию: 32)\n"
        << "  -n, --iterations N   Количество констант (по умолчанию: bits-1)\n"
        << "  -o, --out ФАЙЛ       Записать в файл\n"
        << "  -h, --help           Показать справку\n"
        << "\n"
        << "Формат вывода (JSON):\n"
        << "  Объект с полями:\n"
        << "    coef        - K_inv в формате {dec, bin}\n"
        << "    coef_double - K_inv в десятичном представлении (double)\n"
        << "    iterations  - список arctan(2^{-i}), каждый элемент {dec, bin}\n"
        << "\n"
        << "Пример вывода:\n"
        << "  {\n"
        << "    \"coef\": {\"dec\": 1304969940, \"bin\": \"01001101...\"},\n"
        << "    \"coef_double\": 0.607252935008881,\n"
        << "    \"iterations\": [\n"
        << "      {\"dec\": 1073741824, \"bin\": \"01000000...\"},\n"
        << "      ...\n"
        << "    ]\n"
        << "  }\n";
}


int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    int bits = 32;
    int iterations = bits;
    std::string out_file;

    for (int a = 1; a < argc; a++) {
        std::string arg = argv[a];
        if (arg == "--bits") {
            if (++a >= argc) { std::cerr << "Ошибка: --bits требует значение\n"; return 1; }
            bits = std::stoi(argv[a]);
            if (bits < 4 || bits > 62) { std::cerr << "Ошибка: bits должен быть в диапазоне [4, 62]\n"; return 1; }
        } else if (arg == "-n" || arg == "--iterations") {
            if (++a >= argc) { std::cerr << "Ошибка: --iterations требует значение\n"; return 1; }
            iterations = std::stoi(argv[a]);
            if (iterations < 0) {
                std::cerr << "Ошибка: iterations должен быть > 0\n";
                return 1;
            }
        } else if (arg == "-o" || arg == "--out") {
            if (++a >= argc) { std::cerr << "Ошибка: --out требует имя файла\n"; return 1; }
            out_file = argv[a];
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Неизвестный аргумент: " << arg << "\n";
            return 1;
        }
    }

    auto atan_table = generate_atan_table(bits, iterations);
    int64_t k_inv   = generate_k_inv(bits, iterations);

    // K_inv как double
    double k_inv_double = (double)k_inv / (double)(1LL << (bits - 1));

    // JSON
    json iter_array = json::array();
    for (int i = 0; i < iterations; i++) {
        iter_array.push_back(fixed_to_json(atan_table[i], bits));
    }

    json output = {
        {"coef", fixed_to_json(k_inv, bits)},
        {"coef_double", k_inv_double},
        {"iterations", iter_array}
    };

    std::string json_str = output.dump(2);

    if (!out_file.empty()) {
        std::ofstream f(out_file);
        write_to_file(out_file, json_str + "\n");
    } else {
        std::cout << json_str << "\n";
    }

    return 0;
}
