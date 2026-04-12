#include "converter.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

static void print_usage() {
    std::cerr
        << "Использование: converter [ПАРАМЕТРЫ] [значение1 значение2 ...]\n"
        << "\n"
        << "Преобразует углы (градусы или радианы) в формат Q1.(bits-1)\n"
        << "с фиксированной точкой. Диапазон [-2pi, 2pi) отображается\n"
        << "на [-2^(bits-1), 2^(bits-1)).\n"
        << "\n"
        << "Параметры:\n"
        << "  --deg             Вход в градусах (по умолчанию)\n"
        << "  --rad             Вход в радианах\n"
        << "  --bits N          Разрядность выхода (по умолчанию: 32)\n"
        << "  -i, --in ФАЙЛ     Читать значения из файла\n"
        << "  -o, --out ФАЙЛ    Записать результат в файл\n"
        << "  -h, --help        Показать справку\n"
        << "\n"
        << "Формат вывода (JSON):\n"
        << "  Список объектов, по одному на каждое входное значение.\n"
        << "  Каждый объект содержит:\n"
        << "    dec  - целочисленное значение в формате Q1.(bits-1)\n"
        << "    bin  - двоичная строка длиной bits бит\n"
        << "\n"
        << "Пример вывода:\n"
        << "  [\n"
        << "    {\"dec\": 536870912, \"bin\": \"00100000...0\"},\n"
        << "    {\"dec\": -536870912, \"bin\": \"11100000...0\"}\n"
        << "  ]\n";
}



int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");

    bool use_radians = false;
    int bits = 32;
    std::string in_file;
    std::string out_file;
    std::vector<std::string> raw_values;

    // ---- Разбор аргументов командной строки ----
    for (int a = 1; a < argc; a++) {
        std::string arg = argv[a];
        if (arg == "--deg") {
            use_radians = false;
        } else if (arg == "--rad") {
            use_radians = true;
        } else if (arg == "--bits") {
            if (++a >= argc) {
                std::cerr << "Ошибка: --bits требует значение\n";
                return 1;
            }
            bits = std::stoi(argv[a]);
            if (bits < 4 || bits > 62) {
                std::cerr << "Ошибка: bits должен быть в диапазоне [4, 62]\n";
                return 1;
            }
        } else if (arg == "-i" || arg == "--in") {
            if (++a >= argc) {
                std::cerr << "Ошибка: --in требует имя файла\n";
                return 1;
            }
            in_file = argv[a];
        } else if (arg == "-o" || arg == "--out") {
            if (++a >= argc) {
                std::cerr << "Ошибка: --out требует имя файла\n";
                return 1;
            }
            out_file = argv[a];
        } else if (arg == "-h" || arg == "--help") {
            print_usage();
            return 0;
        } else {
            raw_values.push_back(arg);
        }
    }

    // Чтение значений из файла, если указан
    if (!in_file.empty()) {
        auto file_vals = read_values_from_file(in_file);
        raw_values.insert(raw_values.end(),
                          file_vals.begin(), file_vals.end());
    }

    if (raw_values.empty()) {
        std::cerr << "Ошибка: не указаны входные значения\n";
        print_usage();
        return 1;
    }

    // ---- Конвертация и формирование JSON ----
        json output = json::array();
    for (size_t i = 0; i < raw_values.size(); i++) {
        double angle;
        try {
            angle = parse_number(raw_values[i]);
        } catch (const std::exception& e) {
            std::cerr << "Ошибка разбора '" << raw_values[i]
                      << "': " << e.what() << "\n";
            return 1;
        }
        double angle_rad = use_radians ? angle : deg_to_rad(angle);
        int64_t fixed = angle_rad_to_fixed(angle_rad, bits);
        output.push_back(fixed_to_json(fixed, bits));
    }

    std::string json_str = output.dump(2);  // отступ 2 пробела

    if (!out_file.empty()) {
        write_to_file(out_file, json_str + "\n");
    } else {
        std::cout << json_str << std::endl;
    }

    return 0;
}
