/*
 * psp_sound.c - Audio per Chex Quest PSP
 * Emulazione OPL2 con sintesi FM per musica MIDI
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

#define EG_ATTACK   0
#define EG_DECAY    1
#define EG_SUSTAIN  2
#define EG_RELEASE  3
#define EG_OFF      4

/* ==================== OPL2 Tables ==================== */

/* Log-sin table: logsin[i] = round(-log2(sin((i+0.5)/256 * pi/2)) * 256) */
static const uint16_t logsin_table[256] = {
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137,
    1091, 1050, 1013,  979,  949,  920,  894,  869,
     846,  825,  804,  785,  767,  749,  732,  717,
     701,  687,  672,  659,  646,  633,  621,  609,
     598,  587,  576,  566,  556,  546,  536,  527,
     518,  509,  501,  492,  484,  476,  468,  461,
     453,  446,  439,  432,  425,  418,  411,  405,
     399,  392,  386,  380,  375,  369,  363,  358,
     352,  347,  341,  336,  331,  326,  321,  316,
     311,  307,  302,  297,  293,  289,  284,  280,
     276,  271,  267,  263,  259,  255,  251,  248,
     244,  240,  236,  233,  229,  226,  222,  219,
     215,  212,  209,  205,  202,  199,  196,  193,
     190,  187,  184,  181,  178,  175,  172,  169,
     167,  164,  161,  159,  156,  153,  151,  148,
     146,  143,  141,  138,  136,  134,  131,  129,
     127,  125,  122,  120,  118,  116,  114,  112,
     110,  108,  106,  104,  102,  100,   98,   96,
      94,   92,   91,   89,   87,   85,   84,   82,
      80,   79,   77,   76,   74,   72,   71,   69,
      68,   66,   65,   64,   62,   61,   59,   58,
      57,   55,   54,   53,   52,   50,   49,   48,
      47,   46,   45,   43,   42,   41,   40,   39,
      38,   37,   36,   35,   34,   33,   32,   31,
      30,   29,   28,   27,   27,   26,   25,   24,
      23,   23,   22,   21,   20,   20,   19,   18,
      17,   17,   16,   15,   15,   14,   13,   13,
      12,   12,   11,   10,   10,    9,    9,    8,
       8,    7,    7,    6,    6,    5,    5,    4,
       4,    4,    3,    3,    2,    2,    2,    1,
       1,    1,    0,    0,    0,    0,    0,    0,
       0,    0,    0,    0,    0,    0,    0,    0
};

/* Exponential table: exp[i] = round(2^((255-i)/256) * 1024) */
static const uint16_t exp_table[256] = {
    1018, 1013, 1007, 1002,  996,  991,  986,  980,
     975,  969,  964,  959,  953,  948,  943,  938,
     932,  927,  922,  917,  912,  906,  901,  896,
     891,  886,  881,  876,  871,  866,  861,  856,
     851,  846,  841,  837,  832,  827,  822,  817,
     813,  808,  803,  798,  794,  789,  784,  780,
     775,  771,  766,  762,  757,  752,  748,  744,
     739,  735,  730,  726,  722,  717,  713,  709,
     704,  700,  696,  692,  688,  683,  679,  675,
     671,  667,  663,  659,  655,  651,  647,  643,
     639,  635,  631,  627,  623,  619,  616,  612,
     608,  604,  600,  597,  593,  589,  585,  582,
     578,  574,  571,  567,  564,  560,  556,  553,
     549,  546,  542,  539,  535,  532,  528,  525,
     521,  518,  515,  511,  508,  505,  501,  498,
     495,  491,  488,  485,  482,  478,  475,  472,
     469,  466,  462,  459,  456,  453,  450,  447,
     444,  441,  438,  435,  432,  429,  426,  423,
     420,  417,  414,  411,  408,  405,  402,  400,
     397,  394,  391,  388,  385,  383,  380,  377,
     374,  372,  369,  366,  363,  361,  358,  355,
     353,  350,  347,  345,  342,  340,  337,  334,
     332,  329,  327,  324,  322,  319,  317,  314,
     312,  309,  307,  304,  302,  299,  297,  295,
     292,  290,  288,  285,  283,  280,  278,  276,
     274,  271,  269,  267,  265,  262,  260,  258,
     256,  253,  251,  249,  247,  245,  243,  241,
     238,  236,  234,  232,  230,  228,  226,  224,
     222,  220,  218,  216,  214,  212,  210,  208,
     206,  204,  202,  200,  198,  196,  194,  193,
     191,  189,  187,  185,  183,  182,  180,  178,
     176,  174,  173,  171,  169,  167,  166,  164
};

/* Frequency multiplier (x2): 0.5,1,2,3,4,5,6,7,8,9,10,10,12,12,15,15 */
static const uint8_t mt[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

/* KSL table */
static const int32_t ksl_tab[4] = { 0, 1, 2, 4 }; /* shift values for KSL */

/* F-Number table for each semitone (calculated for OPL2) */
static const uint16_t fnumber_table[12] = {
    0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,
    0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287
};

/* Envelope rate increments - based on rate and sub-counter
 * For attack: env_inc_attack[rate>>2]
 * For decay/release: rate determines shift amount
 */

/* ==================== OPL2 Structures ==================== */

typedef struct {
    uint8_t     mult;
    uint8_t     ksr;
    uint8_t     egt;     /* 1 = sustained tone */
    uint8_t     vib;
    uint8_t     am;
    uint8_t     tl;      /* 0-63 total level attenuation */
    uint8_t     ksl;
    uint8_t     ar;      /* 0-15 */
    uint8_t     dr;      /* 0-15 */
    uint8_t     sl;      /* 0-15 */
    uint8_t     rr;      /* 0-15 */
    uint8_t     ws;

    uint32_t    phase;
    uint32_t    phase_inc;
    int32_t     env;          /* 0 = max volume, 511 = silent */
    uint8_t     eg_state;
    uint8_t     key;
    int32_t     ksl_atten;

    /* Envelope sub-counter for proper rate timing */
    uint32_t    eg_counter;
    uint32_t    eg_period;
} opl_op_t;

typedef struct {
    opl_op_t    op[2];        /* [0]=modulator [1]=carrier */
    uint16_t    fnum;
    uint8_t     block;
    uint8_t     fb;
    uint8_t     cnt;          /* 0=FM, 1=additive */
    uint8_t     key_on;
    int32_t     fb_out[2];
    int32_t     vol_atten;    /* volume attenuation from MIDI velocity */
} opl_channel_t;

typedef struct {
    opl_channel_t   chan[OPL_NUM_CHANNELS];
    uint8_t         trem_depth;
    uint8_t         vib_depth;
    uint32_t        trem_counter;
    uint32_t        vib_counter;
    uint32_t        eg_timer;
    uint32_t        sample_count;

    /* Resampling */
    uint32_t        resamp_frac;
    int32_t         prev_sample;
    int32_t         cur_sample;
} opl_chip_t;

static opl_chip_t opl;

/* ==================== GENMIDI ==================== */

/*
 * GENMIDI lump layout (Doom format):
 * 8 bytes: "#OPL_II#"
 * Then 175 instruments, each 36 bytes:
 *   uint16 flags
 *   uint8  fine_tuning
 *   uint8  fixed_note
 *   Voice 0 (16 bytes):
 *     Modulator: am_vib_egt_ksr_mult, ksl_tl, ar_dr, sl_rr, ws (5 bytes)
 *     Carrier:   am_vib_egt_ksr_mult, ksl_tl, ar_dr, sl_rr, ws (5 bytes)
 *     uint16 mod_offset (unused by us)
 *     uint16 car_offset (unused by us)
 *     uint8  fb_cnt
 *     uint8  padding
 *   Voice 1 (16 bytes): same layout
 *
 * Actually the real Doom GENMIDI format per instrument is:
 *   uint16 flags
 *   uint8  fine_tuning
 *   uint8  fixed_note
 *   Voice[0]:
 *     uint8 modchar (AM/VIB/EGT/KSR/MULT)
 *     uint8 carchar
 *     uint8 modscal (KSL/TL)
 *     uint8 carscal
 *     uint8 modad   (AR/DR)
 *     uint8 carad
 *     uint8 modsr   (SL/RR)
 *     uint8 carsr
 *     uint8 modwav  (WS)
 *     uint8 carwav
 *     uint8 feedback (FB/CNT)
 *     uint8 unused
 *   Voice[1]: same 12 bytes
 * Total = 4 + 12 + 12 = 28 bytes... but official is 36.
 *
 * Let me check: the actual size is:
 * header: 2+1+1 = 4 bytes
 * per voice: 12 bytes (modchar, carchar, modscal, carscal, modad, carad,
 *            modsr, carsr, modwav, carwav, fb_cnt, unused)
 * 2 voices: 24 bytes
 * instrument name: 32 bytes (after all instruments)
 * So per instrument data: 4 + 24 = 28 bytes? No...
 *
 * Checking Chocolate Doom source: each instrument is:
 *   uint16 flags
 *   uint8  fine_tuning  
 *   uint8  fixed_note
 *   voice[2], each voice is:
 *     opl_voice_data (11 bytes):
 *       uint8 mdChar, uint8 cChar, uint8 mdScal, uint8 cScal,
 *       uint8 mdAd, uint8 cAd, uint8 mdSr, uint8 cSr,
 *       uint8 mdWav, uint8 cWav, uint8 fbConn
 *     int16 offset (note offset)
 * So voice = 11 + 2 = 13 bytes
 * Instrument = 4 + 13*2 = 30 bytes? 
 *
 * Actually from genmidi.h in Chocolate Doom:
 * sizeof(genmidi_op_t) = 5 bytes (packed)
 * sizeof(genmidi_voice_t) = 2*5 + 2 + 2 + 1 = 15 bytes... no
 *
 * Let's just look at the raw data more carefully.
 * From Chocolate Doom genmidi.h:
 *
 * typedef struct {
 *     byte tremolo;
 *     byte attack;
 *     byte sustain;
 *     byte waveform;
 *     byte scale;
 *     byte level;
 * } PACKED genmidi_op_t;  // 6 bytes
 *
 * typedef struct {
 *     genmidi_op_t modulator;   // 6 bytes
 *     byte feedback;            // 1 byte
 *     genmidi_op_t carrier;     // 6 bytes
 *     byte unused;              // 1 byte
 *     short base_note_offset;   // 2 bytes
 * } PACKED genmidi_voice_t;  // 16 bytes
 *
 * typedef struct {
 *     unsigned short flags;     // 2 bytes
 *     byte fine_tuning;         // 1 byte
 *     byte fixed_note;          // 1 byte
 *     genmidi_voice_t voices[2]; // 32 bytes
 * } PACKED genmidi_instr_t;  // 36 bytes
 *
 * OK! So per operator: tremolo, attack, sustain, waveform, scale, level = 6 bytes
 * where:
 *   tremolo = AM/VIB/EGT/KSR/MULT byte (reg 0x20)
 *   attack  = AR/DR byte (reg 0x60)
 *   sustain = SL/RR byte (reg 0x80)
 *   waveform = WS byte (reg 0xE0)
 *   scale   = KSL/TL byte (reg 0x40)
 *   level   = same? No, that's the output level
 *
 * Wait, from the actual source:
 *   tremolo = reg 0x20 (AM, VIB, EGT, KSR, MULT)
 *   attack  = reg 0x60 (AR, DR)
 *   sustain = reg 0x80 (SL, RR)
 *   waveform= reg 0xE0 (WS)
 *   scale   = reg 0x40 (KSL, TL)
 *   level   = TL override for volume control
 */

/* Match Chocolate Doom's GENMIDI layout exactly */
typedef struct {
    uint8_t tremolo;    /* AM/VIB/EGT/KSR/MULT - reg 0x20 */
    uint8_t attack;     /* AR/DR - reg 0x60 */
    uint8_t sustain;    /* SL/RR - reg 0x80 */
    uint8_t waveform;   /* WS - reg 0xE0 */
    uint8_t scale;      /* KSL/TL - reg 0x40 */
    uint8_t level;      /* Output level */
} __attribute__((packed)) genmidi_op_t;

typedef struct {
    genmidi_op_t    modulator;      /* 6 bytes */
    uint8_t         feedback;       /* FB/CNT - 1 byte */
    genmidi_op_t    carrier;        /* 6 bytes */
    uint8_t         unused;         /* 1 byte */
    int16_t         base_note_offset; /* 2 bytes */
} __attribute__((packed)) genmidi_voice_t;

typedef struct {
    uint16_t        flags;          /* 2 bytes */
    uint8_t         fine_tuning;    /* 1 byte */
    uint8_t         fixed_note;     /* 1 byte */
    genmidi_voice_t voices[2];      /* 32 bytes */
} __attribute__((packed)) genmidi_instr_t;

#define GENMIDI_NUM_INSTRS  175
#define GENMIDI_FLAG_FIXED  0x0001
#define GENMIDI_FLAG_2VOICE 0x0004

static genmidi_instr_t *genmidi_instrs = NULL;
static int genmidi_loaded = 0;

/* ==================== MIDI Sequencer ==================== */

#define MIDI_CHANNELS       16
#define MAX_MIDI_EVENTS     65536
#define MAX_VOICES          9

#define MIDI_EV_NOTEON      0x90
#define MIDI_EV_NOTEOFF     0x80
#define MIDI_EV_CONTROL     0xB0
#define MIDI_EV_PROGRAM     0xC0
#define MIDI_EV_PITCHBEND   0xE0
#define MIDI_EV_TEMPO       0x01  /* internal marker */

typedef struct {
    uint32_t    tick;
    uint8_t     type;
    uint8_t     channel;
    uint8_t     data1;
    uint8_t     data2;
    uint32_t    tempo;      /* for tempo events */
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
    int16_t     note_offset;  /* from GENMIDI base_note_offset */
} voice_t;

typedef struct {
    midi_event_t   *events;
    int             num_events;
    int             current_event;
    uint16_t        ticks_per_beat;
    uint32_t        us_per_beat;

    /* Fixed-point timing: accumulator counts in units of
     * (us_per_beat / ticks_per_beat) microseconds per tick
     * We track microsecond fractions using 64-bit math */
    double          samples_per_tick;
    double          tick_accum;

    uint32_t        current_tick;
    uint32_t        voice_age;

    midi_channel_t  chan[MIDI_CHANNELS];
    voice_t         voices[MAX_VOICES];

    int             playing;
    int             looping;
    int             volume;  /* 0-127 */

    void           *midi_data;
    int             midi_data_len;
} midi_state_t;

static midi_state_t midi;

/* ==================== SFX ==================== */

typedef struct {
    const uint8_t  *data;
    int             length;
    uint32_t        pos;    /* 16.16 fixed point */
    uint32_t        step;   /* 16.16 fixed point */
    int             vol;
    int             sep;
    int             handle;
    int             active;
} sfx_channel_t;

static sfx_channel_t sfx_channels[SND_CHANNELS];
static int           psp_audio_ch  = -1;
static volatile int  snd_running   = 0;
static SceUID        snd_thread_id = -1;
static int           next_handle   = 1;
static int           sfx_volume    = 127;

static void         *sfx_cache[2048];
static int           sfx_cache_init = 0;

static int16_t __attribute__((aligned(64))) mix_buffer[MIX_SAMPLES * 2];

static SceUID sfx_sema = -1;

static void sfx_lock(void)
{
    if (sfx_sema >= 0)
        sceKernelWaitSema(sfx_sema, 1, NULL);
}

static void sfx_unlock(void)
{
    if (sfx_sema >= 0)
        sceKernelSignalSema(sfx_sema, 1);
}

/* ==================== OPL2 Implementation ==================== */

static void opl_reset(void)
{
    int i, j;
    memset(&opl, 0, sizeof(opl));
    opl.trem_depth = 0;
    opl.vib_depth  = 0;

    for (i = 0; i < OPL_NUM_CHANNELS; i++)
    {
        for (j = 0; j < 2; j++)
        {
            opl.chan[i].op[j].env      = 511;
            opl.chan[i].op[j].eg_state = EG_OFF;
            opl.chan[i].op[j].eg_counter = 0;
        }
    }
}

/*
 * Envelope calculation.
 * The OPL2 envelope operates at OPL_RATE/4 (about 12429 Hz).
 * We use a counter-based approach: every N OPL samples, the envelope steps.
 * The effective rate determines N.
 */

static int eg_effective_rate(int rate, int ksr, int block, int fnum)
{
    int rof, eff;
    if (rate == 0) return 0;

    rof = (block << 1) | ((fnum >> 9) & 1);
    if (!ksr) rof >>= 2;

    eff = (rate << 2) + rof;
    if (eff > 63) eff = 63;
    return eff;
}

/* Process envelope for one OPL sample */
static void opl_env_step(opl_op_t *op, int block, int fnum)
{
    int eff, shift, period;

    switch (op->eg_state)
    {
    case EG_ATTACK:
        if (op->ar == 0) return;
        if (op->ar >= 15)
        {
            op->env = 0;
            op->eg_state = EG_DECAY;
            return;
        }

        eff = eg_effective_rate(op->ar, op->ksr, block, fnum);

        /* Attack: exponential curve
         * Period between steps depends on effective rate */
        shift = 13 - (eff >> 2);
        if (shift < 0) shift = 0;
        period = 1 << shift;

        op->eg_counter++;
        if (op->eg_counter >= (uint32_t)period)
        {
            op->eg_counter = 0;
            /* Exponential attack: subtract proportional to current level */
            op->env -= ((op->env >> 3) + 1);
            if (op->env <= 0)
            {
                op->env = 0;
                op->eg_state = EG_DECAY;
            }
        }
        break;

    case EG_DECAY:
        if (op->dr == 0)
        {
            op->env = (int32_t)op->sl << 5;  /* sl * 32: 0-480 in steps of 32 */
            op->eg_state = EG_SUSTAIN;
            return;
        }

        eff = eg_effective_rate(op->dr, op->ksr, block, fnum);
        shift = 13 - (eff >> 2);
        if (shift < 0) shift = 0;
        period = 1 << shift;

        op->eg_counter++;
        if (op->eg_counter >= (uint32_t)period)
        {
            op->eg_counter = 0;
            op->env += 1;
            if (op->env >= (int32_t)(op->sl << 5))
            {
                op->env = (int32_t)op->sl << 5;
                op->eg_state = EG_SUSTAIN;
            }
        }
        break;

    case EG_SUSTAIN:
        if (op->egt) return;  /* Sustained: hold level */

        /* Not sustained: decay to silence using RR */
        if (op->rr == 0) return;

        eff = eg_effective_rate(op->rr, op->ksr, block, fnum);
        shift = 13 - (eff >> 2);
        if (shift < 0) shift = 0;
        period = 1 << shift;

        op->eg_counter++;
        if (op->eg_counter >= (uint32_t)period)
        {
            op->eg_counter = 0;
            op->env += 1;
            if (op->env >= 511)
            {
                op->env = 511;
                op->eg_state = EG_OFF;
            }
        }
        break;

    case EG_RELEASE:
        {
            /* Release rate: use rr*4 for faster release to sound natural */
            int rel_rate = op->rr;
            if (rel_rate == 0) rel_rate = 1;  /* Always release eventually */

            eff = eg_effective_rate(rel_rate, op->ksr, block, fnum);
            shift = 13 - (eff >> 2);
            if (shift < 0) shift = 0;
            period = 1 << shift;

            op->eg_counter++;
            if (op->eg_counter >= (uint32_t)period)
            {
                op->eg_counter = 0;
                op->env += 2;  /* Slightly faster release */
                if (op->env >= 511)
                {
                    op->env = 511;
                    op->eg_state = EG_OFF;
                }
            }
        }
        break;

    case EG_OFF:
    default:
        op->env = 511;
        break;
    }
}

/* Calculate operator output */
static int32_t opl_calc_op(opl_op_t *op, int32_t phase_mod,
                            int32_t trem_val, int32_t vol_atten)
{
    uint32_t phase;
    int32_t  atten, output;
    uint16_t log_val;
    int      neg, exp_idx, exp_sh;

    /* 10-bit phase from accumulator (bits 19..10) */
    phase = ((op->phase >> 10) + (uint32_t)(phase_mod >> 10)) & 0x3FF;

    /* Total attenuation */
    atten = op->env + (op->tl << 3) + op->ksl_atten + vol_atten;
    if (op->am)
        atten += trem_val;

    if (atten >= 511)
        return 0;
    if (atten < 0)
        atten = 0;

    /* Waveform lookup */
    neg = 0;

    switch (op->ws & 3)
    {
    case 0: /* Sine */
        neg = (phase & 0x200) ? 1 : 0;
        if (phase & 0x100)
            log_val = logsin_table[(~phase) & 0xFF];
        else
            log_val = logsin_table[phase & 0xFF];
        break;

    case 1: /* Half sine */
        if (phase & 0x200)
            return 0;
        if (phase & 0x100)
            log_val = logsin_table[(~phase) & 0xFF];
        else
            log_val = logsin_table[phase & 0xFF];
        break;

    case 2: /* Abs sine */
        if (phase & 0x100)
            log_val = logsin_table[(~phase) & 0xFF];
        else
            log_val = logsin_table[phase & 0xFF];
        break;

    case 3: /* Quarter sine */
    default:
        if (phase & 0x100)
            return 0;
        log_val = logsin_table[phase & 0xFF];
        break;
    }

    /* Add attenuation in log domain */
    log_val += (uint16_t)(atten << 3);

    /* Convert log to linear */
    exp_idx = log_val & 0xFF;
    exp_sh  = log_val >> 8;

    if (exp_sh > 13)
        return 0;

    output = (exp_table[exp_idx] << 1) >> exp_sh;

    return neg ? -output : output;
}

/* Update phase accumulator */
static void opl_calc_phase(opl_op_t *op, int fnum, int block, int32_t vib_val)
{
    int fn = fnum;
    uint32_t freq;

    if (op->vib)
        fn += vib_val;

    if (fn < 0)    fn = 0;
    if (fn > 1023) fn = 1023;

    /* phase_inc = fnum * 2^block * mult_factor
     * mt[] already contains multiplier * 2 */
    freq = (uint32_t)fn << block;
    op->phase_inc = freq * mt[op->mult];
    op->phase += op->phase_inc;
}

/* Generate one OPL sample */
static int32_t opl_gen_sample(void)
{
    int     ch;
    int32_t out = 0;
    int32_t trem_val, vib_val;
    uint32_t tv;

    /* Tremolo LFO: ~3.7 Hz triangle, 0 to max_depth */
    opl.trem_counter++;
    tv = (opl.trem_counter >> 6) & 0x7F;
    if (tv > 63) tv = 127 - tv;
    trem_val = opl.trem_depth ? (tv >> 1) : (tv >> 3);

    /* Vibrato LFO: ~6.1 Hz */
    opl.vib_counter++;
    tv = (opl.vib_counter >> 5) & 0x3F;
    if (tv > 31) tv = 63 - tv;
    vib_val = (int32_t)tv - 16;
    if (!opl.vib_depth)
        vib_val >>= 1;

    for (ch = 0; ch < OPL_NUM_CHANNELS; ch++)
    {
        opl_channel_t *c   = &opl.chan[ch];
        opl_op_t      *mod = &c->op[0];
        opl_op_t      *car = &c->op[1];
        int32_t mod_out, car_out, fb;

        /* Skip silent channels */
        if (mod->eg_state == EG_OFF && car->eg_state == EG_OFF)
            continue;

        /* Envelope step */
        opl_env_step(mod, c->block, c->fnum);
        opl_env_step(car, c->block, c->fnum);

        /* Phase step */
        opl_calc_phase(mod, c->fnum, c->block, vib_val);
        opl_calc_phase(car, c->fnum, c->block, vib_val);

        /* Feedback */
        if (c->fb > 0)
            fb = (c->fb_out[0] + c->fb_out[1]) >> (9 - c->fb);
        else
            fb = 0;

        /* Modulator output */
        mod_out = opl_calc_op(mod, fb << 10, trem_val, 0);

        c->fb_out[1] = c->fb_out[0];
        c->fb_out[0] = mod_out;

        /* Carrier output (FM or additive) */
        if (c->cnt == 0)
        {
            /* FM synthesis */
            car_out = opl_calc_op(car, mod_out << 1, trem_val, c->vol_atten);
            out += car_out;
        }
        else
        {
            /* Additive */
            car_out = opl_calc_op(car, 0, trem_val, c->vol_atten);
            out += mod_out + car_out;
        }
    }

    /* Scale output */
    out >>= 1;
    if (out >  32767) out =  32767;
    if (out < -32768) out = -32768;
    return out;
}

/* Generate one output sample at OUTPUT_RATE via resampling */
static int32_t opl_gen_resampled(void)
{
    int32_t s0, s1, frac, sample;

    /* We need OPL_RATE/OUTPUT_RATE = ~1.127 OPL samples per output sample */
    opl.resamp_frac += OPL_RATE;
    while (opl.resamp_frac >= OUTPUT_RATE)
    {
        opl.resamp_frac -= OUTPUT_RATE;
        opl.prev_sample = opl.cur_sample;
        opl.cur_sample  = opl_gen_sample();
    }

    /* Linear interpolation */
    frac = (opl.resamp_frac * 256) / OUTPUT_RATE;
    s0 = opl.prev_sample;
    s1 = opl.cur_sample;
    sample = s0 + (((s1 - s0) * frac) >> 8);

    /* Apply music volume */
    sample = (sample * midi.volume) >> 7;
    return sample;
}

/* ==================== OPL Key On/Off ==================== */

static void opl_key_on(int ch)
{
    opl_channel_t *c = &opl.chan[ch];
    int j;

    c->key_on = 1;
    for (j = 0; j < 2; j++)
    {
        c->op[j].phase      = 0;
        c->op[j].env        = 511;
        c->op[j].eg_state   = EG_ATTACK;
        c->op[j].eg_counter = 0;
        c->op[j].key        = 1;
    }
    c->fb_out[0] = 0;
    c->fb_out[1] = 0;
}

static void opl_key_off(int ch)
{
    opl_channel_t *c = &opl.chan[ch];
    int j;

    c->key_on = 0;
    for (j = 0; j < 2; j++)
    {
        if (c->op[j].eg_state != EG_OFF)
        {
            c->op[j].eg_state   = EG_RELEASE;
            c->op[j].eg_counter = 0;
        }
        c->op[j].key = 0;
    }
}

/* Set channel frequency */
static void opl_set_freq(int ch, int fnum, int block)
{
    opl_channel_t *c = &opl.chan[ch];
    c->fnum  = fnum;
    c->block = block;

    /* KSL attenuation: depends on fnum top bits and block */
    /* Simplified: higher notes get more attenuation with KSL enabled */
    {
        int ksl_base = ((block << 4) | (fnum >> 6));
        if (ksl_base > 112) ksl_base = 112;

        c->op[0].ksl_atten = (c->op[0].ksl > 0) ?
            (ksl_base >> (3 - c->op[0].ksl)) : 0;
        c->op[1].ksl_atten = (c->op[1].ksl > 0) ?
            (ksl_base >> (3 - c->op[1].ksl)) : 0;
    }
}

/* Program an OPL operator from GENMIDI operator data */
static void opl_prog_op(opl_op_t *op, const genmidi_op_t *g)
{
    /* tremolo byte = register 0x20:
     * bit 7: AM
     * bit 6: VIB
     * bit 5: EGT (sustaining)
     * bit 4: KSR
     * bit 3-0: MULT */
    op->am   = (g->tremolo >> 7) & 1;
    op->vib  = (g->tremolo >> 6) & 1;
    op->egt  = (g->tremolo >> 5) & 1;
    op->ksr  = (g->tremolo >> 4) & 1;
    op->mult = (g->tremolo)      & 0x0F;

    /* attack byte = register 0x60:
     * bits 7-4: AR
     * bits 3-0: DR */
    op->ar = (g->attack >> 4) & 0x0F;
    op->dr = (g->attack)      & 0x0F;

    /* sustain byte = register 0x80:
     * bits 7-4: SL
     * bits 3-0: RR */
    op->sl = (g->sustain >> 4) & 0x0F;
    op->rr = (g->sustain)      & 0x0F;

    /* waveform = register 0xE0 */
    op->ws = g->waveform & 0x03;

    /* scale byte = register 0x40:
     * bits 7-6: KSL
     * bits 5-0: TL */
    op->ksl = (g->scale >> 6) & 0x03;
    op->tl  = (g->scale)      & 0x3F;
}

/* Program OPL channel from GENMIDI voice */
static void opl_prog_voice(int ch, const genmidi_voice_t *v)
{
    opl_channel_t *c = &opl.chan[ch];

    opl_prog_op(&c->op[0], &v->modulator);
    opl_prog_op(&c->op[1], &v->carrier);

    /* feedback byte = register 0xC0:
     * bits 3-1: FB
     * bit 0: CNT */
    c->fb  = (v->feedback >> 1) & 7;
    c->cnt = v->feedback & 1;
}

/* ==================== GENMIDI Loading ==================== */

static void load_genmidi(void)
{
    int             lumpnum, len;
    const uint8_t  *data;

    if (genmidi_loaded)
        return;

    lumpnum = W_CheckNumForName("GENMIDI");
    if (lumpnum < 0)
    {
        genmidi_loaded = -1;
        return;
    }

    data = (const uint8_t *)W_CacheLumpNum(lumpnum, PU_STATIC);
    len  = W_LumpLength(lumpnum);

    /* Validate header */
    if (len < 8 || memcmp(data, "#OPL_II#", 8) != 0)
    {
        genmidi_loaded = -1;
        return;
    }

    /* Check size: need at least 8 + 175*36 = 6308 bytes */
    if (len < 8 + GENMIDI_NUM_INSTRS * 36)
    {
        genmidi_loaded = -1;
        return;
    }

    /* Point directly at the instrument data
     * The data is packed exactly as our struct with __attribute__((packed)) */
    genmidi_instrs = (genmidi_instr_t *)(data + 8);

    genmidi_loaded = 1;
}

/* ==================== MIDI Voice Allocation ==================== */

static void midi_note_off(int ch, int note);

static int alloc_voice(int midi_ch, int note)
{
    int      i, steal = -1, rel_voice = -1;
    uint32_t oldest_age;
    (void)note;

    /* Find free voice */
    for (i = 0; i < MAX_VOICES; i++)
        if (!midi.voices[i].active)
            return i;

    /* Find voice in release on same channel */
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].active && midi.voices[i].midi_ch == midi_ch)
        {
            opl_channel_t *c = &opl.chan[i];
            if (c->op[1].eg_state == EG_RELEASE ||
                c->op[1].eg_state == EG_OFF)
            {
                rel_voice = i;
                break;
            }
        }
    }
    if (rel_voice >= 0)
    {
        opl_key_off(rel_voice);
        midi.voices[rel_voice].active = 0;
        return rel_voice;
    }

    /* Steal oldest voice */
    oldest_age = 0xFFFFFFFF;
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].age < oldest_age)
        {
            oldest_age = midi.voices[i].age;
            steal      = i;
        }
    }

    if (steal >= 0)
    {
        opl_key_off(steal);
        midi.voices[steal].active = 0;
    }

    return steal;
}

/* ==================== MIDI Note Handling ==================== */

static void midi_note_on(int ch, int note, int velocity)
{
    int                      slot, instr_idx, real_note, block, fn_idx;
    int                      combined_vol, vol_atten;
    uint16_t                 fnum;
    const genmidi_instr_t   *instr;
    const genmidi_voice_t   *voice;

    if (velocity == 0)
    {
        midi_note_off(ch, note);
        return;
    }

    if (genmidi_loaded != 1 || !genmidi_instrs)
        return;

    /* Select instrument */
    if (midi.chan[ch].is_drum)
    {
        instr_idx = 128 + (note - 35);
        if (instr_idx < 128 || instr_idx >= GENMIDI_NUM_INSTRS)
            return;
    }
    else
    {
        instr_idx = midi.chan[ch].program;
        if (instr_idx >= 128) instr_idx = 0;
    }

    instr = &genmidi_instrs[instr_idx];
    voice = &instr->voices[0];

    /* Allocate OPL channel */
    slot = alloc_voice(ch, note);
    if (slot < 0)
        return;

    /* Program OPL */
    opl_prog_voice(slot, voice);

    /* Calculate note */
    if (instr->flags & GENMIDI_FLAG_FIXED)
        real_note = instr->fixed_note;
    else
        real_note = note;

    /* Apply base note offset from GENMIDI voice */
    real_note += voice->base_note_offset;

    /* Apply fine tuning (center = 128) */
    if (instr->fine_tuning != 128)
    {
        int ft = (int)instr->fine_tuning - 128;
        real_note += ft / 64;
    }

    if (real_note < 0)   real_note = 0;
    if (real_note > 127) real_note = 127;

    /* Calculate block (octave) and F-number */
    fn_idx = real_note % 12;
    block  = real_note / 12;

    /* OPL2 block range is 0-7 */
    if (block > 7) block = 7;
    if (block < 0) block = 0;

    fnum = fnumber_table[fn_idx];

    /* Calculate volume attenuation */
    combined_vol = (velocity * midi.chan[ch].volume * midi.chan[ch].expression)
                   / (127 * 127);
    if (combined_vol > 127) combined_vol = 127;
    if (combined_vol < 0)   combined_vol = 0;

    /* Map to OPL attenuation: 0=loud, ~48=quiet */
    vol_atten = ((127 - combined_vol) * 48) / 127;

    opl.chan[slot].vol_atten = vol_atten;

    opl_set_freq(slot, fnum, block);
    opl_key_on(slot);

    /* Record voice info */
    midi.voices[slot].active      = 1;
    midi.voices[slot].midi_ch     = ch;
    midi.voices[slot].note        = note;
    midi.voices[slot].opl_ch      = slot;
    midi.voices[slot].velocity    = velocity;
    midi.voices[slot].age         = midi.voice_age++;
    midi.voices[slot].note_offset = voice->base_note_offset;
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
    int i;
    switch (cc)
    {
    case 7:  midi.chan[ch].volume = val; break;
    case 10: midi.chan[ch].pan = val; break;
    case 11: midi.chan[ch].expression = val; break;
    case 64: break; /* sustain pedal - ignore */
    case 121:
        midi.chan[ch].volume     = 100;
        midi.chan[ch].pan        = 64;
        midi.chan[ch].expression = 127;
        midi.chan[ch].pitch_bend = 0;
        break;
    case 120:
    case 123:
        for (i = 0; i < MAX_VOICES; i++)
        {
            if (midi.voices[i].active && midi.voices[i].midi_ch == ch)
            {
                opl_key_off(midi.voices[i].opl_ch);
                midi.voices[i].active = 0;
            }
        }
        break;
    }
}

static void midi_program_change(int ch, int prog)
{
    midi.chan[ch].program = prog & 0x7F;
}

/* ==================== MIDI Timing ==================== */

static void midi_calc_timing(void)
{
    if (midi.ticks_per_beat == 0)
        midi.ticks_per_beat = 140;
    if (midi.us_per_beat == 0)
        midi.us_per_beat = 500000;

    midi.samples_per_tick = ((double)midi.us_per_beat / 1000000.0)
                          * (double)OUTPUT_RATE
                          / (double)midi.ticks_per_beat;

    if (midi.samples_per_tick < 1.0)
        midi.samples_per_tick = 1.0;
}

/* ==================== MIDI Parser ==================== */

static uint32_t read_var_len(const uint8_t *data, int *pos, int end)
{
    uint32_t val = 0;
    uint8_t  b;
    int      i;

    for (i = 0; i < 4; i++)
    {
        if (*pos >= end) break;
        b   = data[(*pos)++];
        val = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return val;
}

static int parse_midi(const uint8_t *data, int len)
{
    int      pos, ntracks, track;
    uint16_t division;

    midi.num_events     = 0;
    midi.ticks_per_beat = 120;
    midi.us_per_beat    = 500000;

    if (len < 14 || memcmp(data, "MThd", 4) != 0)
        return 0;

    pos = 8;
    pos += 2;  /* skip format */

    ntracks  = (data[pos] << 8) | data[pos+1]; pos += 2;
    division = (data[pos] << 8) | data[pos+1]; pos += 2;

    if (division & 0x8000)
        midi.ticks_per_beat = 120;
    else if (division > 0)
        midi.ticks_per_beat = division;
    else
        midi.ticks_per_beat = 120;

    if (!midi.events)
    {
        midi.events = (midi_event_t *)malloc(MAX_MIDI_EVENTS * sizeof(midi_event_t));
        if (!midi.events) return 0;
    }

    for (track = 0; track < ntracks && pos + 8 <= len; track++)
    {
        int      trk_len, trk_end;
        uint32_t abs_tick = 0;
        uint8_t  running  = 0;

        if (memcmp(&data[pos], "MTrk", 4) != 0)
        {
            /* Skip unknown chunk */
            int clen = (data[pos+4]<<24)|(data[pos+5]<<16)|
                       (data[pos+6]<<8)|data[pos+7];
            pos += 8 + clen;
            continue;
        }

        trk_len = (data[pos+4]<<24)|(data[pos+5]<<16)|
                  (data[pos+6]<<8)|data[pos+7];
        pos += 8;
        trk_end = pos + trk_len;
        if (trk_end > len) trk_end = len;

        while (pos < trk_end && midi.num_events < MAX_MIDI_EVENTS - 1)
        {
            uint32_t     delta;
            uint8_t      st, type, ch_n;
            midi_event_t *ev;

            delta     = read_var_len(data, &pos, trk_end);
            abs_tick += delta;

            if (pos >= trk_end) break;

            st = data[pos];
            if (st & 0x80)
            {
                running = st;
                pos++;
            }
            else
            {
                st = running;
            }

            if (st == 0) break;

            type = st & 0xF0;
            ch_n = st & 0x0F;

            switch (type)
            {
            case 0x80: /* Note off */
                if (pos + 1 < trk_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = MIDI_EV_NOTEOFF;
                    ev->channel = ch_n;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                    ev->tempo   = 0;
                }
                break;

            case 0x90: /* Note on */
                if (pos + 1 < trk_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = MIDI_EV_NOTEON;
                    ev->channel = ch_n;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                    ev->tempo   = 0;
                }
                break;

            case 0xA0: /* Poly aftertouch */
                pos += 2;
                break;

            case 0xB0: /* Control change */
                if (pos + 1 < trk_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = MIDI_EV_CONTROL;
                    ev->channel = ch_n;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                    ev->tempo   = 0;
                }
                break;

            case 0xC0: /* Program change */
                if (pos < trk_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = MIDI_EV_PROGRAM;
                    ev->channel = ch_n;
                    ev->data1   = data[pos++];
                    ev->data2   = 0;
                    ev->tempo   = 0;
                }
                break;

            case 0xD0: /* Channel pressure */
                pos += 1;
                break;

            case 0xE0: /* Pitch bend */
                if (pos + 1 < trk_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = MIDI_EV_PITCHBEND;
                    ev->channel = ch_n;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                    ev->tempo   = 0;
                }
                break;

            case 0xF0: /* System / Meta */
                if (st == 0xFF)
                {
                    uint8_t  meta;
                    uint32_t mlen;

                    if (pos >= trk_end) goto trk_done;
                    meta = data[pos++];
                    mlen = read_var_len(data, &pos, trk_end);

                    if (meta == 0x51 && mlen == 3 && pos + 3 <= trk_end)
                    {
                        uint32_t t = ((uint32_t)data[pos]<<16)|
                                     ((uint32_t)data[pos+1]<<8)|
                                      (uint32_t)data[pos+2];
                        if (t == 0) t = 500000;

                        ev = &midi.events[midi.num_events++];
                        ev->tick    = abs_tick;
                        ev->type    = MIDI_EV_TEMPO;
                        ev->channel = 0;
                        ev->data1   = 0;
                        ev->data2   = 0;
                        ev->tempo   = t;
                    }
                    else if (meta == 0x2F)
                    {
                        pos += mlen;
                        goto trk_done;
                    }

                    pos += mlen;
                    running = 0;
                }
                else if (st == 0xF0 || st == 0xF7)
                {
                    uint32_t slen = read_var_len(data, &pos, trk_end);
                    pos += slen;
                    running = 0;
                }
                else
                {
                    pos++;
                }
                break;

            default:
                pos++;
                break;
            }
        }

trk_done:
        pos = trk_end;
    }

    midi_calc_timing();
    return midi.num_events;
}

/* Sort events by tick (insertion sort, stable) */
static void sort_events(void)
{
    int          i, j;
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

/* Process a single MIDI event */
static void process_event(const midi_event_t *ev)
{
    switch (ev->type)
    {
    case MIDI_EV_NOTEON:
        midi_note_on(ev->channel, ev->data1, ev->data2);
        break;
    case MIDI_EV_NOTEOFF:
        midi_note_off(ev->channel, ev->data1);
        break;
    case MIDI_EV_CONTROL:
        midi_control_change(ev->channel, ev->data1, ev->data2);
        break;
    case MIDI_EV_PROGRAM:
        midi_program_change(ev->channel, ev->data1);
        break;
    case MIDI_EV_PITCHBEND:
        midi.chan[ev->channel].pitch_bend =
            (int16_t)(((ev->data2 << 7) | ev->data1) - 8192);
        break;
    case MIDI_EV_TEMPO:
        midi.us_per_beat = ev->tempo;
        midi_calc_timing();
        break;
    }
}

/* Advance MIDI sequencer by 'samples' output samples */
static void midi_advance(int samples)
{
    int i;

    if (!midi.playing || midi.num_events == 0)
        return;

    midi.tick_accum += (double)samples;

    while (midi.tick_accum >= midi.samples_per_tick)
    {
        midi.tick_accum -= midi.samples_per_tick;
        midi.current_tick++;

        /* Process all events at this tick */
        while (midi.current_event < midi.num_events &&
               midi.events[midi.current_event].tick <= midi.current_tick)
        {
            process_event(&midi.events[midi.current_event]);
            midi.current_event++;
        }

        /* End of song? */
        if (midi.current_event >= midi.num_events)
        {
            if (midi.looping)
            {
                midi.current_event = 0;
                midi.current_tick  = 0;
                midi.tick_accum    = 0.0;

                /* Silence all voices */
                for (i = 0; i < MAX_VOICES; i++)
                {
                    if (midi.voices[i].active)
                        opl_key_off(midi.voices[i].opl_ch);
                    midi.voices[i].active = 0;
                }

                /* Reset channel state */
                for (i = 0; i < MIDI_CHANNELS; i++)
                {
                    midi.chan[i].volume     = 100;
                    midi.chan[i].expression = 127;
                }

                /* Reset tempo */
                midi.us_per_beat = 500000;
                midi_calc_timing();
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

        /* Advance MIDI timing */
        if (midi.playing)
            midi_advance(MIX_SAMPLES);

        /* Generate audio samples */
        for (s = 0; s < MIX_SAMPLES; s++)
        {
            int32_t left = 0, right = 0;

            /* OPL music */
            if (midi.playing)
            {
                int32_t m = opl_gen_resampled();
                left  += m;
                right += m;
            }

            /* SFX mixing */
            sfx_lock();
            for (c = 0; c < SND_CHANNELS; c++)
            {
                sfx_channel_t *ch = &sfx_channels[c];
                int idx;
                int32_t samp, lv, rv;

                if (!ch->active) continue;

                idx = ch->pos >> 16;
                if (idx >= ch->length)
                {
                    ch->active = 0;
                    continue;
                }

                samp = ((int32_t)ch->data[idx] - 128) << 7;
                ch->pos += ch->step;

                samp = (samp * ch->vol * sfx_volume) / (127 * 127);

                lv = 255 - ch->sep;
                rv = ch->sep;

                left  += (samp * lv) >> 8;
                right += (samp * rv) >> 8;
            }
            sfx_unlock();

            /* Clamp */
            if (left  >  32767) left  =  32767;
            if (left  < -32768) left  = -32768;
            if (right >  32767) right =  32767;
            if (right < -32768) right = -32768;

            mix_buffer[s*2]     = (int16_t)left;
            mix_buffer[s*2 + 1] = (int16_t)right;
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
    midi.volume      = 127;
    midi.us_per_beat = 500000;

    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        midi.chan[i].volume     = 100;
        midi.chan[i].pan        = 64;
        midi.chan[i].expression = 127;
        midi.chan[i].is_drum    = (i == 9) ? 1 : 0;
    }

    sfx_sema = sceKernelCreateSema("sfx_sema", 0, 1, 1, NULL);

    psp_audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                      MIX_SAMPLES,
                                      PSP_AUDIO_FORMAT_STEREO);
    if (psp_audio_ch < 0) return;

    snd_running = 1;
    snd_thread_id = sceKernelCreateThread("snd", audio_thread,
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

    if (sfx_sema >= 0)
    {
        sceKernelDeleteSema(sfx_sema);
        sfx_sema = -1;
    }

    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
    }

    if (midi.events)
    {
        free(midi.events);
        midi.events = NULL;
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
    void          *lump;
    unsigned char *data;
    int            lumpnum, rate, length, slot, handle;

    if (!sfxinfo || !snd_running) return -1;

    lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0 || lumpnum >= 2048) return -1;

    if (!sfx_cache[lumpnum])
        sfx_cache[lumpnum] = W_CacheLumpNum(lumpnum, PU_STATIC);

    lump = sfx_cache[lumpnum];
    if (!lump) return -1;

    data = (unsigned char *)lump;

    if ((data[0] | (data[1] << 8)) != 3) return -1;

    rate   = data[2] | (data[3] << 8);
    length = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);

    if (rate == 0)   rate = 11025;
    if (length <= 8) return -1;
    length -= 8;

    if (channel >= 0 && channel < SND_CHANNELS)
        slot = channel;
    else
    {
        int i;
        slot = 0;
        for (i = 0; i < SND_CHANNELS; i++)
        {
            if (!sfx_channels[i].active) { slot = i; break; }
        }
    }

    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0)   sep = 0;
    if (sep > 255) sep = 255;

    handle = next_handle++;
    if (next_handle <= 0) next_handle = 1;

    sfx_lock();
    sfx_channels[slot].data   = data + 8;
    sfx_channels[slot].length = length;
    sfx_channels[slot].pos    = 0;
    sfx_channels[slot].step   = ((uint32_t)rate << 16) / OUTPUT_RATE;
    sfx_channels[slot].vol    = vol;
    sfx_channels[slot].sep    = sep;
    sfx_channels[slot].handle = handle;
    sfx_channels[slot].active = 1;
    sfx_unlock();

    return handle;
}

void I_StopSound(int handle)
{
    int i;
    sfx_lock();
    for (i = 0; i < SND_CHANNELS; i++)
    {
        if (sfx_channels[i].active && sfx_channels[i].handle == handle)
        {
            sfx_channels[i].active = 0;
            break;
        }
    }
    sfx_unlock();
}

boolean I_SoundIsPlaying(int handle)
{
    int i;
    for (i = 0; i < SND_CHANNELS; i++)
        if (sfx_channels[i].active && sfx_channels[i].handle == handle)
            return 1;
    return 0;
}

void I_UpdateSound(void) { }

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel < 0 || channel >= SND_CHANNELS) return;
    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0)   sep = 0;
    if (sep > 255) sep = 255;

    sfx_lock();
    if (sfx_channels[channel].active)
    {
        sfx_channels[channel].vol = vol;
        sfx_channels[channel].sep = sep;
    }
    sfx_unlock();
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds; (void)num_sounds;
}

void I_BindSoundVariables(void) { }

void I_SetSfxVolume(int vol)
{
    if (vol < 0)   vol = 0;
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
    if (midi.midi_data) { free(midi.midi_data); midi.midi_data = NULL; }
    if (midi.events)    { free(midi.events); midi.events = NULL; }
}

void I_SetMusicVolume(int vol)
{
    if (vol < 0)   vol = 0;
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

    midi.playing       = 0;
    midi.current_event = 0;
    midi.current_tick  = 0;
    midi.tick_accum    = 0.0;

    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].active)
            opl_key_off(midi.voices[i].opl_ch);
        midi.voices[i].active = 0;
    }

    opl_reset();

    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        midi.chan[i].volume     = 100;
        midi.chan[i].pan        = 64;
        midi.chan[i].expression = 127;
        midi.chan[i].program    = 0;
        midi.chan[i].pitch_bend = 0;
    }
}

boolean I_MusicIsPlaying(void)
{
    return midi.playing ? 1 : 0;
}

void *I_RegisterSong(void *data, int len)
{
    MEMFILE *in, *out;
    void    *buf;
    size_t   buflen;

    if (!data || len <= 0) return NULL;

    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data = NULL;
        midi.midi_data_len = 0;
    }

    if (!genmidi_loaded)
        load_genmidi();

    /* Already MIDI? */
    if (len >= 4 && memcmp(data, "MThd", 4) == 0)
    {
        midi.midi_data = malloc(len);
        if (!midi.midi_data) return NULL;
        memcpy(midi.midi_data, data, len);
        midi.midi_data_len = len;
    }
    else
    {
        /* Convert MUS to MIDI */
        in = mem_fopen_read(data, len);
        if (!in) return NULL;

        out = mem_fopen_write();
        if (!out) { mem_fclose(in); return NULL; }

        if (mus2mid(in, out) != 0)
        {
            mem_fclose(in);
            mem_fclose(out);
            return NULL;
        }

        buf = NULL;
        buflen = 0;
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
        midi.midi_data_len = (int)buflen;

        mem_fclose(in);
        mem_fclose(out);
    }

    /* Parse MIDI data */
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

    if (midi.num_events == 0) return;

    midi.current_event = 0;
    midi.current_tick  = 0;
    midi.tick_accum    = 0.0;
    midi.looping       = looping ? 1 : 0;
    midi.voice_age     = 0;

    /* Find initial tempo */
    midi.us_per_beat = 500000;
    for (i = 0; i < midi.num_events; i++)
    {
        if (midi.events[i].type == MIDI_EV_TEMPO && midi.events[i].tick == 0)
        {
            midi.us_per_beat = midi.events[i].tempo;
            break;
        }
    }
    midi_calc_timing();

    /* Reset voices */
    for (i = 0; i < MAX_VOICES; i++)
        midi.voices[i].active = 0;

    opl_reset();

    /* Reset MIDI channels */
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        midi.chan[i].volume     = 100;
        midi.chan[i].pan        = 64;
        midi.chan[i].expression = 127;
        midi.chan[i].pitch_bend = 0;
        midi.chan[i].program    = 0;
        midi.chan[i].is_drum    = (i == 9) ? 1 : 0;
    }

    midi.playing = 1;
}
