#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>

#define DINGHY_DUAL_CORE 0
#define fm1_irq_off() (void)0
#define fm1_irq_on() (void)0
#define fm1_ticks() 0

#include "firmware/src/fm106_tables.h"
#include "firmware/src/fm106_vcf.h"

#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106VcfFreqJ106.h"
#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106VCF_OPTIMIZED.h"

int main() {
    fm106_vcf_t vcf_fixed;
    fm106_vcf_init(&vcf_fixed);

    kr106::VCF vcf_ref;
    vcf_ref.SetSampleRate(44100.0f);
    vcf_ref.SetOversample(1);
    vcf_ref.mJ106Res = true;

    uint8_t res = 86;
    uint16_t dac = 7168; // Note 60 cutoff

    float vcfHz = kr106::dacToHz(dac, 0.0f, 1.0f);
    float frq = vcfHz / 22050.0f;

    // Generate 4096 samples of input: C4 saw + pulse (50%) + sub
    // Freq C4 = 261.625565 Hz
    double Fs = 44100.0;
    double f0 = 261.625565;
    double inc = f0 / Fs;

    int N = 8192;
    std::vector<int32_t> in_fixed(N);
    std::vector<float> in_ref(N);

    double pos = 0;
    bool sub_st = false;
    for (int i = 0; i < N; i++) {
        double prev = pos;
        pos += inc;
        if (pos >= 1.0) {
            pos -= 1.0;
            sub_st = !sub_st;
        }
        float saw = 2.0f * (float)pos - 1.0f;
        float pulse = (pos < 0.5) ? -1.0f : 1.0f;
        float sub = sub_st ? -1.0f : 1.0f;

        float tot = saw * 0.6f + pulse * 0.5f + sub * 0.75f;
        in_ref[i] = tot;
        in_fixed[i] = (int32_t)(tot * 16384.0f);
    }

    std::vector<double> out_fixed(N);
    std::vector<double> out_ref(N);

    for (int i = 0; i < N; i++) {
        int32_t sf = fm106_vcf_process(&vcf_fixed, in_fixed[i], dac, res);
        out_fixed[i] = (double)sf / 32768.0;

        float sr = vcf_ref.Process(in_ref[i], frq, res / 127.0f);
        out_ref[i] = (double)sr;
    }

    // Measure spectrum on second half (samples 4096..8191)
    int start = 4096;
    int len = 4096;

    std::vector<double> freqs = {65, 131, 261, 393, 523, 785, 1046, 1318, 1568, 2093, 2616, 3136};
    printf("Freq   | Fixed VCF  | Ref VCF\n");
    for (double f : freqs) {
        double w = 2.0 * M_PI * f / Fs;
        std::complex<double> acc_f = 0, acc_r = 0;
        for (int n = 0; n < len; n++) {
            std::complex<double> rot = std::polar(1.0, -w * n);
            acc_f += out_fixed[start + n] * rot;
            acc_r += out_ref[start + n] * rot;
        }
        printf("%5.0f Hz | %10.4f | %10.4f\n", f, std::abs(acc_f) / len, std::abs(acc_r) / len);
    }

    return 0;
}
