// ============================================================
//  Целочисленная арифметика для моделирования Минитеры
// ============================================================

// Арифметический сдвиг вправо
static inline int64_t asr(int64_t val, int shift) {
    return val >> shift;
}

// Усечение значения до bits бит с расширением знака.
// Моделирует поведение bits-разрядной арифметики в доп. коде.
static inline int64_t truncate(int64_t val, int bits) {
    // Маска младших bits бит
    uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
    val &= (int64_t)mask;
    // Расширение знака
    int64_t sign_bit = 1LL << (bits - 1);
    if (val & sign_bit) {
        val |= ~(int64_t)mask;
    }
    return val;
}

// Насышение 
static inline int64_t saturate(int64_t val, int bits) {
    int64_t max_val = (1LL << (bits - 1)) - 1;
    int64_t min_val = -(1LL << (bits - 1));
    return std::max(min_val, std::min(max_val, val));
}