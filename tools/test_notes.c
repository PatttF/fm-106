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
    fm106_engine_init(&g_synth);
    fm106_load_preset(&g_synth, 53); // A76

    printf("Triggering note 60...\n");
    fm106_note_on(&g_synth, 60, 100);
    int32_t out[512];
    for (int b = 0; b < 50; b++) { // ~300ms
        fm106_audio_render(out, 256);
    }
    printf("Releasing note 60...\n");
    fm106_note_off(&g_synth, 60);
    // Render 5 seconds of release!
    for (int sec = 1; sec <= 5; sec++) {
        int max_val = 0;
        for (int b = 0; b < 172; b++) { // 1 sec
            fm106_audio_render(out, 256);
            for (int k = 0; k < 512; k++) {
                if (abs(out[k]) > max_val) max_val = abs(out[k]);
            }
        }
        printf("Sec %d after release: max_val = %d\n", sec, max_val);
    }
    return 0;
}
