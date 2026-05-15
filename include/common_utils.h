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