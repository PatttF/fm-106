#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>

#define DINGHY_DUAL_CORE 0
#define fm1_irq_off() (void)0
#define fm1_irq_on() (void)0
#define fm1_ticks() 0

#include "firmware/src/fm106_tables.h"
#include "firmware/src/fm106_vcf.h"

#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106VcfFreqJ106.h"
#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106VCF_OPTIMIZED.h"

int main() {
    printf("Comparing FM-106 VCF vs Reference VCF...\n");

    fm106_vcf_t vcf_fixed;
    fm106_vcf_init(&vcf_fixed);

    kr106::VCF vcf_ref;
    vcf_ref.SetSampleRate(44100.0f);
    vcf_ref.SetOversample(1); // 1x OS
    vcf_ref.mJ106Res = true;  // J106 resonance curve

    uint8_t res = 86; // A76 resonance
    uint16_t dac = 7168; // Note 60 cutoff with env decayed to 0

    // Compute frq (vcfCPS) for reference:
    float vcfHz = kr106::dacToHz(dac, 0.0f, 1.0f);
    float frq = vcfHz / 22050.0f; // CPS relative to Nyquist

    printf("DAC=%d -> Hz=%.2f -> frq=%.5f\n", dac, vcfHz, frq);

    // Feed a step or impulse or saw wave and compare output
    float max_diff = 0.0f;
    for (int s = 0; s < 1000; s++) {
        // Saw input at 261 Hz (C4)
        float saw_f = ((float)(s % 169) / 169.0f) * 2.0f - 1.0f;
        int32_t saw_q15 = (int32_t)(saw_f * 16384.0f); // ~0.5 amplitude

        int32_t out_fixed = fm106_vcf_process(&vcf_fixed, saw_q15, dac, res);
        float out_ref = vcf_ref.Process(saw_f * 0.5f, frq, res / 127.0f);

        float fixed_f = (float)out_fixed / 32768.0f;
        float diff = std::fabs(fixed_f - out_ref);
        if (diff > max_diff) max_diff = diff;

        if (s < 20 || s == 100 || s == 500) {
            printf("s=%3d: input=%6d | fixed=%6d (%.3f) | ref=%.3f | diff=%.4f\n",
                   s, saw_q15, out_fixed, fixed_f, out_ref, diff);
        }
    }
    printf("Max difference over 1000 samples: %.4f\n", max_diff);
    return 0;
}
