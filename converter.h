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

// Вычисление pi через стандартную библиотеку
inline double PI() {
    return std::acos(-1.0);
}

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
// Градусы → радианы
// ============================================================
inline double deg_to_rad(double deg) {
    return deg * PI() / 180.0;
}

// ============================================================
// Радианы → градусы
// ============================================================
inline double rad_to_deg(double rad) {
    return rad * 180.0 / PI();
}

// ============================================================
// Угол в радианах → беззнаковый Q0.(bits) fixed-point
// Диапазон [0, 2π) отображается на [0, 2^{bits})
// При этом 2 бита отводится под квадрант, а (bits - 2) - под представление угла в диапазоне [0, pi/2)
// ============================================================
inline int64_t angle_rad_to_fixed(double angle_rad, int bits) {
    const double TWO_PI = 2.0 * PI();

    // Редукция в (-2π, 2π)
    // fmod гарантирует |результат| < |делитель|
    double reduced = fmod(angle_rad, TWO_PI);

    // Перевод отрицательных углов в эквивалентные положительные
    if (reduced < 0)
        reduced = TWO_PI + reduced;

    // Нормализация в [0, 1)
    double normalized = reduced / TWO_PI;

    // Масштабирование в Q0.(bits)
    double scale = (double)(1LL << bits);
    int64_t result = (int64_t)llround(normalized * scale);

    // Ограничение диапазона: [0, 2^{bits} - 1]
    int64_t max_val = (1LL << bits) - 1;
    int64_t min_val = 0;
    if (result > max_val) result = max_val;
    if (result < min_val) result = min_val;

    return result;
}

// ============================================================
// Q1.(bits-1) fixed-point результат sin/cos → double
// Здесь 1.0 = 2^(bits-1)
// ============================================================
inline double fixed_result_to_double(int64_t fixed_val, int bits) {
    double scale = (double)(1LL << (bits - 1));
    return fixed_val / scale;
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
// Возвращает значение как int64_t, интерпретируя результат как беззнаковый шаблон битов.
static int64_t parse_fixed_bin(const std::string& s, int bits)
{
    // убираем '_' и пробелы
    std::string t;
    t.reserve(s.size());
    for (char c : s) {
        if (c == '_' || std::isspace((unsigned char)c)) continue;
        t.push_back(c);
    }
    if (t.empty()) throw std::invalid_argument("empty binary string");

    size_t pos = 0;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'b' || t[1] == 'B')) pos = 2;
    if (pos >= t.size()) throw std::invalid_argument("binary string has no digits");

    uint64_t v = 0;
    int used = 0;
    for (; pos < t.size(); pos++) {
        char c = t[pos];
        if (c != '0' && c != '1') throw std::invalid_argument("non-binary digit in: " + s);
        v = (v << 1) | (uint64_t)(c - '0');
        used++;
        if (used > bits) throw std::out_of_range("binary value wider than 'bits' in: " + s);
    }

    return (int64_t)v;
}

// fixed (беззнаковый Q0.bits, диапазон [0, 2^bits)) -> угол в радианах [0, 2π)
inline double angle_fixed_to_rad(int64_t fixed, int bits) {
    const double TWO_PI = 2.0 * PI();

    // интерпретируем как беззнаковое значение bits бит
    uint64_t mask = (bits == 64) ? ~0ULL : ((1ULL << bits) - 1);
    uint64_t u = (uint64_t)fixed & mask;

    double scale = (double)(1ULL << bits);   // 2^bits
    double normalized = (double)u / scale;   // [0, 1)
    return normalized * TWO_PI;              // [0, 2π)
}