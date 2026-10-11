#include <cstdio>
#include <cmath>
#include <cstdint>
#include <algorithm>

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

#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106Voice.h"

int main() {
    printf("Setting up Voice comparison for Preset A76 (index 53)...\n");

    // 1. FM-106 engine
    fm106_engine_init(&g_synth);
    fm106_load_preset(&g_synth, 53);

    // 2. Reference Voice
    kr106::Voice<float> ref_voice;
    ref_voice.SetSampleRate(44100.0f);
    ref_voice.mModel = kr106::kJ106;
    ref_voice.mADSR.mModel = kr106::kJ106;
    ref_voice.mVCF.mJ106Res = true;
    ref_voice.mOscMode = 1; // polyBLEP
    ref_voice.SetOversample(1); // 1x OS
    ref_voice.mOscBLEP.mPulseInvert = true;

    // Apply A76 parameters to ref_voice:
    // Sliders: [127, 0, 0, 0, 0, 56, 86, 6, 0, 71, 79, 0, 47, 0, 50, 127]
    // SW1: 26 (0x1A: 8', pulse=1, saw=1, chorus=II)
    // SW2: 26 (0x1A: pwm_m=0, vcf_inv=1, vca_m=0, hpf=0)
    ref_voice.mSawOn = true;
    ref_voice.mPulseOn = true;
    ref_voice.mSubOn = true;
    ref_voice.mDcoSub = kr106::Voice<float>::dcoSubLevel_j106(127.0f / 127.0f);
    ref_voice.mDcoNoise = 0.0f;
    ref_voice.mDcoPwm = 0.0f;
    ref_voice.mPwmMode = -1; // LFO PWM
    ref_voice.mDcoLfo = 0.0f;

    uint16_t cutoffInt = static_cast<uint16_t>((56.0f / 127.0f) * 0x3F80);
    ref_voice.mVcfCutoffInt = cutoffInt;
    ref_voice.mVcfFreqJ106 = kr106::dacToHz(cutoffInt);
    ref_voice.mVcfRes = 86.0f / 127.0f;
    ref_voice.mVcfEnv = 6.0f / 127.0f;
    ref_voice.mVcfEnvModInt = static_cast<uint8_t>((6.0f / 127.0f) * 254.0f);
    ref_voice.mVcfLfo = 0.0f;
    ref_voice.mVcfLfoDepthInt = 0;
    ref_voice.mVcfKbd = 71.0f / 127.0f;
    ref_voice.mVcfKeyTrackInt = static_cast<uint8_t>((71.0f / 127.0f) * 254.0f);
    ref_voice.mVcfEnvInvert = -1; // INVERTED!
    ref_voice.mVcaMode = 0; // ENV

    ref_voice.mADSR.Set106Attack(0.0f);
    ref_voice.mADSR.Set106Decay(47);
    ref_voice.mADSR.SetSustain(0.001f);
    ref_voice.mADSR.Set106Release(50);

    // Note 60 on
    fm106_note_on(&g_synth, 60, 100);
    ref_voice.mPitch = (60 - 69) / 12.0; // A440 = 69
    ref_voice.Trigger(1.0, false);

    // Render 1000 samples and compare
    printf("\nComparing first 40 samples:\n");
    float noiseBuf[1024] = {0};
    float mixBus[1024] = {0};

    for (int s = 0; s < 40; s++) {
        int32_t fm_s = 0;
        fm106_render_voice(&g_synth, 0, &fm_s, 1);

        mixBus[0] = 0.0f;
        ref_voice.ProcessBlock(mixBus, 1, 0.0f, 0.0f, noiseBuf, 0.0f);
        float ref_s = mixBus[0];

        printf("s=%2d | FM106=%7d (%.4f) | Ref=%.4f | diff=%.4f\n",
               s, fm_s, (float)fm_s / 32768.0f, ref_s, std::fabs((float)fm_s / 32768.0f - ref_s));
    }
    return 0;
}
