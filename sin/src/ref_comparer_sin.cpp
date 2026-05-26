#include "generic_ref_comparer.h"
#include "converter_sin.h"
#include "const_generator_sin.h"
#include "cordic_sin.h"

struct SinReferenceProvider : IReferenceProvider {
    static const char* app_name() {
        return "ref_comparer_sin.exe";
    }

    static const char* app_description() {
        return "Сравнение выходного значения из макроса IDE с эталоном CORDIC sin\n";
    }

    ReferenceTrace compute_reference(
        int64_t input_fixed,
        int bits,
        int iterations
    ) const override {
        double angle_rad = angle_fixed_to_rad(input_fixed, bits);
        double angle_deg = rad_to_deg(angle_rad);

        auto atan_table = generate_atan_table(bits, iterations);
        int64_t k_inv = generate_k_inv(bits, iterations);

        CordicResult ref = compute_sin(
            input_fixed,
            angle_deg,
            angle_rad,
            bits,
            atan_table,
            k_inv
        );

        ReferenceTrace trace;
        trace.result = ref.result_fixed;
        trace.x.reserve(ref.iterations.size());
        trace.y.reserve(ref.iterations.size());
        trace.z.reserve(ref.iterations.size());

        for (const auto& it : ref.iterations) {
            trace.x.push_back(it.cos_val);
            trace.y.push_back(it.sin_val);
            trace.z.push_back(it.z_val);
        }

        return trace;
    }
};

using RefComparerSinShell = GenericRefComparerShell<SinReferenceProvider>;

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    RefComparerSinShell shell;
    return shell.run(argc, argv);
}
