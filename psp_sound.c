/*
 * psp_sound.c - Audio per Chex Quest PSP
 * Emulazione OPL2 (Yamaha YM3812) accurata
 * Basato sulle specifiche hardware reali
 */

#include "doomtype.h"
#include "i_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"
#include "memio.h"
#include "mus2mid.h"

#include <pspaudio.h>
#include <pspthreadman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

/* ==================== Costanti ==================== */

#define SND_CHANNELS    8
#define MIX_SAMPLES     512
#define OUTPUT_RATE     44100

/* ==================== OPL2 Constants ==================== */

#define OPL_RATE            49716
#define OPL_NUM_CHANNELS    9
#define OPL_NUM_OPERATORS   18

/* Envelope states */
#define EG_ATTACK   0
#define EG_DECAY    1
#define EG_SUSTAIN  2
#define EG_RELEASE  3
#define EG_OFF      4

/* Waveforms */
#define WF_SINE         0
#define WF_HALFSINE     1
#define WF_ABSSINE      2
#define WF_QUARTERSINE  3

/* ==================== OPL2 Tables (Hardware Accurate) ==================== */

/* Log-sin table: 256 entries, represents -log2(sin(x)) * 256 */
static const uint16_t logsin_table[256] = {
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137,
    1091, 1050, 1013, 979, 949, 920, 894, 869,
    846, 825, 804, 785, 767, 749, 732, 717,
    701, 687, 672, 659, 646, 633, 621, 609,
    598, 587, 576, 566, 556, 546, 536, 527,
    518, 509, 501, 492, 484, 476, 468, 461,
    453, 446, 439, 432, 425, 418, 411, 405,
    399, 392, 386, 380, 375, 369, 363, 358,
    352, 347, 341, 336, 331, 326, 321, 316,
    311, 307, 302, 297, 293, 289, 284, 280,
    276, 271, 267, 263, 259, 255, 251, 248,
    244, 240, 236, 233, 229, 226, 222, 219,
    215, 212, 209, 205, 202, 199, 196, 193,
    190, 187, 184, 181, 178, 175, 172, 169,
    167, 164, 161, 159, 156, 153, 151, 148,
    146, 143, 141, 138, 136, 134, 131, 129,
    127, 125, 122, 120, 118, 116, 114, 112,
    110, 108, 106, 104, 102, 100, 98, 96,
    94, 92, 91, 89, 87, 85, 84, 82,
    80, 79, 77, 76, 74, 72, 71, 69,
    68, 66, 65, 64, 62, 61, 59, 58,
    57, 55, 54, 53, 52, 50, 49, 48,
    47, 46, 45, 43, 42, 41, 40, 39,
    38, 37, 36, 35, 34, 33, 32, 31,
    30, 29, 28, 27, 27, 26, 25, 24,
    23, 23, 22, 21, 20, 20, 19, 18,
    17, 17, 16, 15, 15, 14, 13, 13,
    12, 12, 11, 10, 10, 9, 9, 8,
    8, 7, 7, 6, 6, 5, 5, 4,
    4, 4, 3, 3, 2, 2, 2, 1,
    1, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* Exponential table: 256 entries, represents 2^(-x/256) * 1024 */
static const uint16_t exp_table[256] = {
    1018, 1013, 1007, 1002, 996, 991, 986, 980,
    975, 969, 964, 959, 953, 948, 943, 938,
    932, 927, 922, 917, 912, 906, 901, 896,
    891, 886, 881, 876, 871, 866, 861, 856,
    851, 846, 841, 837, 832, 827, 822, 817,
    813, 808, 803, 798, 794, 789, 784, 780,
    775, 771, 766, 762, 757, 752, 748, 744,
    739, 735, 730, 726, 722, 717, 713, 709,
    704, 700, 696, 692, 688, 683, 679, 675,
    671, 667, 663, 659, 655, 651, 647, 643,
    639, 635, 631, 627, 623, 619, 616, 612,
    608, 604, 600, 597, 593, 589, 585, 582,
    578, 574, 571, 567, 564, 560, 556, 553,
    549, 546, 542, 539, 535, 532, 528, 525,
    521, 518, 515, 511, 508, 505, 501, 498,
    495, 491, 488, 485, 482, 478, 475, 472,
    469, 466, 462, 459, 456, 453, 450, 447,
    444, 441, 438, 435, 432, 429, 426, 423,
    420, 417, 414, 411, 408, 405, 402, 400,
    397, 394, 391, 388, 385, 383, 380, 377,
    374, 372, 369, 366, 363, 361, 358, 355,
    353, 350, 347, 345, 342, 340, 337, 334,
    332, 329, 327, 324, 322, 319, 317, 314,
    312, 309, 307, 304, 302, 299, 297, 295,
    292, 290, 288, 285, 283, 280, 278, 276,
    274, 271, 269, 267, 265, 262, 260, 258,
    256, 253, 251, 249, 247, 245, 243, 241,
    238, 236, 234, 232, 230, 228, 226, 224,
    222, 220, 218, 216, 214, 212, 210, 208,
    206, 204, 202, 200, 198, 196, 194, 193,
    191, 189, 187, 185, 183, 182, 180, 178,
    176, 174, 173, 171, 169, 167, 166, 164
};

/* Frequency multiplier table */
static const uint8_t mt[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

/* Key scale level table (dB attenuation per octave) */
static const uint8_t ksl_table[16] = {
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

/* Attack rate table */
static const uint8_t attack_table[64] = {
    255, 255, 255, 255, 254, 254, 254, 254,
    252, 252, 252, 252, 250, 250, 250, 250,
    248, 248, 248, 248, 244, 244, 244, 244,
    240, 240, 240, 240, 232, 232, 232, 232,
    224, 224, 224, 224, 208, 208, 208, 208,
    192, 192, 192, 192, 160, 160, 160, 160,
    128, 128, 128, 128, 96, 96, 96, 96,
    64, 64, 64, 64, 32, 32, 32, 32
};

/* Decay/Release rate table */
static const uint8_t decay_table[64] = {
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 2, 2, 2, 2, 2, 2,
    2, 2, 4, 4, 4, 4, 4, 4,
    4, 4, 8, 8, 8, 8, 8, 8,
    16, 16, 16, 16, 32, 32, 64, 128
};

/* F-Number table for each note (C to B) */
static const uint16_t fnumber[12] = {
    0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,
    0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287
};

/* ==================== OPL2 Operator ==================== */

typedef struct {
    /* Programmable registers */
    uint8_t     mult;       /* Frequency multiplier 0-15 */
    uint8_t     ksr;        /* Key scale rate */
    uint8_t     egt;        /* Envelope type (sustain) */
    uint8_t     vib;        /* Vibrato enable */
    uint8_t     am;         /* Tremolo enable */
    uint8_t     tl;         /* Total level (attenuation) 0-63 */
    uint8_t     ksl;        /* Key scale level 0-3 */
    uint8_t     ar;         /* Attack rate 0-15 */
    uint8_t     dr;         /* Decay rate 0-15 */
    uint8_t     sl;         /* Sustain level 0-15 */
    uint8_t     rr;         /* Release rate 0-15 */
    uint8_t     ws;         /* Waveform select 0-3 */
    
    /* Runtime state */
    uint32_t    phase;      /* Phase accumulator (20 bit) */
    uint32_t    phase_inc;  /* Phase increment */
    int32_t     env;        /* Envelope level (0-511, 0=max, 511=min) */
    int32_t     env_target; /* Envelope target level */
    int32_t     env_inc;    /* Envelope increment */
    uint8_t     eg_state;   /* Envelope state */
    uint8_t     key;        /* Key on flag */
    
    /* Precalculated values */
    int32_t     ksl_add;    /* KSL attenuation to add */
} opl_op_t;

/* ==================== OPL2 Channel ==================== */

typedef struct {
    opl_op_t    op[2];      /* Modulator [0] and Carrier [1] */
    uint16_t    fnum;       /* Frequency number */
    uint8_t     block;      /* Block (octave) */
    uint8_t     fb;         /* Feedback amount 0-7 */
    uint8_t     cnt;        /* Connection (algorithm) 0-1 */
    uint8_t     key_on;     /* Key on state */
    int32_t     fb_out[2];  /* Feedback output buffer */
    int32_t     vol_atten;  /* Volume attenuation from MIDI */
} opl_channel_t;

/* ==================== OPL2 Chip ==================== */

typedef struct {
    opl_channel_t   chan[OPL_NUM_CHANNELS];
    
    /* Global state */
    uint32_t        timer;
    uint8_t         trem_depth;     /* Tremolo depth */
    uint8_t         vib_depth;      /* Vibrato depth */
    uint32_t        trem_phase;     /* Tremolo LFO phase */
    uint32_t        vib_phase;      /* Vibrato LFO phase */
    int32_t         trem_value;     /* Current tremolo value */
    int32_t         vib_value;      /* Current vibrato value */
    
    /* Resampling */
    int32_t         sample_accum;
    int32_t         last_sample;
} opl_chip_t;

static opl_chip_t opl;

/* ==================== GENMIDI ==================== */

typedef struct {
    uint8_t     mult_ksr_egt_vib_am;
    uint8_t     ksl_tl;
    uint8_t     ar_dr;
    uint8_t     sl_rr;
    uint8_t     ws;
} genmidi_op_t;

typedef struct {
    genmidi_op_t    mod;
    genmidi_op_t    car;
    uint8_t         fb_cnt;
} genmidi_voice_t;

typedef struct {
    uint16_t        flags;
    uint8_t         fine_tuning;
    uint8_t         fixed_note;
    genmidi_voice_t voice[2];
} genmidi_instr_t;

#define GENMIDI_NUM_INSTRS  175
#define GENMIDI_FLAG_FIXED  0x0001
#define GENMIDI_FLAG_2VOICE 0x0004

static genmidi_instr_t genmidi_instrs[GENMIDI_NUM_INSTRS];
static int genmidi_loaded = 0;

/* ==================== MIDI Sequencer ==================== */

#define MIDI_CHANNELS   16
#define MAX_MIDI_EVENTS 32768
#define MAX_VOICES      9

typedef struct {
    uint32_t    tick;
    uint8_t     type;
    uint8_t     channel;
    uint8_t     data1;
    uint8_t     data2;
} midi_event_t;

typedef struct {
    uint8_t     program;
    uint8_t     volume;
    uint8_t     pan;
    uint8_t     expression;
    int16_t     pitch_bend;
    uint8_t     is_drum;
} midi_channel_t;

typedef struct {
    int         active;
    int         midi_ch;
    int         note;
    int         opl_ch;
    int         velocity;
    uint32_t    age;
} voice_t;

typedef struct {
    midi_event_t    events[MAX_MIDI_EVENTS];
    int             num_events;
    int             current_event;
    uint16_t        ticks_per_beat;
    uint32_t        us_per_beat;
    double          samples_per_tick;
    double          tick_accum;
    uint32_t        current_tick;
    uint32_t        voice_age;
    
    midi_channel_t  chan[MIDI_CHANNELS];
    voice_t         voices[MAX_VOICES];
    
    int             playing;
    int             looping;
    int             volume;
    
    void           *midi_data;
    int             midi_data_len;
} midi_state_t;

static midi_state_t midi;

/* ==================== SFX ==================== */

typedef struct {
    const uint8_t  *data;
    int             length;
    uint32_t        pos;
    uint32_t        step;
    int             vol;
    int             sep;
    int             handle;
    int             active;
} sfx_channel_t;

static sfx_channel_t sfx_channels[SND_CHANNELS];
static int psp_audio_ch = -1;
static volatile int snd_running = 0;
static SceUID snd_thread_id = -1;
static int next_handle = 1;
static int sfx_volume = 127;

static void *sfx_cache[2048];
static int sfx_cache_init = 0;

static int16_t __attribute__((aligned(64))) mix_buffer[MIX_SAMPLES * 2];

/* ==================== OPL2 Implementation ==================== */

static void opl_reset(void)
{
    int i, j;
    
    memset(&opl, 0, sizeof(opl));
    
    opl.trem_depth = 1;
    opl.vib_depth = 1;
    
    for (i = 0; i < OPL_NUM_CHANNELS; i++)
    {
        for (j = 0; j < 2; j++)
        {
            opl.chan[i].op[j].env = 511;
            opl.chan[i].op[j].eg_state = EG_OFF;
        }
    }
}

/* Calculate envelope rate */
static int calc_rate(int rate, int ksr, int block, int fnum)
{
    int rof;
    int eff_rate;
    
    if (rate == 0)
        return 0;
    
    /* Calculate rate offset from key */
    rof = (block << 1) | ((fnum >> 9) & 1);
    if (!ksr)
        rof >>= 2;
    
    eff_rate = (rate << 2) + rof;
    if (eff_rate > 63)
        eff_rate = 63;
    
    return eff_rate;
}

/* Update envelope for one operator */
static void opl_envelope_calc(opl_op_t *op, int block, int fnum)
{
    int rate;
    
    switch (op->eg_state)
    {
    case EG_ATTACK:
        if (op->ar == 0)
        {
            /* No attack, stay at current level */
            return;
        }
        else if (op->ar == 15)
        {
            /* Instant attack */
            op->env = 0;
            op->eg_state = EG_DECAY;
            return;
        }
        
        rate = calc_rate(op->ar, op->ksr, block, fnum);
        
        /* Attack uses different curve */
        op->env -= ((op->env * attack_table[rate]) >> 8) + 1;
        
        if (op->env <= 0)
        {
            op->env = 0;
            op->eg_state = EG_DECAY;
        }
        break;
        
    case EG_DECAY:
        if (op->dr == 0)
        {
            op->eg_state = EG_SUSTAIN;
            return;
        }
        
        rate = calc_rate(op->dr, op->ksr, block, fnum);
        op->env += decay_table[rate];
        
        if (op->env >= (op->sl << 5))
        {
            op->env = op->sl << 5;
            op->eg_state = EG_SUSTAIN;
        }
        break;
        
    case EG_SUSTAIN:
        if (op->egt)
        {
            /* Sustained - hold level */
            return;
        }
        /* Fall through to release */
        rate = calc_rate(op->rr, op->ksr, block, fnum);
        op->env += decay_table[rate];
        
        if (op->env >= 511)
        {
            op->env = 511;
            op->eg_state = EG_OFF;
        }
        break;
        
    case EG_RELEASE:
        rate = calc_rate(op->rr, op->ksr, block, fnum);
        op->env += decay_table[rate];
        
        if (op->env >= 511)
        {
            op->env = 511;
            op->eg_state = EG_OFF;
        }
        break;
        
    case EG_OFF:
    default:
        op->env = 511;
        break;
    }
}

/* Calculate output for one operator */
static int32_t opl_operator_calc(opl_op_t *op, int32_t phase_mod, int32_t trem, int32_t vol_atten)
{
    uint32_t phase;
    int32_t atten;
    int32_t output;
    uint16_t log_value;
    int neg;
    int exp_index;
    int exp_shift;
    
    /* Add phase modulation */
    phase = (op->phase + phase_mod) >> 10;
    phase &= 0x3FF;
    
    /* Calculate attenuation */
    atten = op->env + (op->tl << 3) + op->ksl_add + vol_atten;
    
    /* Add tremolo if enabled */
    if (op->am)
        atten += trem;
    
    /* Clamp attenuation */
    if (atten > 511)
        atten = 511;
    
    if (atten >= 511)
        return 0;
    
    /* Waveform lookup */
    neg = 0;
    
    switch (op->ws)
    {
    case WF_SINE:
        /* Full sine wave */
        if (phase & 0x200)
            neg = 1;
        if (phase & 0x100)
            log_value = logsin_table[~phase & 0xFF];
        else
            log_value = logsin_table[phase & 0xFF];
        break;
        
    case WF_HALFSINE:
        /* Half sine (positive only) */
        if (phase & 0x200)
            return 0;
        if (phase & 0x100)
            log_value = logsin_table[~phase & 0xFF];
        else
            log_value = logsin_table[phase & 0xFF];
        break;
        
    case WF_ABSSINE:
        /* Absolute sine (full-wave rectified) */
        if (phase & 0x100)
            log_value = logsin_table[~phase & 0xFF];
        else
            log_value = logsin_table[phase & 0xFF];
        break;
        
    case WF_QUARTERSINE:
    default:
        /* Quarter sine (pseudo-sawtooth) */
        if (phase & 0x100)
            return 0;
        log_value = logsin_table[phase & 0xFF];
        break;
    }
    
    /* Add attenuation to log value */
    log_value += (atten << 3);
    
    /* Convert log to linear using exp table */
    exp_index = log_value & 0xFF;
    exp_shift = log_value >> 8;
    
    if (exp_shift > 13)
        return 0;
    
    output = (exp_table[exp_index] << 1) >> exp_shift;
    
    if (neg)
        output = -output;
    
    return output;
}

/* Update phase for operator */
static void opl_phase_calc(opl_op_t *op, int fnum, int block, int32_t vib)
{
    uint32_t freq;
    
    /* Add vibrato if enabled */
    if (op->vib)
        fnum += vib;
    
    /* Clamp F-number */
    if (fnum < 0)
        fnum = 0;
    if (fnum > 0x3FF)
        fnum = 0x3FF;
    
    /* Calculate frequency:
     * freq = fnum * 2^(block-1) * Fsam / 2^20
     * phase_inc = freq * 2^20 / OPL_RATE
     * Simplified: phase_inc = fnum * 2^block * mult / 2
     */
    freq = fnum << block;
    op->phase_inc = freq * mt[op->mult];
    
    /* Advance phase */
    op->phase += op->phase_inc;
}

/* Generate one sample from OPL chip */
static int32_t opl_generate(void)
{
    int ch;
    int32_t output = 0;
    int32_t trem;
    int32_t vib;
    
    /* Update LFOs */
    opl.trem_phase += 19;   /* ~3.7 Hz at 49716 Hz */
    opl.vib_phase += 31;    /* ~6.1 Hz at 49716 Hz */
    
    /* Calculate tremolo value (0-52 in dB units) */
    trem = ((opl.trem_phase >> 8) & 0x3F);
    if (trem > 31)
        trem = 63 - trem;
    if (!opl.trem_depth)
        trem >>= 2;
    
    /* Calculate vibrato value (frequency deviation) */
    vib = (opl.vib_phase >> 8) & 0x1F;
    if (vib > 15)
        vib = 31 - vib;
    vib -= 8;
    if (!opl.vib_depth)
        vib >>= 1;
    
    /* Process each channel */
    for (ch = 0; ch < OPL_NUM_CHANNELS; ch++)
    {
        opl_channel_t *c = &opl.chan[ch];
        opl_op_t *mod = &c->op[0];
        opl_op_t *car = &c->op[1];
        int32_t mod_out, car_out;
        int32_t feedback;
        int32_t phase_mod;
        
        /* Skip if both operators are off */
        if (mod->eg_state == EG_OFF && car->eg_state == EG_OFF)
            continue;
        
        /* Update envelopes */
        opl_envelope_calc(mod, c->block, c->fnum);
        opl_envelope_calc(car, c->block, c->fnum);
        
        /* Update phases */
        opl_phase_calc(mod, c->fnum, c->block, vib);
        opl_phase_calc(car, c->fnum, c->block, vib);
        
        /* Calculate feedback for modulator */
        if (c->fb > 0)
        {
            feedback = (c->fb_out[0] + c->fb_out[1]) >> (9 - c->fb);
        }
        else
        {
            feedback = 0;
        }
        
        /* Generate modulator output */
        mod_out = opl_operator_calc(mod, feedback << 10, trem, 0);
        
        /* Save feedback */
        c->fb_out[1] = c->fb_out[0];
        c->fb_out[0] = mod_out;
        
        /* Generate carrier output */
        if (c->cnt == 0)
        {
            /* FM: modulator modulates carrier */
            phase_mod = mod_out << 1;
            car_out = opl_operator_calc(car, phase_mod, trem, c->vol_atten);
            output += car_out;
        }
        else
        {
            /* Additive: both go to output */
            car_out = opl_operator_calc(car, 0, trem, c->vol_atten);
            output += mod_out + car_out;
        }
    }
    
    /* Clamp and scale output */
    output >>= 1;
    
    if (output > 32767)
        output = 32767;
    if (output < -32768)
        output = -32768;
    
    return output;
}

/* Generate samples at OUTPUT_RATE using linear interpolation */
static int32_t opl_generate_resampled(void)
{
    int32_t sample;
    
    /* Accumulate fractional position */
    opl.sample_accum += OPL_RATE;
    
    while (opl.sample_accum >= OUTPUT_RATE)
    {
        opl.sample_accum -= OUTPUT_RATE;
        opl.last_sample = opl_generate();
    }
    
    /* Linear interpolation */
    sample = opl.last_sample;
    
    /* Apply music volume */
    sample = (sample * midi.volume) >> 7;
    
    return sample;
}

/* Key on */
static void opl_key_on(int ch)
{
    opl_channel_t *c = &opl.chan[ch];
    int j;
    
    c->key_on = 1;
    
    for (j = 0; j < 2; j++)
    {
        opl_op_t *op = &c->op[j];
        
        op->phase = 0;
        op->env = 511;
        op->eg_state = EG_ATTACK;
        op->key = 1;
    }
    
    c->fb_out[0] = 0;
    c->fb_out[1] = 0;
}

/* Key off */
static void opl_key_off(int ch)
{
    opl_channel_t *c = &opl.chan[ch];
    int j;
    
    c->key_on = 0;
    
    for (j = 0; j < 2; j++)
    {
        opl_op_t *op = &c->op[j];
        
        if (op->eg_state != EG_OFF)
            op->eg_state = EG_RELEASE;
        op->key = 0;
    }
}

/* Set frequency */
static void opl_set_frequency(int ch, int fnum, int block)
{
    opl_channel_t *c = &opl.chan[ch];
    int ksl_base;
    
    c->fnum = fnum;
    c->block = block;
    
    /* Calculate KSL attenuation */
    ksl_base = ksl_table[fnum >> 6];
    ksl_base = (ksl_base << 1) - ((8 - block) << 5);
    if (ksl_base < 0)
        ksl_base = 0;
    
    /* Apply KSL to operators */
    c->op[0].ksl_add = (c->op[0].ksl > 0) ? (ksl_base >> (3 - c->op[0].ksl)) : 0;
    c->op[1].ksl_add = (c->op[1].ksl > 0) ? (ksl_base >> (3 - c->op[1].ksl)) : 0;
}

/* Program operator from GENMIDI data */
static void opl_program_operator(opl_op_t *op, const genmidi_op_t *data)
{
    op->am   = (data->mult_ksr_egt_vib_am >> 7) & 1;
    op->vib  = (data->mult_ksr_egt_vib_am >> 6) & 1;
    op->egt  = (data->mult_ksr_egt_vib_am >> 5) & 1;
    op->ksr  = (data->mult_ksr_egt_vib_am >> 4) & 1;
    op->mult = (data->mult_ksr_egt_vib_am) & 0x0F;
    
    op->ksl  = (data->ksl_tl >> 6) & 3;
    op->tl   = (data->ksl_tl) & 0x3F;
    
    op->ar   = (data->ar_dr >> 4) & 0x0F;
    op->dr   = (data->ar_dr) & 0x0F;
    
    op->sl   = (data->sl_rr >> 4) & 0x0F;
    op->rr   = (data->sl_rr) & 0x0F;
    
    op->ws   = (data->ws) & 0x03;
}

/* Program channel from GENMIDI voice */
static void opl_program_voice(int ch, const genmidi_voice_t *voice)
{
    opl_channel_t *c = &opl.chan[ch];
    
    opl_program_operator(&c->op[0], &voice->mod);
    opl_program_operator(&c->op[1], &voice->car);
    
    c->fb = (voice->fb_cnt >> 1) & 7;
    c->cnt = voice->fb_cnt & 1;
}

/* ==================== GENMIDI Loading ==================== */

static void load_genmidi(void)
{
    int lumpnum;
    const uint8_t *data;
    int len, i, pos, v;
    
    if (genmidi_loaded)
        return;
    
    lumpnum = W_CheckNumForName("GENMIDI");
    if (lumpnum < 0)
    {
        genmidi_loaded = -1;
        return;
    }
    
    data = (const uint8_t *)W_CacheLumpNum(lumpnum, PU_STATIC);
    len = W_LumpLength(lumpnum);
    
    /* Validate header */
    if (len < 8 || memcmp(data, "#OPL_II#", 8) != 0)
    {
        genmidi_loaded = -1;
        return;
    }
    
    pos = 8;
    
    /* Load instruments */
    for (i = 0; i < GENMIDI_NUM_INSTRS && pos + 26 <= len; i++)
    {
        genmidi_instrs[i].flags = data[pos] | (data[pos+1] << 8);
        genmidi_instrs[i].fine_tuning = data[pos+2];
        genmidi_instrs[i].fixed_note = data[pos+3];
        pos += 4;
        
        for (v = 0; v < 2; v++)
        {
            /* Modulator */
            genmidi_instrs[i].voice[v].mod.mult_ksr_egt_vib_am = data[pos++];
            genmidi_instrs[i].voice[v].mod.ksl_tl = data[pos++];
            genmidi_instrs[i].voice[v].mod.ar_dr = data[pos++];
            genmidi_instrs[i].voice[v].mod.sl_rr = data[pos++];
            genmidi_instrs[i].voice[v].mod.ws = data[pos++];
            
            /* Carrier */
            genmidi_instrs[i].voice[v].car.mult_ksr_egt_vib_am = data[pos++];
            genmidi_instrs[i].voice[v].car.ksl_tl = data[pos++];
            genmidi_instrs[i].voice[v].car.ar_dr = data[pos++];
            genmidi_instrs[i].voice[v].car.sl_rr = data[pos++];
            genmidi_instrs[i].voice[v].car.ws = data[pos++];
            
            /* Feedback/Connection */
            genmidi_instrs[i].voice[v].fb_cnt = data[pos++];
        }
    }
    
    genmidi_loaded = 1;
}

/* ==================== MIDI Note Handling ==================== */

static void midi_note_off(int ch, int note);

static int alloc_voice(int midi_ch, int note)
{
    int i;
    int oldest = -1;
    uint32_t oldest_age = 0xFFFFFFFF;
    
    (void)midi_ch;
    (void)note;
    
    /* Find free voice */
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (!midi.voices[i].active)
            return i;
    }
    
    /* Steal oldest voice */
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].age < oldest_age)
        {
            oldest_age = midi.voices[i].age;
            oldest = i;
        }
    }
    
    if (oldest >= 0)
    {
        opl_key_off(oldest);
        midi.voices[oldest].active = 0;
    }
    
    return oldest;
}

static void midi_note_on(int ch, int note, int velocity)
{
    int slot;
    int instr_idx;
    const genmidi_instr_t *instr;
    int real_note;
    int block, fnum;
    int vol_atten;
    int combined_vol;
    
    if (velocity == 0)
    {
        midi_note_off(ch, note);
        return;
    }
    
    if (genmidi_loaded != 1)
        return;
    
    /* Get instrument */
    if (midi.chan[ch].is_drum)
    {
        instr_idx = 128 + (note - 35);
        if (instr_idx < 128 || instr_idx >= GENMIDI_NUM_INSTRS)
            return;
    }
    else
    {
        instr_idx = midi.chan[ch].program;
        if (instr_idx >= 128)
            instr_idx = 0;
    }
    
    instr = &genmidi_instrs[instr_idx];
    
    /* Allocate voice */
    slot = alloc_voice(ch, note);
    if (slot < 0)
        return;
    
    /* Program OPL channel */
    opl_program_voice(slot, &instr->voice[0]);
    
    /* Calculate note */
    if (instr->flags & GENMIDI_FLAG_FIXED)
        real_note = instr->fixed_note;
    else
        real_note = note;
    
    /* Apply fine tuning */
    if (instr->fine_tuning != 128)
    {
        int offset = (int)instr->fine_tuning - 128;
        real_note += offset / 64;
    }
    
    if (real_note < 0)
        real_note = 0;
    if (real_note > 127)
        real_note = 127;
    
    /* Calculate block and F-number */
    block = real_note / 12;
    if (block < 1)
        block = 1;
    if (block > 7)
        block = 7;
    
    fnum = fnumber[real_note % 12];
    
    /* Calculate volume attenuation */
    combined_vol = (velocity * midi.chan[ch].volume * midi.chan[ch].expression) / (127 * 127);
    if (combined_vol > 127)
        combined_vol = 127;
    
    /* Convert to OPL attenuation (0 = loud, 63 = quiet) */
    vol_atten = ((127 - combined_vol) * 48) / 127;
    
    opl.chan[slot].vol_atten = vol_atten;
    
    /* Set frequency and key on */
    opl_set_frequency(slot, fnum, block);
    opl_key_on(slot);
    
    /* Record voice */
    midi.voices[slot].active = 1;
    midi.voices[slot].midi_ch = ch;
    midi.voices[slot].note = note;
    midi.voices[slot].opl_ch = slot;
    midi.voices[slot].velocity = velocity;
    midi.voices[slot].age = midi.voice_age++;
}

static void midi_note_off(int ch, int note)
{
    int i;
    
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].active &&
            midi.voices[i].midi_ch == ch &&
            midi.voices[i].note == note)
        {
            opl_key_off(midi.voices[i].opl_ch);
            midi.voices[i].active = 0;
            break;
        }
    }
}

static void midi_control_change(int ch, int cc, int val)
{
    switch (cc)
    {
    case 7:     /* Volume */
        midi.chan[ch].volume = val;
        break;
    case 10:    /* Pan */
        midi.chan[ch].pan = val;
        break;
    case 11:    /* Expression */
        midi.chan[ch].expression = val;
        break;
    case 121:   /* Reset all controllers */
        midi.chan[ch].volume = 100;
        midi.chan[ch].pan = 64;
        midi.chan[ch].expression = 127;
        midi.chan[ch].pitch_bend = 0;
        break;
    case 120:   /* All sound off */
    case 123:   /* All notes off */
        {
            int i;
            for (i = 0; i < MAX_VOICES; i++)
            {
                if (midi.voices[i].active && midi.voices[i].midi_ch == ch)
                {
                    opl_key_off(midi.voices[i].opl_ch);
                    midi.voices[i].active = 0;
                }
            }
        }
        break;
    }
}

static void midi_program_change(int ch, int prog)
{
    midi.chan[ch].program = prog;
}

/* ==================== MIDI Parser ==================== */

static uint32_t read_var(const uint8_t *data, int *pos, int len)
{
    uint32_t val = 0;
    uint8_t b;
    
    do {
        if (*pos >= len)
            return val;
        b = data[(*pos)++];
        val = (val << 7) | (b & 0x7F);
    } while (b & 0x80);
    
    return val;
}

static int parse_midi(const uint8_t *data, int len)
{
    int pos = 0;
    int tracks, track;
    uint16_t division;
    
    midi.num_events = 0;
    midi.ticks_per_beat = 140;
    midi.us_per_beat = 500000;
    
    /* Check header */
    if (len < 14)
        return 0;
    if (memcmp(data, "MThd", 4) != 0)
        return 0;
    
    pos = 8;
    pos += 2;   /* Skip format */
    tracks = (data[pos] << 8) | data[pos+1];
    pos += 2;
    division = (data[pos] << 8) | data[pos+1];
    pos += 2;
    
    if (division & 0x8000)
    {
        /* SMPTE timing - not supported */
        midi.ticks_per_beat = 140;
    }
    else
    {
        midi.ticks_per_beat = division;
    }
    
    if (midi.ticks_per_beat == 0)
        midi.ticks_per_beat = 140;
    
    /* Parse tracks */
    for (track = 0; track < tracks && pos < len; track++)
    {
        int track_len, track_end;
        uint32_t abs_tick = 0;
        uint8_t running = 0;
        
        if (pos + 8 > len)
            break;
        if (memcmp(&data[pos], "MTrk", 4) != 0)
            break;
        
        track_len = (data[pos+4] << 24) | (data[pos+5] << 16) |
                    (data[pos+6] << 8) | data[pos+7];
        pos += 8;
        track_end = pos + track_len;
        if (track_end > len)
            track_end = len;
        
        while (pos < track_end && midi.num_events < MAX_MIDI_EVENTS)
        {
            uint32_t delta;
            uint8_t status, type, ch;
            midi_event_t *ev;
            
            delta = read_var(data, &pos, track_end);
            abs_tick += delta;
            
            if (pos >= track_end)
                break;
            
            status = data[pos];
            if (status & 0x80)
            {
                running = status;
                pos++;
            }
            else
            {
                status = running;
            }
            
            type = status & 0xF0;
            ch = status & 0x0F;
            
            switch (type)
            {
            case 0x80:  /* Note off */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0x80;
                    ev->channel = ch;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos+1];
                    pos += 2;
                }
                break;
                
            case 0x90:  /* Note on */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0x90;
                    ev->channel = ch;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos+1];
                    pos += 2;
                }
                break;
                
            case 0xA0:  /* Aftertouch */
                pos += 2;
                break;
                
            case 0xB0:  /* Control change */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0xB0;
                    ev->channel = ch;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos+1];
                    pos += 2;
                }
                break;
                
            case 0xC0:  /* Program change */
                if (pos < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0xC0;
                    ev->channel = ch;
                    ev->data1 = data[pos];
                    ev->data2 = 0;
                    pos++;
                }
                break;
                
            case 0xD0:  /* Channel pressure */
                pos++;
                break;
                
            case 0xE0:  /* Pitch bend */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0xE0;
                    ev->channel = ch;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos+1];
                    pos += 2;
                }
                break;
                
            case 0xF0:  /* System/Meta */
                if (status == 0xFF)
                {
                    uint8_t meta;
                    uint32_t meta_len;
                    
                    if (pos >= track_end)
                        goto track_done;
                    
                    meta = data[pos++];
                    meta_len = read_var(data, &pos, track_end);
                    
                    if (meta == 0x51 && meta_len == 3 && pos + 3 <= track_end)
                    {
                        /* Tempo change */
                        midi.us_per_beat = (data[pos] << 16) |
                                           (data[pos+1] << 8) |
                                           data[pos+2];
                        if (midi.us_per_beat == 0)
                            midi.us_per_beat = 500000;
                    }
                    else if (meta == 0x2F)
                    {
                        /* End of track */
                        pos += meta_len;
                        goto track_done;
                    }
                    
                    pos += meta_len;
                }
                else if (status == 0xF0 || status == 0xF7)
                {
                    uint32_t sysex_len = read_var(data, &pos, track_end);
                    pos += sysex_len;
                }
                break;
            }
        }
        
track_done:
        pos = track_end;
    }
    
    /* Calculate timing */
    if (midi.ticks_per_beat > 0 && midi.us_per_beat > 0)
    {
        double secs_per_tick = (double)midi.us_per_beat /
                               (double)midi.ticks_per_beat / 1000000.0;
        midi.samples_per_tick = secs_per_tick * OUTPUT_RATE;
    }
    else
    {
        midi.samples_per_tick = OUTPUT_RATE / 140.0;
    }
    
    return midi.num_events;
}

static void sort_events(void)
{
    int i, j;
    midi_event_t tmp;
    
    for (i = 1; i < midi.num_events; i++)
    {
        tmp = midi.events[i];
        j = i - 1;
        while (j >= 0 && midi.events[j].tick > tmp.tick)
        {
            midi.events[j+1] = midi.events[j];
            j--;
        }
        midi.events[j+1] = tmp;
    }
}

static void process_event(midi_event_t *ev)
{
    uint8_t type = ev->type & 0xF0;
    int ch = ev->channel;
    
    switch (type)
    {
    case 0x90:
        midi_note_on(ch, ev->data1, ev->data2);
        break;
    case 0x80:
        midi_note_off(ch, ev->data1);
        break;
    case 0xB0:
        midi_control_change(ch, ev->data1, ev->data2);
        break;
    case 0xC0:
        midi_program_change(ch, ev->data1);
        break;
    }
}

static void midi_advance(int samples)
{
    int i;
    
    if (!midi.playing || midi.num_events == 0)
        return;
    
    midi.tick_accum += samples;
    
    while (midi.tick_accum >= midi.samples_per_tick)
    {
        midi.tick_accum -= midi.samples_per_tick;
        midi.current_tick++;
        
        while (midi.current_event < midi.num_events &&
               midi.events[midi.current_event].tick <= midi.current_tick)
        {
            process_event(&midi.events[midi.current_event]);
            midi.current_event++;
        }
        
        if (midi.current_event >= midi.num_events)
        {
            if (midi.looping)
            {
                midi.current_event = 0;
                midi.current_tick = 0;
                midi.tick_accum = 0;
                
                for (i = 0; i < MAX_VOICES; i++)
                {
                    if (midi.voices[i].active)
                        opl_key_off(midi.voices[i].opl_ch);
                    midi.voices[i].active = 0;
                }
                
                for (i = 0; i < MIDI_CHANNELS; i++)
                {
                    midi.chan[i].volume = 100;
                    midi.chan[i].expression = 127;
                }
            }
            else
            {
                midi.playing = 0;
            }
            break;
        }
    }
}

/* ==================== Audio Thread ==================== */

static int audio_thread(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    
    while (snd_running)
    {
        int s, c;
        
        memset(mix_buffer, 0, sizeof(mix_buffer));
        
        /* Advance MIDI */
        if (midi.playing)
            midi_advance(MIX_SAMPLES);
        
        /* Generate samples */
        for (s = 0; s < MIX_SAMPLES; s++)
        {
            int32_t left = 0, right = 0;
            
            /* Mix SFX */
            for (c = 0; c < SND_CHANNELS; c++)
            {
                sfx_channel_t *ch = &sfx_channels[c];
                int idx;
                int32_t sample;
                int lv, rv;
                
                if (!ch->active)
                    continue;
                
                idx = ch->pos >> 16;
                if (idx >= ch->length)
                {
                    ch->active = 0;
                    continue;
                }
                
                sample = ((int32_t)ch->data[idx] - 128) << 8;
                ch->pos += ch->step;
                
                sample = (sample * ch->vol * sfx_volume) / (127 * 127);
                sample <<= 1;
                
                lv = 255 - ch->sep;
                rv = ch->sep;
                
                left += (sample * lv) >> 8;
                right += (sample * rv) >> 8;
            }
            
            /* Add music */
            if (midi.playing)
            {
                int32_t music = opl_generate_resampled();
                left += music;
                right += music;
            }
            
            /* Clamp */
            if (left > 32767) left = 32767;
            if (left < -32768) left = -32768;
            if (right > 32767) right = 32767;
            if (right < -32768) right = -32768;
            
            mix_buffer[s*2] = left;
            mix_buffer[s*2+1] = right;
        }
        
        sceAudioOutputBlocking(psp_audio_ch, PSP_AUDIO_VOLUME_MAX, mix_buffer);
    }
    
    return 0;
}

/* ==================== Sound Interface ==================== */

void I_InitSound(boolean use_sfx_prefix)
{
    int i;
    
    (void)use_sfx_prefix;
    
    memset(sfx_channels, 0, sizeof(sfx_channels));
    
    if (!sfx_cache_init)
    {
        memset(sfx_cache, 0, sizeof(sfx_cache));
        sfx_cache_init = 1;
    }
    
    sfx_volume = 127;
    
    opl_reset();
    
    memset(&midi, 0, sizeof(midi));
    midi.volume = 127;
    midi.us_per_beat = 500000;
    
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        midi.chan[i].volume = 100;
        midi.chan[i].pan = 64;
        midi.chan[i].expression = 127;
        midi.chan[i].is_drum = (i == 9) ? 1 : 0;
    }
    
    psp_audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                      MIX_SAMPLES,
                                      PSP_AUDIO_FORMAT_STEREO);
    if (psp_audio_ch < 0)
        return;
    
    snd_running = 1;
    snd_thread_id = sceKernelCreateThread("audio", audio_thread,
                                           0x12, 0x10000,
                                           PSP_THREAD_ATTR_USER, NULL);
    if (snd_thread_id >= 0)
        sceKernelStartThread(snd_thread_id, 0, NULL);
}

void I_ShutdownSound(void)
{
    snd_running = 0;
    
    if (snd_thread_id >= 0)
    {
        sceKernelWaitThreadEnd(snd_thread_id, NULL);
        sceKernelDeleteThread(snd_thread_id);
        snd_thread_id = -1;
    }
    
    if (psp_audio_ch >= 0)
    {
        sceAudioChRelease(psp_audio_ch);
        psp_audio_ch = -1;
    }
    
    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
    }
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[16];
    if (sfx->link)
        sfx = sfx->link;
    snprintf(name, sizeof(name), "ds%s", sfx->name);
    return W_GetNumForName(name);
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    void *lump;
    unsigned char *data;
    int lumpnum, rate, length, slot, handle;
    
    if (!sfxinfo || !snd_running)
        return -1;
    
    lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0 || lumpnum >= 2048)
        return -1;
    
    if (!sfx_cache[lumpnum])
        sfx_cache[lumpnum] = W_CacheLumpNum(lumpnum, PU_STATIC);
    
    lump = sfx_cache[lumpnum];
    if (!lump)
        return -1;
    
    data = (unsigned char *)lump;
    
    /* Check format */
    if ((data[0] | (data[1] << 8)) != 3)
        return -1;
    
    rate = data[2] | (data[3] << 8);
    length = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);
    
    if (rate == 0)
        rate = 11025;
    if (length <= 8)
        return -1;
    
    length -= 8;
    
    /* Find slot */
    if (channel >= 0 && channel < SND_CHANNELS)
    {
        slot = channel;
    }
    else
    {
        int i;
        slot = 0;
        for (i = 0; i < SND_CHANNELS; i++)
        {
            if (!sfx_channels[i].active)
            {
                slot = i;
                break;
            }
        }
    }
    
    handle = next_handle++;
    
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0) sep = 0;
    if (sep > 255) sep = 255;
    
    sfx_channels[slot].data = data + 8;
    sfx_channels[slot].length = length;
    sfx_channels[slot].pos = 0;
    sfx_channels[slot].step = ((uint32_t)rate << 16) / OUTPUT_RATE;
    sfx_channels[slot].vol = vol;
    sfx_channels[slot].sep = sep;
    sfx_channels[slot].handle = handle;
    sfx_channels[slot].active = 1;
    
    return handle;
}

void I_StopSound(int handle)
{
    int i;
    for (i = 0; i < SND_CHANNELS; i++)
    {
        if (sfx_channels[i].active && sfx_channels[i].handle == handle)
        {
            sfx_channels[i].active = 0;
            break;
        }
    }
}

boolean I_SoundIsPlaying(int handle)
{
    int i;
    for (i = 0; i < SND_CHANNELS; i++)
    {
        if (sfx_channels[i].active && sfx_channels[i].handle == handle)
            return 1;
    }
    return 0;
}

void I_UpdateSound(void)
{
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel >= 0 && channel < SND_CHANNELS && sfx_channels[channel].active)
    {
        if (vol < 0) vol = 0;
        if (vol > 127) vol = 127;
        if (sep < 0) sep = 0;
        if (sep > 255) sep = 255;
        sfx_channels[channel].vol = vol;
        sfx_channels[channel].sep = sep;
    }
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds;
    (void)num_sounds;
}

void I_BindSoundVariables(void)
{
}

void I_SetSfxVolume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    sfx_volume = vol;
}

/* ==================== Music Interface ==================== */

void I_InitMusic(void)
{
    load_genmidi();
}

void I_ShutdownMusic(void)
{
    I_StopSong();
    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
    }
}

void I_SetMusicVolume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    midi.volume = vol;
}

void I_PauseSong(void)
{
    midi.playing = 0;
}

void I_ResumeSong(void)
{
    if (midi.num_events > 0)
        midi.playing = 1;
}

void I_StopSong(void)
{
    int i;
    
    midi.playing = 0;
    midi.current_event = 0;
    midi.current_tick = 0;
    midi.tick_accum = 0;
    
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].active)
            opl_key_off(midi.voices[i].opl_ch);
        midi.voices[i].active = 0;
    }
    
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        midi.chan[i].volume = 100;
        midi.chan[i].pan = 64;
        midi.chan[i].expression = 127;
        midi.chan[i].program = 0;
        midi.chan[i].pitch_bend = 0;
    }
}

boolean I_MusicIsPlaying(void)
{
    return midi.playing ? 1 : 0;
}

void *I_RegisterSong(void *data, int len)
{
    MEMFILE *in = NULL;
    MEMFILE *out = NULL;
    void *buf = NULL;
    size_t buflen = 0;
    
    if (!data || len <= 0)
        return NULL;
    
    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
        midi.midi_data_len = 0;
    }
    
    if (!genmidi_loaded)
        load_genmidi();
    
    /* Check if already MIDI */
    if (len >= 4 && memcmp(data, "MThd", 4) == 0)
    {
        midi.midi_data = malloc(len);
        if (!midi.midi_data)
            return NULL;
        memcpy(midi.midi_data, data, len);
        midi.midi_data_len = len;
    }
    else
    {
        /* Convert MUS to MIDI */
        in = mem_fopen_read(data, len);
        if (!in)
            return NULL;
        
        out = mem_fopen_write();
        if (!out)
        {
            mem_fclose(in);
            return NULL;
        }
        
        if (mus2mid(in, out) != 0)
        {
            mem_fclose(in);
            mem_fclose(out);
            return NULL;
        }
        
        mem_get_buf(out, &buf, &buflen);
        if (!buf || buflen == 0)
        {
            mem_fclose(in);
            mem_fclose(out);
            return NULL;
        }
        
        midi.midi_data = malloc(buflen);
        if (!midi.midi_data)
        {
            mem_fclose(in);
            mem_fclose(out);
            return NULL;
        }
        
        memcpy(midi.midi_data, buf, buflen);
        midi.midi_data_len = buflen;
        
        mem_fclose(in);
        mem_fclose(out);
    }
    
    /* Parse MIDI */
    if (parse_midi((const uint8_t *)midi.midi_data, midi.midi_data_len) <= 0)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
        midi.midi_data_len = 0;
        return NULL;
    }
    
    sort_events();
    
    return (void *)1;
}

void I_UnRegisterSong(void *handle)
{
    (void)handle;
    
    I_StopSong();
    
    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
        midi.midi_data_len = 0;
    }
    
    midi.num_events = 0;
}

void I_PlaySong(void *handle, boolean looping)
{
    int i;
    
    (void)handle;
    
    if (midi.num_events == 0)
        return;
    
    midi.current_event = 0;
    midi.current_tick = 0;
    midi.tick_accum = 0;
    midi.looping = looping ? 1 : 0;
    midi.voice_age = 0;
    
    /* Recalculate timing */
    if (midi.ticks_per_beat > 0 && midi.us_per_beat > 0)
    {
        double secs_per_tick = (double)midi.us_per_beat /
                               (double)midi.ticks_per_beat / 1000000.0;
        midi.samples_per_tick = secs_per_tick * OUTPUT_RATE;
    }
    
    /* Reset voices */
    for (i = 0; i < MAX_VOICES; i++)
        midi.voices[i].active = 0;
    
    /* Reset OPL */
    opl_reset();
    
    /* Reset channels */
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        midi.chan[i].volume = 100;
        midi.chan[i].pan = 64;
        midi.chan[i].expression = 127;
        midi.chan[i].pitch_bend = 0;
        midi.chan[i].is_drum = (i == 9) ? 1 : 0;
    }
    
    midi.playing = 1;
}
