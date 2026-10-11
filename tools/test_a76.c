#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

int main(void) {
    printf("Initializing engine...\n");
    fm106_engine_init(&g_synth);
    
    // Find preset A76
    int a76_idx = -1;
    for (int i = 0; i < FM106_NUM_PRESETS; i++) {
        if (strstr(FM106_PRESETS[i].name, "A76")) {
            a76_idx = i;
            break;
        }
    }
    printf("Found A76 at index %d: %s\n", a76_idx, FM106_PRESETS[a76_idx].name);
    fm106_load_preset(&g_synth, a76_idx);

    printf("Cutoff: %d, Res: %d, EnvMod: %d, KeyTrack: %d, EnvPol: %d\n",
           g_synth.vcf_cutoff, g_synth.vcf_resonance, g_synth.vcf_env_mod,
           g_synth.vcf_kbd_track, g_synth.vcf_env_polarity);

    int32_t out[512];
    // Inspect voice 0 internals on note 60:
    printf("\n--- Voice 0 Internals on Note 60 ---\n");
    fm106_note_on(&g_synth, 60, 100);
    fm106_voice_t *v0 = &g_synth.voice[0];
    for (int s = 0; s < 50; s++) {
        // Step voice 0 for 1 sample
        int32_t sm = 0;
        fm106_render_voice(&g_synth, 0, &sm, 1);
        printf("s=%2d | gate=%d adsr=%d env_q15=%5d vca_slew=%5d | z3=%8d | out=%6d\n",
               s, v0->gate, v0->adsr.stage, v0->adsr.env14 << 1, v0->vca_slew,
               v0->vcf.z[3] >> 9, sm);
    }


    // Render test sequence matching test_presets.py
    // Open 16-bit stereo WAV file
    FILE *fp = fopen("/tmp/a76_fm106.wav", "wb");
    // WAV header (44-byte standard PCM)
    uint32_t sample_rate = 44100;
    uint32_t num_samples = 436590; // 9.9 seconds
    uint32_t data_bytes = num_samples * 2 * 2; // 16-bit stereo
    uint32_t file_bytes = data_bytes + 36;
    uint16_t num_channels = 2;
    uint16_t bits_per_sample = 16;
    uint32_t byte_rate = sample_rate * num_channels * bits_per_sample / 8;
    uint16_t block_align = num_channels * bits_per_sample / 8;
    uint16_t audio_format = 1;

    fwrite("RIFF", 1, 4, fp);
    fwrite(&file_bytes, 4, 1, fp);
    fwrite("WAVEfmt ", 1, 8, fp);
    uint32_t fmt_len = 16;
    fwrite(&fmt_len, 4, 1, fp);
    fwrite(&audio_format, 2, 1, fp);
    fwrite(&num_channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits_per_sample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&data_bytes, 4, 1, fp);

    // Track total frames rendered
    static uint32_t total_rendered_frames = 0;
    #undef RENDER_MS
    #define RENDER_MS(ms) do { \
        int frames = (int)((ms) * 44.1); \
        while (frames > 0) { \
            int chunk = frames > 256 ? 256 : frames; \
            fm106_audio_render(out, chunk); \
            int16_t s16[512]; \
            for (int k = 0; k < chunk * 2; k++) { \
                int32_t val = fm106_soft_limit(out[k]); \
                if (val > 32767) val = 32767; \
                if (val < -32768) val = -32768; \
                s16[k] = (int16_t)val; \
            } \
            fwrite(s16, 2, chunk * 2, fp); \
            total_rendered_frames += chunk; \
            frames -= chunk; \
        } \
    } while (0)

    // Sequence from test_presets.py:
    RENDER_MS(500);
    fm106_note_on(&g_synth, 48, 100);
    RENDER_MS(800);
    fm106_note_off(&g_synth, 48);
    RENDER_MS(600);
    fm106_note_on(&g_synth, 60, 100);
    RENDER_MS(800);
    fm106_note_off(&g_synth, 60);
    RENDER_MS(600);
    fm106_note_on(&g_synth, 48, 100);
    fm106_note_on(&g_synth, 55, 100);
    fm106_note_on(&g_synth, 60, 100);
    fm106_note_on(&g_synth, 64, 100);
    RENDER_MS(1000);
    fm106_note_off(&g_synth, 48);
    fm106_note_off(&g_synth, 55);
    fm106_note_off(&g_synth, 60);
    fm106_note_off(&g_synth, 64);
    RENDER_MS(4000);

    data_bytes = total_rendered_frames * 2 * 2;
    file_bytes = data_bytes + 36;
    fseek(fp, 4, SEEK_SET);
    fwrite(&file_bytes, 4, 1, fp);
    fseek(fp, 40, SEEK_SET);
    fwrite(&data_bytes, 4, 1, fp);
    fclose(fp);
    printf("Wrote /tmp/a76_fm106.wav successfully! (%u frames)\n", total_rendered_frames);


    return 0;
}
