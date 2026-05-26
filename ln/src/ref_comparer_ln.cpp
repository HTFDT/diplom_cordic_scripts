#include "generic_ref_comparer.h"
#include "converter_ln.h"
#include "const_generator_ln.h"
#include "cordic_ln.h"

struct LnReferenceProvider : IReferenceProvider {
    static const char* app_name() {
        return "ref_comparer_ln.exe";
    }

    static const char* app_description() {
        return "Сравнение выходного значения из макроса IDE с эталоном CORDIC ln(1+x)\n";
    }

    ReferenceTrace compute_reference(
        int64_t input_fixed,
        int bits,
        int iterations
    ) const override {
        double x_double = ln_fixed_to_real(input_fixed, bits);

        auto schedule = generate_hyperbolic_schedule(iterations);
        auto atanh_table = generate_atanh_table(bits, schedule);

        LnResult ref = compute_ln(
            input_fixed,
            x_double,
            bits,
            schedule,
            atanh_table
        );

        ReferenceTrace trace;
        trace.result = ref.result_fixed;
        trace.x.reserve(ref.iterations.size());
        trace.y.reserve(ref.iterations.size());
        trace.z.reserve(ref.iterations.size());

        for (const auto& it : ref.iterations) {
            trace.x.push_back(it.x);
            trace.y.push_back(it.y);
            trace.z.push_back(it.z);
        }

        return trace;
    }
};

using RefComparerLnShell = GenericRefComparerShell<LnReferenceProvider>;

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "ru_RU.UTF-8");
    RefComparerLnShell shell;
    return shell.run(argc, argv);
}
