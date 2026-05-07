#include "converter.h"
#include "const_generator.h"
#include "cordic_sin.h"
#include "cli_common.h"
#include "json.h"

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#include <cstdint>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>

// ------------------------------------------------------------
// Битовые утилиты
// ------------------------------------------------------------

// Инвертирует порядок бит в байте
static inline uint8_t reverse_bits8(uint8_t b) {
    b = (uint8_t)((b & 0xF0) >> 4) | (uint8_t)((b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2) | (uint8_t)((b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1) | (uint8_t)((b & 0x55) << 1);
    return b;
}

// Извлекает бит под номером bit_index из потока байт
static inline int get_bit(const std::vector<uint8_t>& data, uint64_t bit_index) {
    uint64_t byte_idx = bit_index >> 3;
    uint64_t bit_in_byte = bit_index & 7ULL;        // 0..7
    if (byte_idx >= data.size()) 
        return 0;           // out of range -> 0
    uint8_t byte = data[(size_t)byte_idx];
    int shift = 7 - (int)bit_in_byte;                // MSB-first
    return (byte >> shift) & 1;
}

static inline std::string bits_to_string(const std::vector<int>& bits_msb_first) {
    std::string s;
    s.reserve(bits_msb_first.size());
    for (int b : bits_msb_first) 
        s.push_back(b ? '1' : '0');
    return s;
}

static inline std::string to_bin_fixed(int64_t v, int bits) {
    return to_bin_string((uint64_t)v & ((bits == 64) ? ~0ULL : ((1ULL << bits) - 1)), bits);
}

// ------------------------------------------------------------
// Структура кэша прочитанного файла
// ------------------------------------------------------------
struct FileBits {
    std::vector<uint8_t> bytes;
};

static FileBits load_file_bits(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f)
        throw std::runtime_error("Не удалось открыть файл: " + filename);

    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());

    for (auto &b : buf)
        b = reverse_bits8(b);

    return FileBits{std::move(buf)};
}

// ------------------------------------------------------------
// Извлекает одно слово из потока бит файла
// ------------------------------------------------------------
static int64_t extract_word_tc(
    const FileBits& fb,
    uint64_t offset_bits,
    int word_bits,
    bool has_start_bit)
{
    uint64_t data_start = offset_bits + (has_start_bit ? 1ULL : 0ULL);

    std::vector<int> bits_time_order;
    bits_time_order.reserve((size_t)word_bits);

    for (int i = 0; i < word_bits; i++) {
        int bit = get_bit(fb.bytes, data_start + (uint64_t)i);
        bits_time_order.push_back(bit);
    }

    // Интвертирует биты в извлеченном слове
    std::reverse(bits_time_order.begin(), bits_time_order.end());
    std::string word_msb = bits_to_string(bits_time_order);

    // парсит как беззнаковое
    int64_t u = parse_fixed_bin(word_msb, word_bits);
    // расширение знака
    int64_t s = truncate(u, word_bits);
    return s;
}

// ------------------------------------------------------------
// JSON
// ------------------------------------------------------------
static ordered_json read_json_file(const std::string& filename) {
    std::ifstream f(filename);
    if (!f) throw std::runtime_error("Не удалось открыть файл: " + filename);
    ordered_json j;
    f >> j;
    return j;
}

static std::string status_ok(bool ok) { return ok ? "OK" : "ERR"; }

struct CompareItem {
    bool ok;
    std::string ref_bin;
    std::string val_bin;
};

// ------------------------------------------------------------
// ref_comparer app
// ------------------------------------------------------------
struct RefCmpArgs : CommonArgs {
    std::string config_file;
    int iterations = 32;
};

struct RefCmpState : ReplState { };

struct RefComparer : AppShell<RefCmpArgs, RefCmpState> {
public:
    RefComparer()
        : AppShell("ref_comparer.exe",
                   "Сравнение результатов эмуляции IDE Минитеры с эталоном CORDIC\n")
    {
        app_.add_option("config", args_.config_file,
            "Путь к конфигурационному JSON файлу")
            ->required(false);

        app_.add_option("-n,--iterations", args_.iterations,
            "Кол-во итераций (по умолчанию: 32)")
            ->check(CLI::Range(4, 62));
    }

protected:
    bool is_batch() {
        return !args_.config_file.empty();
    }

    void load_repl_input(const std::string& line) {
        // В REPL пользователь вводит только путь к config'у
        args_.config_file = trim(line);
    }

    std::string compute() {
        if (args_.config_file.empty())
            throw std::invalid_argument("Не задан файл конфигурации.");

        ordered_json cfg = read_json_file(args_.config_file);

        int bits = args_.bits;
        if (cfg.contains("bits")) 
            bits = cfg["bits"].get<int>();
        if (bits < 6 || bits > 64) 
            throw std::invalid_argument("cfg.bits must be in [6..64]");

        bool has_start = true;
        if (cfg.contains("start-bit"))
            has_start = cfg["start-bit"].get<bool>();

        if (!cfg.contains("arg"))
            throw std::invalid_argument("cfg.arg is required");
        std::string arg_bin = cfg["arg"].get<std::string>();
        int64_t arg_fixed = parse_fixed_bin(arg_bin, bits);

        // Получаем кол-во итераций
        int iterations = args_.iterations;
        if (cfg.contains("iterations-count")) 
            bits = cfg["iterations-count"].get<int>();
        if (bits < 6 || bits > 64) 
            throw std::invalid_argument("cfg.iterations-count must be in [6..64]");

        auto atan_table = generate_atan_table(bits, iterations);
        int64_t k_inv = generate_k_inv(bits, iterations);

        double angle_rad = angle_fixed_to_rad(arg_fixed, bits);
        double angle_deg = rad_to_deg(angle_rad);

        CordicResult ref = compute_sin(arg_fixed, angle_deg, angle_rad, bits, atan_table, k_inv);

        std::unordered_map<std::string, FileBits> cache;

        auto get_file = [&](const std::string& fn) -> const FileBits& {
            auto it = cache.find(fn);
            if (it != cache.end()) return it->second;
            auto fb = load_file_bits(fn);
            auto [ins_it, ok] = cache.emplace(fn, std::move(fb));
            return ins_it->second;
        };

        // Сравнение по итерациям
        ordered_json out;
        out["iterations"] = ordered_json::object();

        if (!cfg.contains("iterations") || !cfg["iterations"].is_object())
            throw std::invalid_argument("cfg.iterations must be an object");

        auto mk_item = [&](int64_t ref_val, int64_t val) -> CompareItem {
            int64_t ref_t = truncate(ref_val, bits);
            int64_t val_t = truncate(val, bits);
            std::string ref_bin_s = to_bin_string(ref_t, bits);
            std::string val_bin_s = to_bin_string(val_t, bits);
            return CompareItem{ref_bin_s == val_bin_s, ref_bin_s, val_bin_s};
        };

        for (auto it = cfg["iterations"].begin(); it != cfg["iterations"].end(); ++it) {
            const std::string iter_key = it.key();
            int iter = std::stoi(iter_key);

            if (iter < 0 || iter >= (int)ref.iterations.size()) {
                throw std::invalid_argument("Итерация " + iter_key + " находится за пределом списка итераций эталона");
            }

            const auto& ref_iter = ref.iterations[(size_t)iter];

            ordered_json iter_obj = ordered_json::object();
            bool iter_ok = true;

            uint64_t default_offset = 0;
            bool has_default_offset = false;
            if (it.value().contains("offset")) {
                default_offset = it.value()["offset"].get<uint64_t>();
                has_default_offset = true;
            }

            auto read_comp = [&](const char* name, int64_t ref_val) -> CompareItem {
                if (!it.value().contains(name))
                    throw std::invalid_argument("cfg.iterations[" + iter_key + "] missing component: " + std::string(name));
                const json& comp = it.value()[name];
                if (!comp.contains("file"))
                    throw std::invalid_argument("cfg.iterations[" + iter_key + "]." + name + " must have file");

                uint64_t offset;
                if (comp.contains("offset")) {
                    offset = comp["offset"].get<uint64_t>();
                } else if (has_default_offset) {
                    offset = default_offset;
                } else {
                    throw std::invalid_argument("missing offset on iteration " + iter_key + "." + name);
                }
                std::string fn = comp["file"].get<std::string>();
                const FileBits& fb = get_file(fn);

                int64_t val = extract_word_tc(fb, offset, bits, has_start);
                return mk_item(ref_val, val);
            };

            // Compare cos/sin/z
            auto ci = read_comp("cos", ref_iter.cos_val);
            auto si = read_comp("sin", ref_iter.sin_val);
            auto zi = read_comp("z", ref_iter.z_val);

            iter_ok = ci.ok && si.ok && zi.ok;

            iter_obj["cos"] = {
                {"status", status_ok(ci.ok)},
                {"reference", ci.ref_bin},
                {"value", ci.val_bin}
            };
            iter_obj["sin"] = {
                {"status", status_ok(si.ok)},
                {"reference", si.ref_bin},
                {"value", si.val_bin}
            };
            iter_obj["z"] = {
                {"status", status_ok(zi.ok)},
                {"reference", zi.ref_bin},
                {"value", zi.val_bin}
            };
            iter_obj["status"] = status_ok(iter_ok);

            out["iterations"][iter_key] = iter_obj;
        }

        // Сравнение результата
        if (!cfg.contains("result") || !cfg["result"].is_object())
            throw std::invalid_argument("cfg.result must exist and be an object with offset+file");

        uint64_t res_offset = cfg["result"].value("offset", 0ULL);
        std::string res_file = cfg["result"].value("file", "");
        if (res_file.empty())
            throw std::invalid_argument("cfg.result.file is required");

        int64_t res_val = extract_word_tc(get_file(res_file), res_offset, bits, has_start);
        auto ri = mk_item(ref.result_fixed, res_val);

        out["result"] = {
            {"status", status_ok(ri.ok)},
            {"reference", ri.ref_bin},
            {"value", ri.val_bin}
        };

        return out.dump(2) + "\n";
    }

    std::string format_text(const std::string& json_result) {
        return json_result;
    }

    void print_repl_status() {
        std::cout
            << "  bits (CLI default): " << args_.bits << "\n"
            << "  iterations (CLI default): " << args_.iterations << "\n"
            << "  out: " << (args_.out_file.empty() ? "console" : args_.out_file) << "\n";
    }

    bool handle_repl_command(const std::string& cmd, const std::string& arg) {
        bool handled = AppShell<RefCmpArgs, RefCmpState>::handle_repl_command(cmd, arg);

        if (handled)
            return true;

        if (cmd == "iter") {
            if (arg.empty()) {
                std::cout << "  Кол-во итераций: "<< args_.iterations << "\n";
            } else {
                try {
                    int iter = std::stoi(arg);
                    if (iter < 4 || iter > 62) {
                        std::cout << "  Ошибка: кол-во итераций должно быть в диапазоне [4, 62]\n";
                    } else {
                        args_.iterations = iter;
                        std::cout << "  Кол-во итераций: "
                                  << args_.iterations << "\n";
                    }
                } catch (...) {
                    std::cout << "  Ошибка: некорректное значение: " << arg << "\n";
                }
            }
        }

        return false;
    }

    void print_repl_help() {
        std::cout
            << "Команды:\n"
            << "  :bits N         bits по умолчанию (может быть переопределён cfg.bits)\n"
            << "  :iterations N   кол-во итераций по умолчанию (может быть переопределён cfg.iterations-count)\n"
            << "  :out FILE       записать результат в файл\n"
            << "  :out -          вывод в консоль\n"
            << "  :status         показать настройки\n"
            << "  :help           справка\n"
            << "  quit            выход\n\n"
            << "Ввод:\n"
            << "  Введите путь к JSON конфигу (одной строкой).\n";
    }
};

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    RefComparer app;
    return app.run(argc, argv);
}
