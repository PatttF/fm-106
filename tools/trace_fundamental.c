#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <complex.h>

#define DINGHY_DUAL_CORE 0
#define fm1_irq_off() (void)0
#define fm1_irq_on() (void)0
#define fm1_ticks() 0

#include "firmware/src/fm106_tables.h"
#include "firmware/src/fm106_osc.h"
#include "firmware/src/fm106_vcf.h"
#include "firmware/src/fm106_adsr.h"
#include "firmware/src/fm106_chorus.h"
#include "firmware/src/fm106_presets.h"
#include "firmware/src/fm106_engine.h"

int main() {
    fm106_engine_init(&g_synth);
    fm106_load_preset(&g_synth, 53); // A76

    printf("Tracing 261 Hz fundamental at each DSP stage on Note 60...\n");
    fm106_note_on(&g_synth, 60, 100);

    // Warm up 1000 samples
    int32_t out[512];
    for (int b = 0; b < 10; b++) {
        fm106_audio_render(out, 256);
    }

    // Capture 4096 samples at each stage
    fm106_voice_t *v = &g_synth.voice[0];
    double sum_dco = 0, sum_post_osc = 0, sum_vcf = 0, sum_post_vcf = 0;
    double sum_voice_out = 0, sum_hpf = 0, sum_chorus_l = 0, sum_chorus_r = 0;

    double complex acc_dco = 0, acc_post_osc = 0, acc_vcf = 0, acc_post_vcf = 0;
    double complex acc_voice_out = 0, acc_hpf = 0, acc_chorus_l = 0, acc_chorus_r = 0;

    double w0 = 2.0 * M_PI * 261.625565 / 44100.0;

    int N = 4096;
    for (int s = 0; s < N; s++) {
        double complex rot = cexp(-I * w0 * s);

        // Run engine per sample
        // 1. DCO
        int32_t env_q15 = fm106_adsr_process(&v->adsr);
        int32_t dco_s = fm106_dco_process(&v->dco, v->base_inc, g_synth.dco_pwm_depth, g_synth.dco_pwm_mode, 0, env_q15);
        acc_dco += (double)dco_s * rot;

        // 2. DC block post osc
        int32_t post_osc = fm106_dc_block(dco_s, &v->dc_post_osc);
        acc_post_osc += (double)post_osc * rot;

        // 3. VCF
        uint16_t env14_interp = (uint16_t)(env_q15 >> 1);
        uint16_t vcfCutoff = (uint16_t)(((uint32_t)g_synth.vcf_cutoff * 0x3F80u) / 127u);
        uint8_t vcfEnvMod = (uint8_t)(((uint32_t)g_synth.vcf_env_mod * 254u) / 127u);
        uint8_t vcfKeyTrack = (uint8_t)(((uint32_t)g_synth.vcf_kbd_track * 254u) / 127u);
        uint8_t envPol = (g_synth.vcf_env_polarity == 0) ? 1u : 0u;
        uint16_t pitch88 = (uint16_t)((uint32_t)v->note << 8);

        uint16_t dac = fm106_calc_vcf_freq(
            vcfCutoff, 0, 0, vcfEnvMod, vcfKeyTrack,
            0, 0, envPol, env14_interp, pitch88);
        int32_t vcf_s = fm106_vcf_process(&v->vcf, post_osc, dac, g_synth.vcf_resonance);
        acc_vcf += (double)vcf_s * rot;

        // 4. DC block post vcf
        int32_t post_vcf = fm106_dc_block(vcf_s, &v->dc_post_vcf);
        acc_post_vcf += (double)post_vcf * rot;

        // 5. VCA
        uint32_t vca_idx = (uint32_t)env_q15 >> 7;
        if (vca_idx > 255u) vca_idx = 255u;
        int32_t vca_hw = FM106_VCA_HW[vca_idx];
        v->vca_slew += ((vca_hw - v->vca_slew) * 1064) >> 15;
        int32_t out_s = (post_vcf * v->vca_slew) >> 15;
        acc_voice_out += (double)out_s * rot;

        // 6. HPF
        int32_t voice_sum = (int32_t)(((int64_t)out_s * 3LL) >> 5);
        int32_t hpf_out = fm106_hpf_process(&g_synth.hpf, voice_sum);
        int32_t vca_law = FM106_VCA_LAW[g_synth.vca_level > 127u ? 127u : g_synth.vca_level];
        int32_t vca_out = (int32_t)(((int64_t)hpf_out * vca_law) >> 12);
        acc_hpf += (double)vca_out * rot;

        // 7. Chorus
        int32_t sl, sr;
        fm106_chorus_process(&g_synth.chorus, vca_out, &sl, &sr);
        acc_chorus_l += (double)sl * rot;
        acc_chorus_r += (double)sr * rot;
    }

    printf("1. DCO raw:         %.1f\n", cabs(acc_dco) / N);
    printf("2. Post-osc DC:     %.1f\n", cabs(acc_post_osc) / N);
    printf("3. VCF out:         %.1f (dac=%d, res=%d)\n", cabs(acc_vcf) / N, g_synth.vcf_cutoff, g_synth.vcf_resonance);
    printf("4. Post-vcf DC:     %.1f\n", cabs(acc_post_vcf) / N);
    printf("5. Voice out:       %.1f (vca_slew=%d)\n", cabs(acc_voice_out) / N, v->vca_slew);
    printf("6. HPF + Level out: %.1f (hpf_mode=%d)\n", cabs(acc_hpf) / N, g_synth.hpf.mode);
    printf("7. Chorus L:        %.1f\n", cabs(acc_chorus_l) / N);
    printf("8. Chorus R:        %.1f\n", cabs(acc_chorus_r) / N);

    return 0;
}
