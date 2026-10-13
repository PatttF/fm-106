/* SPDX-License-Identifier: GPL-3.0-only */
/* Based on Melodee fx.c © 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments — GPL-3.0-only.
 * Single-instance port of the Melodee stereo insert-effects engine, trimmed to
 * one kernel: OFF / TAPE, in a static buffer. TAPE is enhanced with Airwindows
 * ToTape9 ideas (MIT, Chris Johnson): the degenerate Taylor-series sin()
 * saturation core, a resonant head-bump lowpass, an asymmetric bias follower,
 * and a sine-swept flutter delay — none of which the original Melodee tape had.
 * Only the multi-slot bus machinery (plan/send/return, worker offload, HW-EQ
 * probe, arena allocation, pan coupling) is gone.
 * fm106_fx_run takes interleaved L/R int32 and edits it in place, n = 256,
 * internally sliced into FX_CTL = 32 sub-blocks so every control-rate cadence
 * (LFO phase, mix ramp, decim counters, ring advances, reverb drift, tape
 * flutter) keeps a per-CTL schedule. A user type change fades the wet return to
 * silence, resets state (rng seed 0x73a9825d), then fades the new kernel in.
 * type/mix/rate/character are the user parameters; active/gain/epoch plus the
 * DSP histories are engine-owned. */
#ifndef FM106_FX_H
#define FM106_FX_H

#include <stdint.h>
#include "fm106_fx_tables.h"

#ifndef FS
#define FS 44100u                               /* the sample rate; the unity build's platform.c defines it first */
#endif
#define FX_CTL 32u                              /* control sub-block (Melodee CTL) */
#define FX_RATE (FS/4u)

enum { FM106_FX_OFF=0, FM106_FX_TAPE, FM106_FX_COUNT };

typedef struct { float b0,b1,b2,a1,a2,z1,z2; } fx_bq_t;
typedef struct fm106_fx {
    fx_bq_t q[2][4];
    uint32_t phase, age, epoch, pos;
    int32_t rng, random;
    int16_t rate, character;
    uint8_t mono, decim, failed;
    int32_t rev_damp[8], rev_l, rev_r, rev_prev_l, rev_prev_r;
    int32_t rev_bass[8];
    int16_t mix;            /* user wet amount 0..127 (127 = 1.0) */
    uint8_t type;           /* user-selected FM106_FX_* */
    uint8_t active;         /* kernel type running after the fade completes */
    uint8_t si;             /* slot index into fm106_fx_mem (chain position) */
    int32_t gain;           /* wet return ramp toward target (Melodee's fx_state_t.mix) */
    /* stability + new-kernel state (epoch-reset with the rest) */
    fx_bq_t hb[2];          /* head-bump resonant lowpass, one per channel */
    float fl_sweep;         /* flutter delay-modulation LFO phase (radians) */
    uint16_t fl_wr;         /* flutter ring write (shared L/R index) */
    float tape_dc;          /* bias follower: asymmetric DC before the sin core */
    float tape_hp[2];       /* subsonic/DC block at tape output (per channel) */
} fm106_fx_t;

/* One effect instance: 8 KiB union (TAPE uses 512-sample flutter rings) in
 * .pool; block scratch Melodee sized for CTL = 32. */
#define FM106_FX_SLOTS 1u
#define FX_TAPE_RING 512u
static int16_t fm106_fx_mem[FM106_FX_SLOTS][4096] __attribute__((section(".pool")));
static uint32_t fm106_fx_epoch;
static int32_t fx_left[FX_CTL],fx_right[FX_CTL],fx_dry_l[FX_CTL],fx_dry_r[FX_CTL];
static int32_t fx_low_l[FX_CTL/4u+1u],fx_low_r[FX_CTL/4u+1u];

/* sine, linearly interpolated between the 1024 table points (Melodee sine_i/fx_sine) */
static inline __attribute__((always_inline)) int32_t fm106_fx_sine(int32_t ph)
{
    uint32_t p=(uint32_t)ph,i=p>>22;
    int32_t a=FM106_FX_SINE[i],b=FM106_FX_SINE[i+1u];
    return a+(((b-a)*(int32_t)((p>>7)&32767u))>>15);
}
/* xorshift32 */
static inline __attribute__((always_inline)) uint32_t fm106_fx_noise(int32_t *st)
{
    uint32_t s=(uint32_t)*st;
    s^=s<<13;
    s^=s>>17;
    s^=s<<5;
    *st=(int32_t)s;
    return s;
}
static inline __attribute__((always_inline)) float fx_bound(float x,float lim) { return x < -lim ? -lim : x > lim ? lim : x; }
static inline __attribute__((always_inline)) int32_t fx_scale(int32_t a,uint32_t k)
{return (a>>16)*(int32_t)k+(int32_t)(((uint32_t)(a&65535)*k)>>16);}
static inline __attribute__((always_inline)) int32_t fx_limit(int32_t a,int32_t lo,int32_t hi)
{return a<lo?lo:a>hi?hi:a;}
/* Preserve C's truncation towards zero with shifts rather than the target's
 * expensive integer divider. Callers' bounded DSP values leave sign headroom. */
static inline __attribute__((always_inline)) int32_t fx_div_pow2(int32_t x,uint32_t shift)
{return (x+(int32_t)(((uint32_t)x>>31)*((1u<<shift)-1u)))>>shift;}
/* TANH softclip, used by the time kernels that bound their feedback loops. */
static inline __attribute__((always_inline)) int32_t fx_reverb_softclip(int32_t x)
{
    int32_t a=x<0?-x:x,i=a>>8,y;
    if(i>=256)y=FM106_TANH[256];
    else y=FM106_TANH[i]+((FM106_TANH[i+1]-FM106_TANH[i])*(a&255)>>8);
    return x<0?-y:y;
}
static void fx_coeff(fx_bq_t *q,uint32_t type,int32_t hz,float resonance,float gain)
{
    uint32_t ph=(uint32_t)fm106_clamp(hz,60,(int32_t)FX_RATE*45/100)*(uint32_t)(0x100000000ull/FX_RATE);
    float s=(float)fm106_fx_sine((int32_t)ph)*(1.0f/32768.0f),c=(float)fm106_fx_sine((int32_t)(ph+0x40000000u))*(1.0f/32768.0f);
    float alpha=s/(2.0f*resonance),inv=1.0f/(1.0f+alpha);
    q->a1=-2.0f*c*inv;q->a2=(1.0f-alpha)*inv;
    if(type==0u){q->b0=q->a2;q->b1=q->a1;q->b2=1.0f;} /* all pass */
    else if(type==1u){q->b0=(1.0f-c)*0.5f*inv;q->b1=2.0f*q->b0;q->b2=q->b0;}
    else if(type==2u){q->b0=alpha*inv;q->b1=0;q->b2=-q->b0;} /* band pass */
    else { /* peaking EQ: bounded pole radius, gain is amplitude's square root */
        inv=1.0f/(1.0f+alpha/gain);
        q->a1=-2.0f*c*inv;q->a2=(1.0f-alpha/gain)*inv;
        q->b0=(1.0f+alpha*gain)*inv;q->b1=q->a1;q->b2=(1.0f-alpha*gain)*inv;
    }
}
static inline __attribute__((always_inline)) float fx_tick(fx_bq_t *q,float x)
{
    float y=q->b0*x+q->z1;
    q->z1=fx_bound(q->b1*x-q->a1*y+q->z2,2000000.0f);
    q->z2=fx_bound(q->b2*x-q->a2*y,2000000.0f);
    return fx_bound(y,1000000.0f);
}
/* SPIT's only design path: a random band-pass that reseeds on LFO wrap. Both
 * stereo sides share coefficients with independent histories. */
/* TAPE band skeleton (Melodee): three stable one-pole pairs track the low/mid
 * split and the top rolloff. The fourth section stores the polarity control;
 * z1/z2 hold each channel's independent filter histories. Coefficients are
 * re-derived on LFO wrap (the "age" wobble). Head-bump coeffs are derived here
 * too and applied in fx_tape. */
static void fx_tape_design(fm106_fx_t *f,uint32_t n)
{
    uint32_t first=!f->age,old_phase=f->phase;int32_t old_random=f->random;
    fx_bq_t *control=&f->q[0][3];
    f->phase+=FM106_FX_LFO_INC[f->rate]*n/FX_CTL;
    if(f->phase<old_phase || first)f->random=(int32_t)(fm106_fx_noise(&f->rng)&127u);
    f->age=1;
    if(!first && old_random==f->random && control->b2==(float)f->character)return;
    int32_t cha=f->character,low=200+(f->random-64)*cha/64;
    int32_t hz[3]={low,fm106_clamp(1400+(f->random-64)*cha/4,low+150,8000),6500-cha*38};
    for(uint32_t k=0;k<3u;k++){
        float w=(float)fm106_clamp(hz[k],60,(int32_t)FX_RATE*45/100)*(6.28318530718f/FX_RATE);
        f->q[0][k].b1=w/(1.0f+w);
        if(first)f->q[0][k].b0=f->q[0][k].b1;
    }
    control->b1=cha>32 && (f->random&64)?-1.0f:1.0f;control->b2=(float)cha;
    if(first)control->b0=1.0f;
    /* Head-bump: a resonant lowpass whose corner tracks RATE (ToTape9 H²·175+25 Hz,
     * scaled into the FS/4 decimated band). CHAR raises the resonance for more thump. */
    int32_t bump_hz=(int32_t)(25.0f+((float)f->rate*(float)f->rate/16129.0f)*175.0f);
    float res=0.70f+(float)f->character/127.0f;      /* 0.70..1.70 Q */
    for(uint32_t ch=0;ch<2u;ch++)fx_coeff(&f->hb[ch],1,bump_hz,res,1);
}
static inline __attribute__((always_inline)) float fx_tape_lp(fx_bq_t *q,float x,float g)
{
    q->z1+=(x-q->z1)*g;q->z2+=(q->z1-q->z2)*g;return q->z2;
}
/* ToTape9's degenerate Taylor-series sin() saturator, ported to float. Clamp to
 * ±2.306 then evaluate the odd polynomial: the peak softens through the sin
 * rolloff rather than hard-clipping, giving tape's gentle even/odd harmonic
 * bloom. Multiplies only — no libm call. */
static inline __attribute__((always_inline)) float fx_tape_sin(float s)
{
    if(s>2.305929f)s=2.305929f;else if(s<-2.305929f)s=-2.305929f;
    float a2=s*s,e=s*a2;              /* e = s^3 */
    s-=e*(1.0f/6.0f);
    e*=a2;s+=e*(1.0f/69.0f);          /* s^5 */
    e*=a2;s-=e*(1.0f/2530.08f);       /* s^7 */
    e*=a2;s+=e*(1.0f/224985.6f);      /* s^9 */
    e*=a2;s-=e*(1.0f/9979200.0f);     /* s^11 */
    return s;
}
/* TAPE (Melodee band skeleton + Airwindows ToTape9 ideas). Runs in place on the
 * FS/4-decimated buffers (fx_low_l/fx_low_r, length n); the down/up path does
 * the wet/dry return. Adds, beyond Melodee: (1) ToTape9's degenerate Taylor
 * sin() saturation core — the main missing "tape glue"; (2) a signal-following
 * bias follower for asymmetric even-harmonic saturation; (3) a resonant
 * head-bump lowpass (bass thump); (4) a sine-swept flutter delay (wow/flutter).
 * RATE = flutter depth+speed and head-bump corner; CHAR = drive+bias+bump mix. */
static void fx_tape(fm106_fx_t *restrict f,int16_t *restrict mem,int32_t *restrict left,int32_t *restrict right,uint32_t n)
{
    const float slew=1.0f/(0.015f*FX_RATE);       /* ~15 ms band/polarity smoothing */
    float drive=1.0f+(float)f->character/127.0f*2.0f;   /* 1..3 saturation drive */
    float bias=(((float)f->character/127.0f)-0.5f)*0.30f; /* ±0.15 asymmetry */
    float bumpmix=(float)f->character/127.0f*0.35f;      /* 0..0.35 head-bump */
    uint32_t fl_depth=(uint32_t)((float)(f->rate*128)/127.0f);        /* 0..128 samples @FS/4 */
    float fl_freq=(0.5f+(float)(f->rate*f->rate)/16129.0f*6.0f)/(float)FX_RATE; /* cycles/sample, 0.5..6.5 Hz */
    uint32_t channels=f->mono?1u:2u;
    for(uint32_t i=0;i<n;i++){
        for(uint32_t k=0;k<4u;k++)f->q[0][k].b0+=(f->q[0][k].b1-f->q[0][k].b0)*slew;
        float blend=f->q[0][3].b0-1.0f;
        for(uint32_t ch=0;ch<channels;ch++){
            int32_t *b=ch?right:left;float x=(float)b[i];
            /* Flutter: write this sample, read a sine-swept fractional tap. Per-channel
             * ring halves (mem[0..255]=L, mem[256..511]=R) so L/R don't clobber. */
            if(fl_depth){
                int16_t *ring=mem+ch*256u;
                ring[f->fl_wr]=(int16_t)fx_limit((int32_t)x,-32767,32767);
                float sw=(float)fm106_fx_sine((int32_t)(f->fl_sweep*4294967296.0f))*(1.0f/32768.0f);
                float offset=(float)fl_depth+(float)fl_depth*sw;      /* 0..2*depth */
                uint32_t ip=(uint32_t)offset,frac=(uint32_t)((offset-(float)ip)*65536.0f);
                uint32_t d0=(f->fl_wr+256u-ip)&255u,d1=(d0+255u)&255u;
                x=(float)ring[d0]+(float)(((ring[d1]-ring[d0])*(int32_t)frac)>>16);
            }
            /* Band skeleton (Melodee): low + polarity-flipped mid + top rolloff. */
            float low=fx_tape_lp(&f->q[ch][0],x,f->q[0][0].b0);
            float upper=fx_tape_lp(&f->q[ch][1],x,f->q[0][1].b0);
            x+=blend*(upper-low);
            x=fx_tape_lp(&f->q[ch][2],x,f->q[0][2].b0);
            /* Bias: signal-following DC offset → asymmetric saturation (even harmonics). */
            f->tape_dc+=(x*0.002f-f->tape_dc)*0.001f;
            x+=f->tape_dc*bias*48.0f;
            /* Core saturation (ToTape9 TapeHack): normalize, drive, sin(). */
            x=fx_tape_sin(x*(drive*(1.0f/32768.0f)))*32768.0f;
            /* Head-bump: resonant low-frequency thump (ToTape9 hdb). */
            if(bumpmix>0.0f)x+=fx_tick(&f->hb[ch],x)*bumpmix*1.0f;
            /* Subsonic/DC block (~25 Hz @FS/4): head-bump unity-DC gain and the
             * bias follower both inject low energy that a long reverb LF decay
             * turns into boom. One-pole HP removes it before the return. */
            f->tape_hp[ch]+=(x-f->tape_hp[ch])*0.014f;
            x-=f->tape_hp[ch];
            /* Bound like SPIT: the sin-core + head-bump can stack past full scale
             * at hot drive; TANH softclip keeps |x| <= ~32605. */
            b[i]=fx_reverb_softclip((int32_t)fx_bound(x,40000.0f));
        }
        if(f->mono){f->q[1][0].z1=f->q[0][0].z1;f->q[1][0].z2=f->q[0][0].z2;f->hb[1].z1=f->hb[0].z1;f->hb[1].z2=f->hb[0].z2;f->tape_hp[1]=f->tape_hp[0];}
        /* Advance the shared flutter LFO once per stereo sample. */
        if(fl_depth){
            f->fl_wr=(uint16_t)((f->fl_wr+1u)&255u);
            f->fl_sweep+=fl_freq;
            if(f->fl_sweep>=1.0f)f->fl_sweep-=1.0f;
        }
    }
    /* Clear inaudible tails before they can become costly denormals. */
    for(uint32_t ch=0;ch<2u;ch++)for(uint32_t k=0;k<3u;k++){
        fx_bq_t *q=&f->q[ch][k];
        if(q->z1>-1e-9f && q->z1<1e-9f)q->z1=0;
        if(q->z2>-1e-9f && q->z2<1e-9f)q->z2=0;
        fx_bq_t *h=&f->hb[ch];
        if(h->z1>-1e-9f && h->z1<1e-9f)h->z1=0;
        if(h->z2>-1e-9f && h->z2<1e-9f)h->z2=0;
    }
}
/* Wet/dry return: mix ramps ±128/block toward target (Melodee fx_return), with
 * no per-type special cases now that PHASER/WIDTH are gone. */
static inline __attribute__((always_inline)) void fx_return(int32_t target,uint32_t mono,uint32_t i,int32_t wl,int32_t wr,int32_t *mix)
{
    int32_t l=fx_dry_l[i],r=fx_dry_r[i];
    if(*mix!=target)*mix+=fx_limit(target-*mix,-128,128);
    fx_left[i]=l+fx_scale(wl-l,(uint32_t)*mix*2u);
    fx_right[i]=mono?fx_left[i]:r+fx_scale(wr-r,(uint32_t)*mix*2u);
}
static uint32_t fx_down(fm106_fx_t *restrict f,uint32_t n)
{
    uint32_t count=0,channels=f->mono?1u:2u;
    for(uint32_t ch=0;ch<channels;ch++){
        int32_t *input=ch?fx_right:fx_left,*output=ch?fx_low_r:fx_low_l,*h=f->rev_damp+ch*4u;
        int32_t a=h[0],b=h[1],previous=h[2],difference=h[3],low=f->rev_bass[ch];
        uint32_t phase=f->decim;count=0;
        for(uint32_t i=0;i<n;i++){
            a=(int32_t)((uint32_t)a+(uint32_t)input[i]);b=(int32_t)((uint32_t)b+(uint32_t)a);
            if(!phase){
                uint32_t comb=(uint32_t)b-(uint32_t)previous;previous=b;
                int32_t x=fx_div_pow2((int32_t)(comb-(uint32_t)difference),4);difference=(int32_t)comb;
                low=fx_div_pow2(low+3*x,2);output[count++]=low;
            }
            phase=(phase+1u)&3u;
        }
        h[0]=a;h[1]=b;h[2]=previous;h[3]=difference;f->rev_bass[ch]=low;
    }
    if(channels==1u){for(uint32_t k=0;k<4u;k++)f->rev_damp[4+k]=f->rev_damp[k];f->rev_bass[1]=f->rev_bass[0];}
    return count;
}
static void fx_up(fm106_fx_t *f,uint32_t n,int32_t target)
{
    uint32_t phase=f->decim,count=0;int32_t mix=f->gain;
    for(uint32_t i=0;i<n;i++){
        if(!phase){
            f->rev_prev_l=f->rev_l;f->rev_prev_r=f->rev_r;
            int32_t l=fx_low_l[count],r=f->mono?l:fx_low_r[count];count++;
            f->rev_l=l;f->rev_r=r;
        }
        int32_t l=f->rev_prev_l+fx_div_pow2((f->rev_l-f->rev_prev_l)*(phase+1u),2);
        int32_t r=f->mono?l:f->rev_prev_r+fx_div_pow2((f->rev_r-f->rev_prev_r)*(phase+1u),2);
        fx_return(target,f->mono,i,l,r,&mix);phase=(phase+1u)&3u;
    }
    f->decim=(uint8_t)phase;f->gain=mix;
}
/* TAPE never folds — fx_copy only snapshots the dry reference. */
static void fx_copy(fm106_fx_t *f,uint32_t n)
{
    (void)f;
    for(uint32_t i=0;i<n;i++){
        fx_dry_l[i]=fx_left[i];fx_dry_r[i]=fx_right[i];
    }
}
/* Zero every DSP history and control, preserving the user parameters and the
 * epoch stamp (Melodee's fx_zero, minus the resource-arena bookkeeping). */
static void fm106_fx_zero(fm106_fx_t *f)
{
    int16_t mix=f->mix,rate=f->rate,character=f->character;
    uint8_t type=f->type,failed=f->failed;uint32_t epoch=f->epoch;
    typedef uint32_t word_t __attribute__((may_alias));word_t *words=(word_t *)f;
    for(uint32_t k=0;k<sizeof *f/4u;k++)words[k]=0;
    f->type=type;f->mix=mix;f->rate=rate;f->character=character;f->failed=failed;f->epoch=epoch;
}
/* Melodee's fx_slot_prepare for one global slot: compute the wet target from
 * the user parameters, fade out before a topology change, reset state and
 * reseed the noise when the kernel switches, release when fully bypassed. */
static int32_t fm106_fx_prepare(fm106_fx_t *f)
{
    uint32_t want=(uint32_t)fm106_clamp(f->type,0,FM106_FX_COUNT-1);
    int32_t amount=fm106_clamp(f->mix,0,127);
    int32_t target=want?(amount+(amount==127))*256:0;
    if(f->active && want && f->active!=want && f->gain)target=0;
    if(!target && !f->gain){if(f->active)fm106_fx_zero(f);return 0;}
    if(f->epoch!=fm106_fx_epoch || (want && f->active!=want && !f->gain)){
        fm106_fx_zero(f);
        for(uint32_t k=0;k<4096u;k++)fm106_fx_mem[f->si][k]=0;
        f->epoch=fm106_fx_epoch;
        f->active=(uint8_t)want;
        f->rng=(int32_t)0x73a9825du;
    }
    f->rate=(int16_t)fm106_clamp(f->rate,0,127);
    f->character=(int16_t)fm106_clamp(f->character,0,127);
    return target;
}
/* One FX_CTL sub-block: tape path (design → FS/4 decimate → tape → upsample). */
static void fm106_fx_slot(fm106_fx_t *f,uint32_t n,int32_t target)
{
    fx_copy(f,n);
    fx_tape_design(f,n);
    uint32_t count=fx_down(f,n);
    fx_tape(f,fm106_fx_mem[f->si],fx_low_l,fx_low_r,count);
    fx_up(f,n,target);
}
void fm106_fx_init(fm106_fx_t *f,uint32_t slot)
{
    fm106_fx_zero(f);
    f->si=(uint8_t)(slot<FM106_FX_SLOTS?slot:0u);
    f->type=FM106_FX_OFF;
    f->mix=0;
    f->rate=60;
    f->character=64;
}
void fm106_fx_run(fm106_fx_t *f,int32_t *stereo,uint32_t n)
{
    /* Fully bypassed: no audio work (state release costs nothing). */
    if(!f->type && !f->mix && !f->gain){if(f->active)fm106_fx_zero(f);return;}
    while(n){
        uint32_t m=n<FX_CTL?n:FX_CTL;
        int32_t target=fm106_fx_prepare(f);
        if(f->active){
            for(uint32_t i=0;i<m;i++){fx_left[i]=stereo[2u*i];fx_right[i]=stereo[2u*i+1u];}
            fm106_fx_slot(f,m,target);
            for(uint32_t i=0;i<m;i++){stereo[2u*i]=fx_left[i];stereo[2u*i+1u]=fx_right[i];}
        }
        stereo+=2u*m;n-=m;
    }
}

#endif /* FM106_FX_H */
