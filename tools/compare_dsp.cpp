#include <cstdio>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>
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

#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106_DSP.h"
#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106_DSP_SetParam.h"
#include "/tmp/juno_ref/ultramaster/Source/DSP/KR106SysEx.h"

enum EParams
{
  kBenderDco = 0, kBenderVcf, kArpRate, kLfoRate, kLfoDelay,
  kDcoLfo, kDcoPwm, kDcoSub, kDcoNoise, kHpfFreq,
  kVcfFreq, kVcfRes, kVcfEnv, kVcfLfo, kVcfKbd,
  kVcaLevel, kEnvA, kEnvD, kEnvS, kEnvR,
  kTranspose, kHold, kArpeggio, kDcoPulse, kDcoSaw, kDcoSubSw,
  kChorusOff, kChorusI, kChorusII,
  kOctTranspose, kArpMode, kArpRange, kLfoMode, kPwmMode,
  kVcfEnvInv, kVcaMode,
  kBender, kTuning, kPower,
  kPortaMode, kPortaRate,
  kTransposeOffset, kBenderLfo,
  kAdsrMode,
  kMasterVol,
  kSettingVoices, kSettingOversample, kSettingIgnoreVel,
  kSettingArpLimitKbd, kSettingArpSync, kSettingLfoSync,
  kSettingMonoRetrig, kSettingMidiSysEx,
  kArpQuantize, kLfoQuantize,
  kSettingOscMode,
  kNumParams
};

static kr106::SysExDecoder makeSysExDecoder(bool j106)
{
    return {
        kLfoRate, kLfoDelay, kDcoLfo, kDcoPwm,
        kDcoNoise, kVcfFreq, kVcfRes, kVcfEnv,
        kVcfLfo, kVcfKbd, kVcaLevel, kEnvA,
        kEnvD, kEnvS, kEnvR, kDcoSub,
        kOctTranspose, kDcoPulse, kDcoSaw,
        kChorusOff, kChorusI, kChorusII,
        kPwmMode, kVcfEnvInv, kVcaMode, kHpfFreq,
        -1, j106
    };
}

int main() {
    float sr = 44100.0f;

    // 1. Setup FM-106 engine
    fm106_engine_init(&g_synth);
    fm106_load_preset(&g_synth, 53); // A76

    // 2. Setup KR106 DSP
    KR106DSP<float> ref(6);
    ref.Reset(sr, 256);
    ref.SetOversample(1); // 1x OS
    ref.ForEachVoice([](kr106::Voice<float>& voice) {
        for (int i = 0; i < kr106::Voice<float>::kNumVarianceParams; i++)
            voice.SetVariance(i, 0.f);
        voice.mOscMode = 1; // polyBLEP
    });
    ref.SetParam(kPower, 1.0);
    ref.SetParam(kAdsrMode, 1.0); // J106
    ref.mMasterVol = 0.25f;
    ref.SetParam(kVcaLevel, 0.5);
    ref.SetParam(kPortaMode, 1.0);
    ref.SetParam(kDcoSubSw, 1.0);
    ref.SetParam(kChorusOff, 1.0);
    ref.SetParam(kHpfFreq, 1.0); // FLAT

    uint8_t pat_bytes[18] = {
        0x7f, 0x00, 0x00, 0x00, 0x00, 0x38, 0x56, 0x06,
        0x00, 0x47, 0x4f, 0x00, 0x2f, 0x00, 0x32, 0x7f,
        0x1a, 0x1a
    };
    uint8_t sysex[22] = { 0x41, 0x31, 0x00, 0x01 };
    memcpy(sysex + 4, pat_bytes, 18);

    auto decoder = makeSysExDecoder(true);
    decoder.decode(sysex, 22, [&](int param, float val) {
        ref.SetParam(param, (double)val);
    });

    printf("Engines initialized. Triggering Note 60...\n");
    fm106_note_on(&g_synth, 60, 100);
    ref.NoteOn(60, 100);

    // Run 1000 samples and print voice 0 internals
    fm106_voice_t *v_fm = &g_synth.voice[0];
    auto *v_ref = ref.GetVoice(0);

    // Let us compare voice internals at sample 2000
    for (int s = 0; s < 2500; s++) {
        // Step FM-106 voice 0 manually
        int32_t env_q15 = fm106_adsr_process(&v_fm->adsr);
        uint32_t eff_inc = v_fm->base_inc;
        int32_t dco_s = fm106_dco_process(&v_fm->dco, eff_inc, g_synth.dco_pwm_depth, g_synth.dco_pwm_mode, 0, env_q15);
        int32_t post_osc = fm106_dc_block(dco_s, &v_fm->dc_post_osc);

        uint16_t env14_interp = (uint16_t)(env_q15 >> 1);
        uint16_t vcfCutoff = (uint16_t)(((uint32_t)g_synth.vcf_cutoff * 0x3F80u) / 127u);
        uint8_t vcfEnvMod = (uint8_t)(((uint32_t)g_synth.vcf_env_mod * 254u) / 127u);
        uint8_t vcfKeyTrack = (uint8_t)(((uint32_t)g_synth.vcf_kbd_track * 254u) / 127u);
        uint8_t envPol = (g_synth.vcf_env_polarity == 0) ? 1u : 0u;
        uint16_t pitch88 = (uint16_t)((uint32_t)v_fm->note << 8);

        uint16_t dac = fm106_calc_vcf_freq(
            vcfCutoff, 0, 0, vcfEnvMod, vcfKeyTrack,
            0, 0, envPol, env14_interp, pitch88);

        int32_t vcf_s = fm106_vcf_process(&v_fm->vcf, post_osc, dac, g_synth.vcf_resonance);
        int32_t post_vcf = fm106_dc_block(vcf_s, &v_fm->dc_post_vcf);

        uint32_t vca_idx = (uint32_t)env_q15 >> 7;
        if (vca_idx > 255u) vca_idx = 255u;
        int32_t vca_hw = FM106_VCA_HW[vca_idx];
        v_fm->vca_slew += ((vca_hw - v_fm->vca_slew) * 1064) >> 15;
        int32_t out_s = (post_vcf * v_fm->vca_slew) >> 15;

        // Step Ref by 1 sample
        float dummy_bus[2] = {0, 0};
        float *ptr[2] = { &dummy_bus[0], &dummy_bus[1] };
        ref.ProcessBlock(nullptr, ptr, 2, 1);

        if (s == 2000) {
            printf("\n--- Sample 2000 (~45ms after note-on) ---\n");
            printf("FM106: dco=%6d, post_osc=%6d, dac=%5d, vcf=%6d, vca_slew=%5d, out=%6d\n",
                   dco_s, post_osc, dac, vcf_s, v_fm->vca_slew, out_s);
            printf("Ref:   envInt=%5d, vcfDacNext=%5d, vcfCPS=%.5f, vcfSlew=%.5f, vcaSlew=%.5f, out=%.5f\n",
                   v_ref->mADSR.mEnvInt, v_ref->mVcfDacNext, v_ref->mVCF.mCacheG,
                   v_ref->mVcfSlew, v_ref->mVcaSlew, dummy_bus[0]);
        }
    }

    return 0;
}
