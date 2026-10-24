/* SPDX-License-Identifier: GPL-3.0-only */
/* FM-106: Roland Juno-106 System Exclusive (SysEx) Librarian & Real-Time Protocol */
#pragma once
#include <stdint.h>

#define FM106_PRESETS_FLASH_ADDR 0x00098000u
#define FM106_PRESETS_FLASH_SIZE 0x4000u    /* 16 KiB (4 x 4 KiB sectors) */
#define FM106_PRESETS_MAGIC      0x36303142u /* "J10B" v1 user banks (preset p[44], FX 1 only) */
#define FM106_PRESETS_MAGIC2     0x36303143u /* "J10C" v2 user banks (preset p[60], FX 1..4) */
#define FM106_PRESETS_MAGIC3     0x36303144u /* "J10D" v3 user banks (preset p[66], FX 1..4 + SEQ) */

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t crc;
    uint32_t reserved;
} fm106_flash_header_t;

/* Global RAM user bank (128 presets = 10,496 bytes: 64 in User A, 64 in User B) */
extern fm106_preset_t g_user_presets[FM106_NUM_USER_PRESETS];
extern fm106_engine_t g_synth;
extern uint32_t g_dirty_screen;

static inline uint32_t fm106_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

/* Reset user presets in RAM back to completely blank */
static void fm106_user_presets_restore_factory(void)
{
    memset(g_user_presets, 0, sizeof(g_user_presets));
}

static uint8_t g_presets_flash_buf[sizeof(fm106_flash_header_t) + sizeof(g_user_presets)];

/* Initialize user presets: load from SPI flash if valid, else initialize as completely blank.
 * v1 banks ("J10B", p[44]) and v2 banks ("J10C", p[60]) still load: widened in place. */
static void fm106_user_presets_init(void)
{
    fm106_flash_header_t hdr;

    if (presets_flash_read(&hdr, sizeof(hdr)) == 0 &&
        hdr.count == FM106_NUM_USER_PRESETS &&
        (hdr.magic == FM106_PRESETS_MAGIC3 || hdr.magic == FM106_PRESETS_MAGIC2 || hdr.magic == FM106_PRESETS_MAGIC)) {
        if (hdr.magic == FM106_PRESETS_MAGIC3) {
            if (presets_flash_read(g_presets_flash_buf, sizeof(g_presets_flash_buf)) == 0 &&
                fm106_crc32(g_presets_flash_buf + sizeof(hdr), sizeof(g_user_presets)) == hdr.crc) {
                memcpy(g_user_presets, g_presets_flash_buf + sizeof(hdr), sizeof(g_user_presets));
                return;
            }
        } else if (hdr.magic == FM106_PRESETS_MAGIC2) {
            /* v2: 128 records of {name[16], p[60]} = 76 B each */
            const uint32_t v2_size = FM106_NUM_USER_PRESETS * 76u;
            if (presets_flash_read(g_presets_flash_buf, sizeof(fm106_flash_header_t) + v2_size) == 0 &&
                fm106_crc32(g_presets_flash_buf + sizeof(hdr), v2_size) == hdr.crc) {
                const uint8_t *src = g_presets_flash_buf + sizeof(hdr);
                for (uint32_t i = FM106_NUM_USER_PRESETS; i-- > 0; ) {
                    /* descending: each widened record sits above the old record it came from */
                    memcpy(g_user_presets[i].name, src + i * 76u, 16);
                    memset(g_user_presets[i].p, 0, sizeof(g_user_presets[i].p));
                    memcpy(g_user_presets[i].p, src + i * 76u + 16, 60);
                }
                return;
            }
        } else {
            /* v1: 128 records of {name[16], p[44]} = 60 B each */
            const uint32_t oldn = FM106_NUM_USER_PRESETS * 60u;
            if (presets_flash_read(g_presets_flash_buf, sizeof(fm106_flash_header_t) + oldn) == 0 &&
                fm106_crc32(g_presets_flash_buf + sizeof(hdr), oldn) == hdr.crc) {
                const uint8_t *src = g_presets_flash_buf + sizeof(hdr);
                for (uint32_t i = FM106_NUM_USER_PRESETS; i-- > 0; ) {
                    /* descending: each widened record sits above the old record it came from */
                    memcpy(g_user_presets[i].name, src + i * 60u, 16);
                    memset(g_user_presets[i].p, 0, sizeof(g_user_presets[i].p));
                    memcpy(g_user_presets[i].p, src + i * 60u + 16, 44);
                }
                return;
            }
        }
    }

    /* Fallback: initialize user presets as completely blank */
    memset(g_user_presets, 0, sizeof(g_user_presets));
}

/* Burn user presets RAM bank into SPI flash */
static int fm106_user_presets_save_flash(void)
{
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif
    fm106_flash_header_t *hdr = (fm106_flash_header_t *)g_presets_flash_buf;

    hdr->magic = FM106_PRESETS_MAGIC3;
    hdr->count = FM106_NUM_USER_PRESETS;
    hdr->crc = fm106_crc32((const uint8_t *)g_user_presets, sizeof(g_user_presets));
    hdr->reserved = 0;

    memcpy(g_presets_flash_buf + sizeof(fm106_flash_header_t), g_user_presets, sizeof(g_user_presets));
    return presets_flash_save(g_presets_flash_buf, sizeof(g_presets_flash_buf));
}

/* Load a preset from the 4 banks (Factory A, Factory B, User A, User B) into the synth engine */
static void fm106_load_user_preset(fm106_engine_t *e, uint32_t idx)
{
    fm1_irq_off();
#if DINGHY_DUAL_CORE
    if (audio_worker_online) audio_worker_join();
#endif
    if (idx >= FM106_TOTAL_PRESETS) idx = 0;
    e->patch_idx = (uint8_t)idx;

    if (idx >= 128u && g_user_presets[idx - 128u].name[0] == '\0') {
        /* Blank user preset: load clean default initialized sound */
        e->patch_name[0] = '\0';
        e->bender_dco = 0; e->bender_vcf = 0;
        e->arp_rate = 120; e->lfo_rate = 60; e->lfo_delay = 0;
        e->dco_lfo_depth = 0; e->dco_pwm_depth = 0; e->dco_sub_level = 0; e->dco_noise_level = 0;
        e->hpf.mode = 1;
        e->vcf_cutoff = 100; e->vcf_resonance = 0; e->vcf_env_mod = 0; e->vcf_lfo_mod = 0;
        e->vcf_kbd_track = 0; e->vca_level = 100;
        e->env_a = 0; e->env_d = 64; e->env_s = 127; e->env_r = 20;
        e->dco_pulse_on = 0; e->dco_saw_on = 1; e->dco_sub_on = 0;
        e->chorus.mode = FM106_CHORUS_OFF;
        e->dco_range = 1; e->arp_mode = 0; e->arp_range = 1;
        e->dco_pwm_mode = 0; e->vcf_env_polarity = 0; e->vca_mode = 0;
        for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
            fm106_adsr_params(&e->voice[i].adsr, e->env_a, e->env_d, e->env_s, e->env_r);
            e->voice[i].dco.saw_on = 1; e->voice[i].dco.pulse_on = 0; e->voice[i].dco.sub_on = 0;
            e->voice[i].dco.sub_level = 0; e->voice[i].dco.noise_level = 0;
            if (e->voice[i].note != 255u) e->voice[i].base_inc = fm106_calc_inc(e->voice[i].note, 0, e->dco_range);
        }
        fm106_param_snapshot(e);
        fm1_irq_on();
        return;
    }

    const fm106_preset_t *p = (idx < 128u) ? &FM106_PRESETS[idx] : &g_user_presets[idx - 128u];

    uint32_t pi = 0;
    while (p->name[pi] && pi < sizeof(e->patch_name) - 1u) {
        e->patch_name[pi] = p->name[pi];
        pi++;
    }
    e->patch_name[pi] = '\0';

    e->bender_dco     = p->p[0];
    e->bender_vcf     = p->p[1];
    e->arp_rate       = p->p[2];
    e->lfo_rate       = p->p[3];
    e->lfo_delay      = p->p[4];
    e->dco_lfo_depth  = p->p[5];
    e->dco_pwm_depth  = p->p[6];
    e->dco_sub_level  = p->p[7];
    e->dco_noise_level= p->p[8];
    e->hpf.mode       = p->p[9] > 3 ? 1 : p->p[9];
    e->vcf_cutoff     = p->p[10];
    e->vcf_resonance  = p->p[11];
    e->vcf_env_mod    = p->p[12];
    e->vcf_lfo_mod    = p->p[13];
    e->vcf_kbd_track  = p->p[14];
    e->vca_level      = p->p[15];
    e->env_a          = p->p[16];
    e->env_d          = p->p[17];
    e->env_s          = p->p[18];
    e->env_r          = p->p[19];

    e->dco_pulse_on   = p->p[23];
    e->dco_saw_on     = p->p[24];
    e->dco_sub_on     = p->p[25];

    if (p->p[27] && p->p[28]) e->chorus.mode = FM106_CHORUS_I_II;
    else if (p->p[28]) e->chorus.mode = FM106_CHORUS_II;
    else if (p->p[27]) e->chorus.mode = FM106_CHORUS_I;
    else e->chorus.mode = FM106_CHORUS_OFF;

    e->dco_range        = p->p[29] > 2 ? 1 : p->p[29];
    e->arp_mode         = p->p[30] > 2 ? 0 : p->p[30];
    e->arp_range        = p->p[31] ? p->p[31] : 1;
    e->dco_pwm_mode     = p->p[33];
    e->vcf_env_polarity = p->p[34];
    e->vca_mode         = p->p[35];

    e->fx[0].type      = p->p[36] >= FM106_FX_COUNT ? 0 : p->p[36];
    e->fx[0].mix       = p->p[37] > 127 ? 127 : p->p[37];
    e->fx[0].rate      = p->p[38] > 127 ? 127 : p->p[38];
    e->fx[0].character = p->p[39] > 127 ? 127 : p->p[39];

    /* Voice allocation mode (0 = Mono, 1 = Poly I, 2 = Poly II) */
    if (p->p[45] <= 2u && p->p[45] != 0u) {
        fm106_set_voice_mode(e, p->p[45]);
    } else {
        fm106_set_voice_mode(e, FM106_MODE_POLY1);
    }

    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        fm106_adsr_params(&e->voice[i].adsr, e->env_a, e->env_d, e->env_s, e->env_r);
        e->voice[i].dco.saw_on = e->dco_saw_on;
        e->voice[i].dco.pulse_on = e->dco_pulse_on;
        e->voice[i].dco.sub_on = e->dco_sub_on;
        e->voice[i].dco.sub_level = e->dco_sub_level;
        e->voice[i].dco.noise_level = e->dco_noise_level;
        if (e->voice[i].note != 255u) {
            e->voice[i].base_inc = (e->voice_mode == FM106_MODE_MONO)
                ? fm106_calc_unison_inc(e->voice[i].note, 0, e->dco_range, i)
                : fm106_calc_inc(e->voice[i].note, 0, e->dco_range);
        }
    }
    fm106_param_snapshot(e); /* p-lock cache follows the loaded sound */
    fm1_irq_on();
}

/* Save the current synth engine state into a preset struct */
static void fm106_save_engine_to_preset(fm106_preset_t *p, const fm106_engine_t *e, const char *name)
{
    memset(p, 0, sizeof(*p));
    uint32_t len = 0;
    while (name && name[len] && len < sizeof(p->name) - 1u) {
        p->name[len] = name[len];
        len++;
    }
    p->name[len] = '\0';

    p->p[0]  = e->bender_dco;
    p->p[1]  = e->bender_vcf;
    p->p[2]  = e->arp_rate;
    p->p[3]  = e->lfo_rate;
    p->p[4]  = e->lfo_delay;
    p->p[5]  = e->dco_lfo_depth;
    p->p[6]  = e->dco_pwm_depth;
    p->p[7]  = e->dco_sub_level;
    p->p[8]  = e->dco_noise_level;
    p->p[9]  = e->hpf.mode & 3u;
    p->p[10] = e->vcf_cutoff;
    p->p[11] = e->vcf_resonance;
    p->p[12] = e->vcf_env_mod;
    p->p[13] = e->vcf_lfo_mod;
    p->p[14] = e->vcf_kbd_track;
    p->p[15] = e->vca_level;
    p->p[16] = e->env_a;
    p->p[17] = e->env_d;
    p->p[18] = e->env_s;
    p->p[19] = e->env_r;

    p->p[23] = e->dco_pulse_on & 1u;
    p->p[24] = e->dco_saw_on & 1u;
    p->p[25] = e->dco_sub_on & 1u;

    p->p[27] = (e->chorus.mode == FM106_CHORUS_I || e->chorus.mode == FM106_CHORUS_I_II) ? 1u : 0u;
    p->p[28] = (e->chorus.mode == FM106_CHORUS_II || e->chorus.mode == FM106_CHORUS_I_II) ? 1u : 0u;

    p->p[29] = e->dco_range & 3u;
    p->p[30] = e->arp_mode & 3u;
    p->p[31] = e->arp_range ? e->arp_range : 1u;
    p->p[33] = e->dco_pwm_mode & 1u;
    p->p[34] = e->vcf_env_polarity & 1u;
    p->p[35] = e->vca_mode & 1u;

    p->p[36] = e->fx[0].type < FM106_FX_COUNT ? e->fx[0].type : 0u;
    p->p[37] = (uint8_t)e->fx[0].mix;
    p->p[38] = (uint8_t)e->fx[0].rate;
    p->p[39] = (uint8_t)e->fx[0].character;

    p->p[45] = e->voice_mode;

    /* SEQ track params (bpm/sdiv/slen/sgate/swing) */
    p->p[40] = (uint8_t)e->seq.bpm;
    p->p[41] = (uint8_t)e->seq.sdiv;
    p->p[42] = (uint8_t)(e->seq.slen & 0xFFu);
    p->p[65] = (uint8_t)(e->seq.slen >> 8);
    p->p[43] = (uint8_t)e->seq.sgate;
    p->p[44] = (uint8_t)e->seq.swing;
}

/* Decode Switch Byte 1 (SW1): Octave, Pulse, Saw, Chorus */
static inline void fm106_decode_sw1(uint8_t sw1, uint8_t *oct, uint8_t *pulse, uint8_t *saw, uint8_t *chorus)
{
    *oct = (sw1 & 4u) ? 2u : (sw1 & 2u) ? 1u : 0u;
    *pulse = (sw1 & 8u) ? 1u : 0u;
    *saw = (sw1 & 16u) ? 1u : 0u;
    if (sw1 & 32u) {
        *chorus = FM106_CHORUS_OFF;
    } else if (sw1 & 64u) {
        *chorus = FM106_CHORUS_I;
    } else {
        *chorus = FM106_CHORUS_II;
    }
}

/* Encode Switch Byte 1 (SW1) */
static inline uint8_t fm106_encode_sw1(uint8_t oct, uint8_t pulse, uint8_t saw, uint8_t chorus)
{
    uint8_t sw1 = (oct == 2u) ? 4u : (oct == 1u) ? 2u : 1u;
    if (pulse) sw1 |= 8u;
    if (saw) sw1 |= 16u;
    if (chorus == FM106_CHORUS_OFF) sw1 |= 32u;
    else if (chorus == FM106_CHORUS_I) sw1 |= 64u;
    else if (chorus == FM106_CHORUS_I_II) sw1 |= 64u;
    return sw1;
}

/* Decode Switch Byte 2 (SW2): PWM Mode, VCF Env Inv, VCA Mode, HPF */
static inline void fm106_decode_sw2(uint8_t sw2, uint8_t *pwm_mode, uint8_t *vcf_inv, uint8_t *vca_mode, uint8_t *hpf)
{
    *pwm_mode = (sw2 & 1u) ? 1u : 0u;
    *vcf_inv = (sw2 & 2u) ? 1u : 0u;
    *vca_mode = (sw2 & 4u) ? 1u : 0u;
    *hpf = (3u - ((sw2 >> 3) & 3u)) & 3u;
}

/* Encode Switch Byte 2 (SW2) */
static inline uint8_t fm106_encode_sw2(uint8_t pwm_mode, uint8_t vcf_inv, uint8_t vca_mode, uint8_t hpf)
{
    uint8_t sw2 = 0;
    if (pwm_mode) sw2 |= 1u;
    if (vcf_inv) sw2 |= 2u;
    if (vca_mode) sw2 |= 4u;
    sw2 |= (((3u - (hpf & 3u)) & 3u) << 3);
    return sw2;
}

/* Apply 18 APR parameters directly to live engine edit buffer */
static void fm106_apply_apr_to_engine(fm106_engine_t *e, const uint8_t *s, uint8_t sw1, uint8_t sw2)
{
    e->lfo_rate        = s[0] & 0x7Fu;
    e->lfo_delay       = s[1] & 0x7Fu;
    e->dco_lfo_depth   = s[2] & 0x7Fu;
    e->dco_pwm_depth   = s[3] & 0x7Fu;
    e->dco_noise_level = s[4] & 0x7Fu;
    e->vcf_cutoff      = s[5] & 0x7Fu;
    e->vcf_resonance   = s[6] & 0x7Fu;
    e->vcf_env_mod     = s[7] & 0x7Fu;
    e->vcf_lfo_mod     = s[8] & 0x7Fu;
    e->vcf_kbd_track   = s[9] & 0x7Fu;
    e->vca_level       = s[10] & 0x7Fu;
    e->env_a           = s[11] & 0x7Fu;
    e->env_d           = s[12] & 0x7Fu;
    e->env_s           = s[13] & 0x7Fu;
    e->env_r           = s[14] & 0x7Fu;
    e->dco_sub_level   = s[15] & 0x7Fu;
    e->dco_sub_on      = (e->dco_sub_level > 0);

    uint8_t oct, pulse, saw, chorus;
    fm106_decode_sw1(sw1, &oct, &pulse, &saw, &chorus);
    e->dco_range    = oct;
    e->dco_pulse_on = pulse;
    e->dco_saw_on   = saw;
    e->chorus.mode  = chorus;

    uint8_t pwm_m, vcf_inv, vca_m, hpf_m;
    fm106_decode_sw2(sw2, &pwm_m, &vcf_inv, &vca_m, &hpf_m);
    e->dco_pwm_mode     = pwm_m;
    e->vcf_env_polarity = vcf_inv;
    e->vca_mode         = vca_m;
    e->hpf.mode         = hpf_m;

    /* Update voices */
    for (uint32_t i = 0; i < FM106_NUM_VOICES; i++) {
        fm106_adsr_params(&e->voice[i].adsr, e->env_a, e->env_d, e->env_s, e->env_r);
        e->voice[i].dco.saw_on      = e->dco_saw_on;
        e->voice[i].dco.pulse_on    = e->dco_pulse_on;
        e->voice[i].dco.sub_on      = e->dco_sub_on;
        e->voice[i].dco.sub_level   = e->dco_sub_level;
        e->voice[i].dco.noise_level = e->dco_noise_level;
        if (e->voice[i].note != 255u) {
            e->voice[i].base_inc = fm106_calc_inc(e->voice[i].note, 0, e->dco_range);
        }
    }
}

/* Store 18 APR parameters into a preset slot */
static void fm106_store_apr_to_preset(fm106_preset_t *p, const uint8_t *s, uint8_t sw1, uint8_t sw2, const char *name, int is_vintage)
{
    p->name[0] = '\0';
    p->p[0]  = 64;           /* bender_dco */
    p->p[1]  = 64;           /* bender_vcf */
    p->p[2]  = 64;           /* arp_rate */
    p->p[3]  = s[0] & 0x7Fu; /* lfo_rate */
    p->p[4]  = s[1] & 0x7Fu; /* lfo_delay */
    p->p[5]  = s[2] & 0x7Fu; /* dco_lfo_depth */
    p->p[6]  = s[3] & 0x7Fu; /* dco_pwm_depth */
    p->p[7]  = s[15] & 0x7Fu;/* dco_sub_level */
    p->p[8]  = s[4] & 0x7Fu; /* dco_noise_level */

    uint8_t pwm_m, vcf_inv, vca_m, hpf_m;
    fm106_decode_sw2(sw2, &pwm_m, &vcf_inv, &vca_m, &hpf_m);
    p->p[9]  = hpf_m;
    p->p[10] = s[5] & 0x7Fu; /* vcf_cutoff */
    p->p[11] = s[6] & 0x7Fu; /* vcf_resonance */
    p->p[12] = s[7] & 0x7Fu; /* vcf_env_mod */
    p->p[13] = s[8] & 0x7Fu; /* vcf_lfo_mod */
    p->p[14] = s[9] & 0x7Fu; /* vcf_kbd_track */
    p->p[15] = s[10] & 0x7Fu;/* vca_level */
    p->p[16] = s[11] & 0x7Fu;/* env_a */
    p->p[17] = s[12] & 0x7Fu;/* env_d */
    p->p[18] = s[13] & 0x7Fu;/* env_s */
    p->p[19] = s[14] & 0x7Fu;/* env_r */

    uint8_t oct, pulse, saw, chorus;
    fm106_decode_sw1(sw1, &oct, &pulse, &saw, &chorus);
    p->p[23] = pulse;
    p->p[24] = saw;
    p->p[25] = (s[15] > 0);
    p->p[27] = (chorus == FM106_CHORUS_I || chorus == FM106_CHORUS_I_II);
    p->p[28] = (chorus == FM106_CHORUS_II || chorus == FM106_CHORUS_I_II);
    p->p[29] = oct;
    p->p[30] = 0;            /* arp_mode */
    p->p[31] = 1;            /* arp_range */
    p->p[33] = pwm_m;
    p->p[34] = vcf_inv;
    p->p[35] = vca_m;

    if (name && name[0]) {
        while (*name == ' ') name++;
        uint32_t len = 0;
        while (name[len] && len < 15u) {
            ((char *)p->name)[len] = name[len];
            len++;
        }
        while (len > 0 && ((char *)p->name)[len - 1] == ' ') len--;
        ((char *)p->name)[len] = '\0';
    }
    if (is_vintage && p->name[0] == '\0') {
        const char *def_nm = "User Patch";
        uint32_t di = 0;
        while (def_nm[di] && di < 15u) { ((char *)p->name)[di] = def_nm[di]; di++; }
        ((char *)p->name)[di] = '\0';
    }
}

/* Transmit a patch dump APR SysEx message over USB-MIDI */
static void fm106_sysex_dump_patch(uint8_t patch_idx)
{
    const fm106_preset_t *p = (patch_idx < 128u) ? &FM106_PRESETS[patch_idx] : &g_user_presets[patch_idx - 128u];

    uint8_t bank_opt = (patch_idx / 64u) + 1u; /* 1=Fact A, 2=Fact B, 3=User A, 4=User B */
    uint8_t ch = (g_synth.midi_channel - 1u) & 0x0Fu;
    uint8_t ch_byte = ((bank_opt << 4) & 0x70u) | ch;

    uint8_t msg[42];
    msg[0]  = 0xF0;
    msg[1]  = 0x41; /* Roland */
    msg[2]  = 0x30; /* APR patch format */
    msg[3]  = ch_byte;
    msg[4]  = patch_idx % 64u;

    /* 16 sliders */
    msg[5]  = p->p[3] & 0x7Fu;  /* lfo_rate */
    msg[6]  = p->p[4] & 0x7Fu;  /* lfo_delay */
    msg[7]  = p->p[5] & 0x7Fu;  /* dco_lfo_depth */
    msg[8]  = p->p[6] & 0x7Fu;  /* dco_pwm_depth */
    msg[9]  = p->p[8] & 0x7Fu;  /* dco_noise_level */
    msg[10] = p->p[10] & 0x7Fu; /* vcf_cutoff */
    msg[11] = p->p[11] & 0x7Fu; /* vcf_resonance */
    msg[12] = p->p[12] & 0x7Fu; /* vcf_env_mod */
    msg[13] = p->p[13] & 0x7Fu; /* vcf_lfo_mod */
    msg[14] = p->p[14] & 0x7Fu; /* vcf_kbd_track */
    msg[15] = p->p[15] & 0x7Fu; /* vca_level */
    msg[16] = p->p[16] & 0x7Fu; /* env_a */
    msg[17] = p->p[17] & 0x7Fu; /* env_d */
    msg[18] = p->p[18] & 0x7Fu; /* env_s */
    msg[19] = p->p[19] & 0x7Fu; /* env_r */
    msg[20] = p->p[7] & 0x7Fu;  /* dco_sub_level */

    uint8_t chorus = FM106_CHORUS_OFF;
    if (p->p[27] && p->p[28]) chorus = FM106_CHORUS_I_II;
    else if (p->p[28]) chorus = FM106_CHORUS_II;
    else if (p->p[27]) chorus = FM106_CHORUS_I;

    msg[21] = fm106_encode_sw1(p->p[29], p->p[23], p->p[24], chorus);
    msg[22] = fm106_encode_sw2(p->p[33], p->p[34], p->p[35], p->p[9]);

    /* Transmit 40-byte extended SysEx if name present, or 24-byte vintage APR if blank */
    if (p->name[0]) {
        uint32_t len = 0;
        while (p->name[len] && len < 15u) {
            msg[23 + len] = (uint8_t)p->name[len] & 0x7Fu;
            len++;
        }
        while (len < 16u) {
            msg[23 + len] = ' ';
            len++;
        }
        msg[39] = 0xF7;
        fm106_sysex_send(msg, 40);

        /* Also transmit patch name in 0x33 message for vintage compatibility */
        uint8_t nmsg[28];
        nmsg[0] = 0xF0;
        nmsg[1] = 0x41;
        nmsg[2] = 0x33;
        nmsg[3] = ch_byte;
        nmsg[4] = patch_idx % 64u;
        nmsg[5] = (uint8_t)(patch_idx / 64u);
        uint32_t nlen = 0;
        while (p->name[nlen] && nlen < 15u) {
            nmsg[6 + nlen] = (uint8_t)p->name[nlen] & 0x7Fu;
            nlen++;
        }
        nmsg[6 + nlen] = 0xF7;
        fm106_sysex_send(nmsg, 7 + nlen);
    } else {
        msg[23] = 0xF7;
        fm106_sysex_send(msg, 24);
    }
}

/* Parse and process incoming Roland SysEx message (bytes after F0 up to F7) */
static void fm106_sysex_handle(const uint8_t *p, uint32_t n)
{
    if (n < 3 || p[0] != 0x41) return;

    uint8_t cmd = p[1];
    uint8_t ch  = p[2] & 0x0Fu;
    uint8_t bank_opt = (p[2] >> 4) & 0x07u;

    /* 1. IPR (Individual Parameter Real-Time): 41 32 0n ctrl val */
    if (cmd == 0x32 && n >= 5) {
        uint8_t ctrl = p[3];
        uint8_t val  = p[4] & 0x7Fu;

        if (ctrl <= 0x0Fu) {
            switch (ctrl) {
            case 0x00: g_synth.lfo_rate = val; break;
            case 0x01: g_synth.lfo_delay = val; break;
            case 0x02: g_synth.dco_lfo_depth = val; break;
            case 0x03: g_synth.dco_pwm_depth = val; break;
            case 0x04: g_synth.dco_noise_level = val;
                       for (uint32_t v=0; v<FM106_NUM_VOICES; v++) g_synth.voice[v].dco.noise_level = val;
                       break;
            case 0x05: g_synth.vcf_cutoff = val; break;
            case 0x06: g_synth.vcf_resonance = val; break;
            case 0x07: g_synth.vcf_env_mod = val; break;
            case 0x08: g_synth.vcf_lfo_mod = val; break;
            case 0x09: g_synth.vcf_kbd_track = val; break;
            case 0x0A: g_synth.vca_level = val; break;
            case 0x0B: g_synth.env_a = val;
                       for (uint32_t v=0; v<FM106_NUM_VOICES; v++) fm106_adsr_params(&g_synth.voice[v].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                       break;
            case 0x0C: g_synth.env_d = val;
                       for (uint32_t v=0; v<FM106_NUM_VOICES; v++) fm106_adsr_params(&g_synth.voice[v].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                       break;
            case 0x0D: g_synth.env_s = val;
                       for (uint32_t v=0; v<FM106_NUM_VOICES; v++) fm106_adsr_params(&g_synth.voice[v].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                       break;
            case 0x0E: g_synth.env_r = val;
                       for (uint32_t v=0; v<FM106_NUM_VOICES; v++) fm106_adsr_params(&g_synth.voice[v].adsr, g_synth.env_a, g_synth.env_d, g_synth.env_s, g_synth.env_r);
                       break;
            case 0x0F: g_synth.dco_sub_level = val;
                       g_synth.dco_sub_on = (val > 0);
                       for (uint32_t v=0; v<FM106_NUM_VOICES; v++) {
                           g_synth.voice[v].dco.sub_level = val;
                           g_synth.voice[v].dco.sub_on = g_synth.dco_sub_on;
                       }
                       break;
            }
        } else if (ctrl == 0x10) {
            /* SW1 */
            uint8_t oct, pulse, saw, chorus;
            fm106_decode_sw1(val, &oct, &pulse, &saw, &chorus);
            g_synth.dco_range    = oct;
            g_synth.dco_pulse_on = pulse;
            g_synth.dco_saw_on   = saw;
            g_synth.chorus.mode  = chorus;
            for (uint32_t v=0; v<FM106_NUM_VOICES; v++) {
                g_synth.voice[v].dco.pulse_on = pulse;
                g_synth.voice[v].dco.saw_on   = saw;
                if (g_synth.voice[v].note != 255u)
                    g_synth.voice[v].base_inc = fm106_calc_inc(g_synth.voice[v].note, 0, oct);
            }
        } else if (ctrl == 0x11) {
            /* SW2 */
            uint8_t pwm_m, vcf_inv, vca_m, hpf_m;
            fm106_decode_sw2(val, &pwm_m, &vcf_inv, &vca_m, &hpf_m);
            g_synth.dco_pwm_mode     = pwm_m;
            g_synth.vcf_env_polarity = vcf_inv;
            g_synth.vca_mode         = vca_m;
            g_synth.hpf.mode         = hpf_m;
        }
        g_dirty_screen = 1;
        return;
    }

    /* 2. APR Manual Mode (Audition to live buffer): 41 31 0n pp [16 sliders] [sw1] [sw2] */
    if (cmd == 0x31 && n >= 22) {
        fm106_apply_apr_to_engine(&g_synth, p + 4, p[20], p[21]);
        if (n >= 38) {
            uint32_t len = 0;
            while (len < 15u && (22u + len) < n && p[22 + len]) {
                g_synth.patch_name[len] = (char)p[22 + len];
                len++;
            }
            g_synth.patch_name[len] = '\0';
        }
        g_dirty_screen = 1;
        return;
    }

    /* 3. APR Patch Store to Slot: 41 30 0n pp [16 sliders] [sw1] [sw2] (opt: name) */
    if (cmd == 0x30 && n >= 22) {
        uint8_t user_slot;
        if (bank_opt == 3u) {
            user_slot = p[3] % 64u;         /* Explicit User A */
        } else if (bank_opt == 4u) {
            user_slot = 64u + (p[3] % 64u); /* Explicit User B */
        } else {
            user_slot = p[3] % FM106_NUM_USER_PRESETS; /* Standard 0..127 */
        }
        char nm[16] = {0};
        if (n >= 38) {
            for (uint32_t i = 0; i < 15u && (22u + i) < n; i++) nm[i] = (char)p[22 + i];
        }
        fm106_store_apr_to_preset(&g_user_presets[user_slot], p + 4, p[20], p[21], nm[0] ? nm : 0, (n < 38u));
        if ((128u + user_slot) == g_synth.patch_idx) {
            fm106_load_user_preset(&g_synth, 128u + user_slot);
        }
        g_dirty_screen = 1;
        return;
    }

    /* 4. Patch Rename: 41 33 0n pp [name bytes] */
    if (cmd == 0x33 && n >= 5) {
        uint8_t user_slot;
        if (bank_opt == 3u) {
            user_slot = p[3] % 64u;
        } else if (bank_opt == 4u) {
            user_slot = 64u + (p[3] % 64u);
        } else {
            user_slot = p[3] % FM106_NUM_USER_PRESETS;
        }
        uint32_t len = 0;
        while (len < 15u && (4u + len) < n && p[4 + len]) {
            ((char *)g_user_presets[user_slot].name)[len] = (char)p[4 + len];
            len++;
        }
        ((char *)g_user_presets[user_slot].name)[len] = '\0';
        if ((128u + user_slot) == g_synth.patch_idx) {
            for (uint32_t i = 0; i < 16u; i++) {
                g_synth.patch_name[i] = g_user_presets[user_slot].name[i];
            }
        }
        g_dirty_screen = 1;
        return;
    }

    /* 5. Request Patch Dump: 41 34 0n pp */
    if (cmd == 0x34 && n >= 4) {
        uint8_t target_patch;
        if (bank_opt == 1u) {
            target_patch = p[3] % 64u;          /* Factory A */
        } else if (bank_opt == 2u) {
            target_patch = 64u + (p[3] % 64u);  /* Factory B */
        } else if (bank_opt == 3u) {
            target_patch = 128u + (p[3] % 64u); /* User A */
        } else if (bank_opt == 4u) {
            target_patch = 192u + (p[3] % 64u); /* User B */
        } else {
            /* Vintage Roland fallback: follows active synth bank or 0..127 */
            if (g_synth.patch_idx >= 128u) {
                target_patch = 128u + (p[3] % 128u);
            } else {
                target_patch = p[3] % 128u;
            }
        }
        fm106_sysex_dump_patch(target_patch);
        return;
    }

    /* 6. Commit All User Presets to SPI Flash: 41 35 0n 01 */
    if (cmd == 0x35 && n >= 4 && p[3] == 0x01) {
        fm106_user_presets_save_flash();
        /* Send ACK */
        uint8_t ack[6] = {0xF0, 0x41, 0x35, ch, 0x00, 0xF7};
        fm106_sysex_send(ack, sizeof(ack));
        return;
    }

    /* 7. Reset User Presets to Factory Defaults: 41 36 0n 01 */
    if (cmd == 0x36 && n >= 4 && p[3] == 0x01) {
        fm106_user_presets_restore_factory();
        fm106_user_presets_save_flash();
        fm106_load_user_preset(&g_synth, g_synth.patch_idx);
        uint8_t ack[6] = {0xF0, 0x41, 0x36, ch, 0x00, 0xF7};
        fm106_sysex_send(ack, sizeof(ack));
        g_dirty_screen = 1;
        return;
    }

    /* 8. Remote Patch / Bank Select: 41 37 0n pp */
    if (cmd == 0x37 && n >= 4) {
        uint8_t target_patch;
        if (bank_opt >= 1u && bank_opt <= 4u) {
            target_patch = (bank_opt - 1u) * 64u + (p[3] % 64u);
        } else {
            target_patch = p[3];
        }
        fm106_load_user_preset(&g_synth, target_patch);
        g_dirty_screen = 1;
        fm106_sysex_dump_patch(target_patch);
        return;
    }

    /* 9. Diagnostic / Battery Query: 41 39 0n */
    if (cmd == 0x39) {
        int32_t bval = fm1_adc_read(FM1_ADC_BATT);
        if (bval < 0) bval = 0;
        uint8_t resp[7] = {0xF0, 0x41, 0x39, ch, (uint8_t)((bval >> 7) & 0x7Fu), (uint8_t)(bval & 0x7Fu), 0xF7};
        fm106_sysex_send(resp, 7);
        return;
    }
}
