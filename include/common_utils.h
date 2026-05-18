#pragma once

#include <string>
#include <iostream>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <bitset>
#include "json.h"
using json = nlohmann::json;

// ============================================================
// Парсинг числа: принимает '.' и ',' как десятичный разделитель
// ============================================================
inline double parse_number(const std::string& s) {
    std::string cleaned = s;
    std::replace(cleaned.begin(), cleaned.end(), ',', '.');

    // Используем istringstream с локалью "C",
    // чтобы '.' всегда была десятичным разделителем
    std::istringstream iss(cleaned);
    iss.imbue(std::locale("C"));

    double val;
    iss >> val;

    if (iss.fail() || !iss.eof()) {
        throw std::invalid_argument("Не удалось распознать число: " + s);
    }

    return val;
}

// ============================================================
// Чтение значений из файла (разделённых пробельными символами)
// ============================================================
inline std::vector<std::string> read_values_from_file(const std::string& filename) {
    std::vector<std::string> values;
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Не удалось открыть файл: " + filename);
    }

    std::string token;
    while (file >> token) {
        values.push_back(token);
    }
    return values;
}

// ============================================================
// Запись строки в файл
// ============================================================
inline void write_to_file(const std::string& filename,
                          const std::string& content) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Не удалось открыть файл для записи: " + filename);
    }
    file << content;
}

// ============================================================
// Форматирование числа в двоичную строку заданной разрядности
// ============================================================
inline std::string to_bin_string(int64_t value, int bits) {
    std::string result(bits, '0');
    uint64_t uval = (uint64_t)value;
    for (int i = 0; i < bits; i++) {
        if (uval & (1ULL << (bits - 1 - i))) {
            result[i] = '1';
        }
    }
    return result;
}

// Парсит двоичную строку в диапазоне 6..64 бит (задаётся параметром bits).
// Формат:
//   - "0b..." или без префикса
//   - допускает '_' и пробелы
//   - допускает ведущие нули
// Возвращает значение как int64_t, интерпретируя результат как беззнаковое число.
static int64_t parse_fixed_bin(const std::string& s, int bits)
{
    // убираем '_' и пробелы
    std::string t;
    t.reserve(s.size());
    for (char c : s) {
        if (c == '_' || std::isspace((unsigned char)c)) continue;
        t.push_back(c);
    }
    if (t.empty()) throw std::invalid_argument("пустая строка");

    size_t pos = 0;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B')) pos = 2;
    if (pos >= t.size()) throw std::invalid_argument("бинарная строка не содержит цифр");

    uint64_t v = 0;
    int used = 0;
    for (; pos < t.size(); pos++) {
        char c = t[pos];
        if (c != '0' && c != '1') throw std::invalid_argument("небинарные цифры: " + s);
        v = (v << 1) | (uint64_t)(c - '0');
        used++;
        if (used > bits) throw std::out_of_range("слишком длинная строка: " + s);
    }

    return (int64_t)v;
}