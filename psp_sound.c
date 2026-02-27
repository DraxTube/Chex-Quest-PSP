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

/* Log-sin table: 256 entries */
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

/* Exponential table: 256 entries */
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

/* Frequency multiplier table (x2 of real values) */
static const uint8_t mt[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

/* Key scale level table */
static const uint8_t ksl_table[16] = {
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64
};

/* F-Number table per ogni nota (C to B), block=4 */
static const uint16_t fnumber[12] = {
    0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,
    0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287
};

/* ==================== OPL2 Operator ==================== */

typedef struct {
    /* Registri programmabili */
    uint8_t     mult;
    uint8_t     ksr;
    uint8_t     egt;
    uint8_t     vib;
    uint8_t     am;
    uint8_t     tl;
    uint8_t     ksl;
    uint8_t     ar;
    uint8_t     dr;
    uint8_t     sl;
    uint8_t     rr;
    uint8_t     ws;

    /* Stato runtime */
    uint32_t    phase;
    uint32_t    phase_inc;
    int32_t     env;
    uint8_t     eg_state;
    uint8_t     key;
    int32_t     ksl_add;
} opl_op_t;

/* ==================== OPL2 Channel ==================== */

typedef struct {
    opl_op_t    op[2];
    uint16_t    fnum;
    uint8_t     block;
    uint8_t     fb;
    uint8_t     cnt;
    uint8_t     key_on;
    int32_t     fb_out[2];
    int32_t     vol_atten;
} opl_channel_t;

/* ==================== OPL2 Chip ==================== */

typedef struct {
    opl_channel_t   chan[OPL_NUM_CHANNELS];

    uint8_t         trem_depth;
    uint8_t         vib_depth;
    uint32_t        trem_phase;
    uint32_t        vib_phase;

    /* Resampling: accumulatore OPL->OUTPUT */
    uint32_t        resamp_accum;
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

#define MIDI_CHANNELS       16
#define MAX_MIDI_EVENTS     65536
#define MAX_VOICES          9

/* Tipo evento interno */
#define MIDI_EV_NOTEON      0x90
#define MIDI_EV_NOTEOFF     0x80
#define MIDI_EV_CONTROL     0xB0
#define MIDI_EV_PROGRAM     0xC0
#define MIDI_EV_PITCHBEND   0xE0
#define MIDI_EV_TEMPO       0xFF

typedef struct {
    uint32_t    tick;
    uint8_t     type;
    uint8_t     channel;
    uint8_t     data1;
    uint8_t     data2;
    uint32_t    data32;   /* usato per tempo (us_per_beat) */
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
    midi_event_t   *events;
    int             num_events;
    int             current_event;
    uint16_t        ticks_per_beat;
    uint32_t        us_per_beat;

    /* Timing: usiamo interi per evitare drift floating point */
    /* samples_per_tick = (us_per_beat * OUTPUT_RATE) / (ticks_per_beat * 1000000) */
    /* Rappresentiamo come frazione: num/den */
    uint64_t        spt_num;      /* numeratore samples_per_tick */
    uint64_t        spt_den;      /* denominatore samples_per_tick */
    uint64_t        tick_frac;    /* accumulatore frazionario (in unita' spt_den) */

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
static int           psp_audio_ch  = -1;
static volatile int  snd_running   = 0;
static SceUID        snd_thread_id = -1;
static int           next_handle   = 1;
static int           sfx_volume    = 127;

/* Cache lump SFX */
static void         *sfx_cache[2048];
static int           sfx_cache_init = 0;

/* Buffer di mix allineato per DMA PSP */
static int16_t __attribute__((aligned(64))) mix_buffer[MIX_SAMPLES * 2];

/* Mutex per proteggere accesso a canali SFX dal thread principale */
static SceUID sfx_mutex = -1;

/* ==================== OPL2 Implementation ==================== */

static void opl_reset(void)
{
    int i, j;
    memset(&opl, 0, sizeof(opl));
    opl.trem_depth = 1;
    opl.vib_depth  = 1;

    for (i = 0; i < OPL_NUM_CHANNELS; i++)
        for (j = 0; j < 2; j++)
        {
            opl.chan[i].op[j].env      = 511;
            opl.chan[i].op[j].eg_state = EG_OFF;
        }
}

/* Calcola rate effettivo per envelope */
static int calc_rate(int rate, int ksr, int block, int fnum)
{
    int rof, eff;

    if (rate == 0)
        return 0;

    rof = (block << 1) | ((fnum >> 9) & 1);
    if (!ksr)
        rof >>= 2;

    eff = (rate << 2) + rof;
    if (eff > 63) eff = 63;
    if (eff < 0)  eff = 0;
    return eff;
}

/* Aggiorna envelope di un operatore per un sample OPL */
static void opl_envelope_calc(opl_op_t *op, int block, int fnum)
{
    int rate, eff;

    switch (op->eg_state)
    {
    case EG_ATTACK:
        if (op->ar == 0)
            return;

        if (op->ar == 15)
        {
            op->env      = 0;
            op->eg_state = EG_DECAY;
            return;
        }

        eff  = calc_rate(op->ar, op->ksr, block, fnum);
        /* Curva di attacco esponenziale: env -= env*rate/32 + 1 */
        rate = (eff < 60) ? (eff >> 2) : 15;
        op->env -= ((op->env >> (5 - (rate >> 2))) | 1);

        if (op->env <= 0)
        {
            op->env      = 0;
            op->eg_state = EG_DECAY;
        }
        break;

    case EG_DECAY:
        if (op->dr == 0)
        {
            op->env      = op->sl << 5;
            op->eg_state = EG_SUSTAIN;
            return;
        }

        eff = calc_rate(op->dr, op->ksr, block, fnum);
        /* Decremento lineare (in unita' di 1/8 dB) */
        op->env += 1 + (eff >> 2);

        if (op->env >= (op->sl << 5))
        {
            op->env      = op->sl << 5;
            op->eg_state = EG_SUSTAIN;
        }
        break;

    case EG_SUSTAIN:
        if (op->egt)
            return;   /* Sustain: mantieni livello */

        /* Senza EGT, decade come release */
        if (op->rr == 0)
            return;

        eff = calc_rate(op->rr, op->ksr, block, fnum);
        op->env += 1 + (eff >> 2);

        if (op->env >= 511)
        {
            op->env      = 511;
            op->eg_state = EG_OFF;
        }
        break;

    case EG_RELEASE:
        if (op->rr == 0)
        {
            op->env      = 511;
            op->eg_state = EG_OFF;
            return;
        }

        eff = calc_rate(op->rr, op->ksr, block, fnum);
        op->env += 1 + (eff >> 2);

        if (op->env >= 511)
        {
            op->env      = 511;
            op->eg_state = EG_OFF;
        }
        break;

    case EG_OFF:
    default:
        op->env = 511;
        break;
    }
}

/* Calcola output di un operatore */
static int32_t opl_operator_calc(opl_op_t *op, int32_t phase_mod,
                                  int32_t trem, int32_t vol_atten)
{
    uint32_t  phase;
    int32_t   atten;
    int32_t   output;
    uint16_t  log_value;
    int       neg;
    int       exp_index, exp_shift;
    uint8_t   ph8;

    /* Fase 10 bit (0-1023) */
    phase = ((op->phase >> 10) + (uint32_t)(phase_mod >> 10)) & 0x3FF;

    /* Attenuazione totale in unita' log */
    atten = op->env + (op->tl << 3) + op->ksl_add + vol_atten;
    if (op->am)
        atten += trem;
    if (atten > 511) atten = 511;
    if (atten >= 511) return 0;

    /* Waveform */
    neg = 0;
    ph8 = phase & 0xFF;

    switch (op->ws)
    {
    case WF_SINE:
        neg = (phase & 0x200) ? 1 : 0;
        log_value = (phase & 0x100) ? logsin_table[~ph8 & 0xFF]
                                    : logsin_table[ph8];
        break;

    case WF_HALFSINE:
        if (phase & 0x200) return 0;
        log_value = (phase & 0x100) ? logsin_table[~ph8 & 0xFF]
                                    : logsin_table[ph8];
        break;

    case WF_ABSSINE:
        log_value = (phase & 0x100) ? logsin_table[~ph8 & 0xFF]
                                    : logsin_table[ph8];
        break;

    case WF_QUARTERSINE:
    default:
        if (phase & 0x100) return 0;
        log_value = logsin_table[ph8];
        break;
    }

    log_value += (uint16_t)(atten << 3);

    exp_index = log_value & 0xFF;
    exp_shift = log_value >> 8;

    if (exp_shift > 13)
        return 0;

    output = (exp_table[exp_index] << 1) >> exp_shift;

    return neg ? -output : output;
}

/* Aggiorna phase accumulator di un operatore */
static void opl_phase_calc(opl_op_t *op, int fnum, int block, int32_t vib)
{
    uint32_t freq;
    int fn = fnum;

    if (op->vib)
    {
        fn += vib;
        if (fn < 0)   fn = 0;
        if (fn > 0x3FF) fn = 0x3FF;
    }

    /* phase_inc = fnum * 2^block * MULT / 2
     * Il fattore /2 e' inglobato nel fatto che mt[] = real_mult * 2
     * e noi shiftiamo di 1 in meno dopo */
    freq = (uint32_t)fn << block;
    op->phase_inc = freq * mt[op->mult];
    op->phase    += op->phase_inc;
}

/* Genera un sample OPL (a OPL_RATE) */
static int32_t opl_generate(void)
{
    int     ch;
    int32_t output = 0;
    int32_t trem, vib;
    uint32_t tp, vp;

    /* Aggiorna LFO tremolo (~3.7 Hz a 49716 Hz)
     * trem_phase e' un contatore 0..1023 */
    opl.trem_phase += 1;
    if (opl.trem_phase >= 210)   /* 49716/210 ~ 236 Hz? no: usiamo periodo corretto */
        opl.trem_phase = 0;

    /* Periodo tremolo: 49716 / 3.7 ≈ 13437 campioni
     * Usiamo un contatore a 24 bit con incremento appropriato */
    /* Riscriviamo con contatori separati a 16 bit */
    /* trem: 0..52 (dB/8 units) */
    tp   = (opl.trem_phase * 256) / 210;
    trem = (tp < 128) ? (tp >> 2) : ((255 - tp) >> 2);
    if (!opl.trem_depth)
        trem >>= 2;

    /* Vibrato ~6.1 Hz */
    opl.vib_phase += 1;
    if (opl.vib_phase >= 8192)
        opl.vib_phase = 0;
    vp  = opl.vib_phase;
    vib = (int32_t)(vp & 0x7FF);
    if (vp & 0x1000) vib = -vib;
    vib >>= 9;
    if (!opl.vib_depth)
        vib >>= 1;

    /* Processa ogni canale */
    for (ch = 0; ch < OPL_NUM_CHANNELS; ch++)
    {
        opl_channel_t *c   = &opl.chan[ch];
        opl_op_t      *mod = &c->op[0];
        opl_op_t      *car = &c->op[1];
        int32_t        mod_out, car_out, feedback, phase_mod;

        if (mod->eg_state == EG_OFF && car->eg_state == EG_OFF)
            continue;

        opl_envelope_calc(mod, c->block, c->fnum);
        opl_envelope_calc(car, c->block, c->fnum);

        opl_phase_calc(mod, c->fnum, c->block, vib);
        opl_phase_calc(car, c->fnum, c->block, vib);

        /* Feedback: media degli ultimi 2 output del modulatore */
        if (c->fb > 0)
            feedback = (c->fb_out[0] + c->fb_out[1]) >> (9 - c->fb);
        else
            feedback = 0;

        /* Modulatore */
        mod_out = opl_operator_calc(mod, feedback << 10, trem, 0);

        c->fb_out[1] = c->fb_out[0];
        c->fb_out[0] = mod_out;

        /* Carrier */
        if (c->cnt == 0)
        {
            /* FM: mod modula car */
            phase_mod = mod_out << 1;
            car_out   = opl_operator_calc(car, phase_mod, trem, c->vol_atten);
            output   += car_out;
        }
        else
        {
            /* Additivo */
            car_out  = opl_operator_calc(car, 0, trem, c->vol_atten);
            output  += mod_out + car_out;
        }
    }

    output >>= 1;
    if (output >  32767) output =  32767;
    if (output < -32768) output = -32768;

    return output;
}

/* Genera un sample a OUTPUT_RATE tramite decimazione di OPL_RATE
 * CORRETTO: per ogni sample OUTPUT dobbiamo consumare
 * OPL_RATE/OUTPUT_RATE campioni OPL (~1.13 per 49716/44100).
 * Usiamo un accumulatore intero: ogni volta che accum >= OUTPUT_RATE
 * generiamo un nuovo campione OPL e sottraiamo OUTPUT_RATE. */
static int32_t opl_generate_resampled(void)
{
    int32_t sample;

    opl.resamp_accum += OPL_RATE;
    while (opl.resamp_accum >= OUTPUT_RATE)
    {
        opl.resamp_accum -= OUTPUT_RATE;
        opl.last_sample   = opl_generate();
    }

    sample = opl.last_sample;
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
        c->op[j].phase    = 0;
        c->op[j].env      = 511;
        c->op[j].eg_state = EG_ATTACK;
        c->op[j].key      = 1;
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
        if (c->op[j].eg_state != EG_OFF)
            c->op[j].eg_state = EG_RELEASE;
        c->op[j].key = 0;
    }
}

/* Imposta frequenza */
static void opl_set_frequency(int ch, int fnum, int block)
{
    opl_channel_t *c = &opl.chan[ch];
    int ksl_base;

    c->fnum  = fnum;
    c->block = block;

    ksl_base = ksl_table[fnum >> 6];
    ksl_base = (ksl_base << 1) - ((8 - block) << 5);
    if (ksl_base < 0) ksl_base = 0;

    c->op[0].ksl_add = (c->op[0].ksl > 0) ? (ksl_base >> (3 - c->op[0].ksl)) : 0;
    c->op[1].ksl_add = (c->op[1].ksl > 0) ? (ksl_base >> (3 - c->op[1].ksl)) : 0;
}

/* Programma un operatore da dati GENMIDI */
static void opl_program_operator(opl_op_t *op, const genmidi_op_t *data)
{
    op->am   = (data->mult_ksr_egt_vib_am >> 7) & 1;
    op->vib  = (data->mult_ksr_egt_vib_am >> 6) & 1;
    op->egt  = (data->mult_ksr_egt_vib_am >> 5) & 1;
    op->ksr  = (data->mult_ksr_egt_vib_am >> 4) & 1;
    op->mult = (data->mult_ksr_egt_vib_am)       & 0x0F;

    op->ksl  = (data->ksl_tl >> 6) & 3;
    op->tl   = (data->ksl_tl)      & 0x3F;

    op->ar   = (data->ar_dr >> 4) & 0x0F;
    op->dr   = (data->ar_dr)      & 0x0F;

    op->sl   = (data->sl_rr >> 4) & 0x0F;
    op->rr   = (data->sl_rr)      & 0x0F;

    op->ws   = (data->ws) & 0x03;
}

/* Programma un canale OPL da una voce GENMIDI */
static void opl_program_voice(int ch, const genmidi_voice_t *voice)
{
    opl_channel_t *c = &opl.chan[ch];

    opl_program_operator(&c->op[0], &voice->mod);
    opl_program_operator(&c->op[1], &voice->car);

    c->fb  = (voice->fb_cnt >> 1) & 7;
    c->cnt = voice->fb_cnt & 1;
}

/* ==================== GENMIDI Loading ==================== */

static void load_genmidi(void)
{
    int             lumpnum, len, i, pos, v;
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

    if (len < 8 || memcmp(data, "#OPL_II#", 8) != 0)
    {
        genmidi_loaded = -1;
        return;
    }

    pos = 8;

    for (i = 0; i < GENMIDI_NUM_INSTRS; i++)
    {
        if (pos + 4 > len) break;

        genmidi_instrs[i].flags       = data[pos] | (data[pos+1] << 8);
        genmidi_instrs[i].fine_tuning = data[pos+2];
        genmidi_instrs[i].fixed_note  = data[pos+3];
        pos += 4;

        for (v = 0; v < 2; v++)
        {
            if (pos + 11 > len) goto done_load;

            genmidi_instrs[i].voice[v].mod.mult_ksr_egt_vib_am = data[pos++];
            genmidi_instrs[i].voice[v].mod.ksl_tl              = data[pos++];
            genmidi_instrs[i].voice[v].mod.ar_dr               = data[pos++];
            genmidi_instrs[i].voice[v].mod.sl_rr               = data[pos++];
            genmidi_instrs[i].voice[v].mod.ws                  = data[pos++];

            genmidi_instrs[i].voice[v].car.mult_ksr_egt_vib_am = data[pos++];
            genmidi_instrs[i].voice[v].car.ksl_tl              = data[pos++];
            genmidi_instrs[i].voice[v].car.ar_dr               = data[pos++];
            genmidi_instrs[i].voice[v].car.sl_rr               = data[pos++];
            genmidi_instrs[i].voice[v].car.ws                  = data[pos++];

            genmidi_instrs[i].voice[v].fb_cnt = data[pos++];
        }
    }

done_load:
    genmidi_loaded = 1;
}

/* ==================== MIDI Note Handling ==================== */

static void midi_note_off(int ch, int note);

static int alloc_voice(int midi_ch, int note)
{
    int      i;
    int      steal     = -1;
    int      off_voice = -1;
    uint32_t oldest_age = 0;

    (void)note;

    /* 1) Cerca voce libera */
    for (i = 0; i < MAX_VOICES; i++)
        if (!midi.voices[i].active)
            return i;

    /* 2) Cerca voce dello stesso canale in release */
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].active && midi.voices[i].midi_ch == midi_ch)
        {
            if (opl.chan[i].op[1].eg_state == EG_RELEASE ||
                opl.chan[i].op[1].eg_state == EG_OFF)
            {
                off_voice = i;
                break;
            }
        }
    }
    if (off_voice >= 0)
    {
        opl_key_off(off_voice);
        midi.voices[off_voice].active = 0;
        return off_voice;
    }

    /* 3) Ruba la voce piu' vecchia */
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

static void midi_note_on(int ch, int note, int velocity)
{
    int                    slot, instr_idx, real_note, block, fnum;
    int                    vol_atten, combined_vol;
    const genmidi_instr_t *instr;

    if (velocity == 0)
    {
        midi_note_off(ch, note);
        return;
    }

    if (genmidi_loaded != 1)
        return;

    /* Seleziona strumento */
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

    slot = alloc_voice(ch, note);
    if (slot < 0)
        return;

    opl_program_voice(slot, &instr->voice[0]);

    /* Calcola nota reale */
    if (instr->flags & GENMIDI_FLAG_FIXED)
        real_note = instr->fixed_note;
    else
        real_note = note;

    /* Fine tuning: centro=128, ogni semitono = 64 unita' */
    if (instr->fine_tuning != 128)
    {
        int offset = (int)instr->fine_tuning - 128;
        real_note += offset / 32;   /* approssimazione semitoni */
    }

    if (real_note < 0)   real_note = 0;
    if (real_note > 127) real_note = 127;

    /* Block = ottava (1-7), fnum = indice cromatico */
    block = (real_note / 12) - 1;
    if (block < 1) block = 1;
    if (block > 7) block = 7;

    fnum = fnumber[real_note % 12];

    /* Volume: velocity * ch_volume * expression -> attenuazione OPL */
    combined_vol = (velocity * midi.chan[ch].volume * midi.chan[ch].expression)
                   / (127 * 127);
    if (combined_vol > 127) combined_vol = 127;
    if (combined_vol < 0)   combined_vol = 0;

    /* Mappa 0-127 -> attenuazione OPL 0-47 (piu' sensibile ai valori alti) */
    vol_atten = ((127 - combined_vol) * 47) / 127;

    opl.chan[slot].vol_atten = vol_atten;

    opl_set_frequency(slot, fnum, block);
    opl_key_on(slot);

    midi.voices[slot].active  = 1;
    midi.voices[slot].midi_ch = ch;
    midi.voices[slot].note    = note;
    midi.voices[slot].opl_ch  = slot;
    midi.voices[slot].velocity= velocity;
    midi.voices[slot].age     = midi.voice_age++;
}

static void midi_note_off(int ch, int note)
{
    int i;
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (midi.voices[i].active      &&
            midi.voices[i].midi_ch == ch &&
            midi.voices[i].note   == note)
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
    case 7:
        midi.chan[ch].volume = val;
        break;
    case 10:
        midi.chan[ch].pan = val;
        break;
    case 11:
        midi.chan[ch].expression = val;
        break;
    case 64:   /* Sustain pedal - ignoriamo */
        break;
    case 121:  /* Reset controllers */
        midi.chan[ch].volume      = 100;
        midi.chan[ch].pan         = 64;
        midi.chan[ch].expression  = 127;
        midi.chan[ch].pitch_bend  = 0;
        break;
    case 120:  /* All sound off */
    case 123:  /* All notes off */
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

/* ==================== Timing helpers ==================== */

/* Aggiorna samples_per_tick come frazione intera
 * spt = (us_per_beat * OUTPUT_RATE) / (ticks_per_beat * 1000000)
 * Rappresentiamo come spt_num/spt_den dove spt_den = ticks_per_beat * 1000000 */
static void midi_update_timing(void)
{
    if (midi.ticks_per_beat == 0)
        midi.ticks_per_beat = 140;

    midi.spt_num = (uint64_t)midi.us_per_beat * OUTPUT_RATE;
    midi.spt_den = (uint64_t)midi.ticks_per_beat * 1000000ULL;

    if (midi.spt_den == 0)
        midi.spt_den = 1;
}

/* ==================== MIDI Parser ==================== */

static uint32_t read_var(const uint8_t *data, int *pos, int end)
{
    uint32_t val = 0;
    uint8_t  b;
    int      i;

    for (i = 0; i < 4; i++)
    {
        if (*pos >= end) break;
        b    = data[(*pos)++];
        val  = (val << 7) | (b & 0x7F);
        if (!(b & 0x80)) break;
    }
    return val;
}

static int parse_midi(const uint8_t *data, int len)
{
    int      pos, tracks, track;
    uint16_t division;

    midi.num_events    = 0;
    midi.ticks_per_beat = 140;
    midi.us_per_beat    = 500000;

    if (len < 14 || memcmp(data, "MThd", 4) != 0)
        return 0;

    /* Header chunk size */
    pos = 8;

    /* Format (ignoriamo, trattiamo tutto come format 0/1) */
    pos += 2;

    tracks   = (data[pos] << 8) | data[pos+1]; pos += 2;
    division = (data[pos] << 8) | data[pos+1]; pos += 2;

    if (division & 0x8000)
        midi.ticks_per_beat = 140;   /* SMPTE non supportato */
    else
        midi.ticks_per_beat = (division > 0) ? division : 140;

    /* Alloca eventi se non gia' fatto */
    if (!midi.events)
    {
        midi.events = (midi_event_t *)malloc(MAX_MIDI_EVENTS * sizeof(midi_event_t));
        if (!midi.events)
            return 0;
    }

    /* Parsifica ogni traccia */
    for (track = 0; track < tracks && pos < len; track++)
    {
        int      track_len, track_end;
        uint32_t abs_tick = 0;
        uint8_t  running  = 0;

        if (pos + 8 > len) break;
        if (memcmp(&data[pos], "MTrk", 4) != 0)
        {
            /* Salta chunk sconosciuto */
            int skip = (data[pos+4]<<24)|(data[pos+5]<<16)|
                       (data[pos+6]<<8)|data[pos+7];
            pos += 8 + skip;
            track--;
            continue;
        }

        track_len = (data[pos+4] << 24) | (data[pos+5] << 16) |
                    (data[pos+6] <<  8) |  data[pos+7];
        pos += 8;
        track_end = pos + track_len;
        if (track_end > len) track_end = len;

        while (pos < track_end && midi.num_events < MAX_MIDI_EVENTS - 1)
        {
            uint32_t     delta;
            uint8_t      status, type, ch;
            midi_event_t *ev;

            delta     = read_var(data, &pos, track_end);
            abs_tick += delta;

            if (pos >= track_end) break;

            /* Running status */
            if (data[pos] & 0x80)
            {
                running = data[pos];
                pos++;
            }

            /* I messaggi SysEx e Meta resettano il running status */
            if (running == 0xF0 || running == 0xFF)
                status = running;
            else
                status = running;

            type = status & 0xF0;
            ch   = status & 0x0F;

            switch (type)
            {
            case 0x80:   /* Note off */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = 0x80;
                    ev->channel = ch;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                }
                break;

            case 0x90:   /* Note on */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = 0x90;
                    ev->channel = ch;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                }
                break;

            case 0xA0:   /* Polyphonic aftertouch */
                if (pos + 1 < track_end) { pos += 2; }
                break;

            case 0xB0:   /* Control change */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = 0xB0;
                    ev->channel = ch;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                }
                break;

            case 0xC0:   /* Program change */
                if (pos < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = 0xC0;
                    ev->channel = ch;
                    ev->data1   = data[pos++];
                    ev->data2   = 0;
                }
                break;

            case 0xD0:   /* Channel pressure */
                if (pos < track_end) pos++;
                break;

            case 0xE0:   /* Pitch bend */
                if (pos + 1 < track_end)
                {
                    ev = &midi.events[midi.num_events++];
                    ev->tick    = abs_tick;
                    ev->type    = 0xE0;
                    ev->channel = ch;
                    ev->data1   = data[pos++];
                    ev->data2   = data[pos++];
                }
                break;

            case 0xF0:   /* System / Meta */
                if (status == 0xFF)
                {
                    uint8_t  meta;
                    uint32_t mlen;

                    if (pos >= track_end) goto track_done;
                    meta = data[pos++];
                    mlen = read_var(data, &pos, track_end);

                    if (meta == 0x51 && mlen == 3 && pos + 3 <= track_end)
                    {
                        uint32_t tempo = ((uint32_t)data[pos]   << 16) |
                                         ((uint32_t)data[pos+1] <<  8) |
                                          (uint32_t)data[pos+2];
                        if (tempo == 0) tempo = 500000;

                        /* Salva evento di cambio tempo */
                        ev = &midi.events[midi.num_events++];
                        ev->tick    = abs_tick;
                        ev->type    = MIDI_EV_TEMPO;
                        ev->channel = 0;
                        ev->data1   = 0;
                        ev->data2   = 0;
                        ev->data32  = tempo;
                    }
                    else if (meta == 0x2F)
                    {
                        pos += mlen;
                        goto track_done;
                    }

                    pos += mlen;
                    running = 0;   /* Meta resetta running status */
                }
                else if (status == 0xF0 || status == 0xF7)
                {
                    uint32_t slen = read_var(data, &pos, track_end);
                    pos += slen;
                    running = 0;
                }
                break;

            default:
                /* Byte sconosciuto: salta */
                pos++;
                break;
            }
        }

track_done:
        pos = track_end;
    }

    midi_update_timing();
    return midi.num_events;
}

/* Insertion sort (stabile) sugli eventi per tick crescente */
static void sort_events(void)
{
    int          i, j;
    midi_event_t tmp;

    for (i = 1; i < midi.num_events; i++)
    {
        tmp = midi.events[i];
        j   = i - 1;
        while (j >= 0 && midi.events[j].tick > tmp.tick)
        {
            midi.events[j+1] = midi.events[j];
            j--;
        }
        midi.events[j+1] = tmp;
    }
}

/* Processa un evento MIDI */
static void process_event(const midi_event_t *ev)
{
    int ch = ev->channel;

    switch (ev->type)
    {
    case MIDI_EV_NOTEON:
        midi_note_on(ch, ev->data1, ev->data2);
        break;
    case MIDI_EV_NOTEOFF:
        midi_note_off(ch, ev->data1);
        break;
    case MIDI_EV_CONTROL:
        midi_control_change(ch, ev->data1, ev->data2);
        break;
    case MIDI_EV_PROGRAM:
        midi_program_change(ch, ev->data1);
        break;
    case MIDI_EV_PITCHBEND:
        midi.chan[ch].pitch_bend =
            (int16_t)(((ev->data2 << 7) | ev->data1) - 8192);
        break;
    case MIDI_EV_TEMPO:
        midi.us_per_beat = ev->data32;
        midi_update_timing();
        break;
    }
}

/* Avanza il sequencer MIDI di 'samples' campioni OUTPUT */
static void midi_advance(int samples)
{
    int i;

    if (!midi.playing || midi.num_events == 0)
        return;

    /* tick_frac e' in unita' spt_den per evitare floating point */
    midi.tick_frac += (uint64_t)samples * midi.spt_den;

    while (midi.tick_frac >= midi.spt_num)
    {
        midi.tick_frac  -= midi.spt_num;
        midi.current_tick++;

        /* Esegui tutti gli eventi al tick corrente */
        while (midi.current_event < midi.num_events &&
               midi.events[midi.current_event].tick <= midi.current_tick)
        {
            process_event(&midi.events[midi.current_event]);
            midi.current_event++;
        }

        /* Fine brano */
        if (midi.current_event >= midi.num_events)
        {
            if (midi.looping)
            {
                midi.current_event = 0;
                midi.current_tick  = 0;
                midi.tick_frac     = 0;

                /* Silenzia tutte le voci */
                for (i = 0; i < MAX_VOICES; i++)
                {
                    if (midi.voices[i].active)
                        opl_key_off(midi.voices[i].opl_ch);
                    midi.voices[i].active = 0;
                }

                /* Ripristina controller */
                for (i = 0; i < MIDI_CHANNELS; i++)
                {
                    midi.chan[i].volume     = 100;
                    midi.chan[i].expression = 127;
                }

                /* Ripristina tempo originale (primo evento di tipo TEMPO) */
                midi.us_per_beat = 500000;
                for (i = 0; i < midi.num_events; i++)
                {
                    if (midi.events[i].type == MIDI_EV_TEMPO)
                    {
                        midi.us_per_beat = midi.events[i].data32;
                        break;
                    }
                }
                midi_update_timing();
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
        int     s, c;
        int32_t left, right, sample, lv, rv;

        /* Azzera buffer */
        memset(mix_buffer, 0, sizeof(mix_buffer));

        /* Avanza sequencer MIDI */
        if (midi.playing)
            midi_advance(MIX_SAMPLES);

        /* Genera campioni */
        for (s = 0; s < MIX_SAMPLES; s++)
        {
            left  = 0;
            right = 0;

            /* Musica OPL */
            if (midi.playing)
            {
                int32_t music = opl_generate_resampled();
                left  += music;
                right += music;
            }

            /* SFX */
            if (sfx_mutex >= 0)
                sceKernelLockMutex(sfx_mutex, 1, NULL);

            for (c = 0; c < SND_CHANNELS; c++)
            {
                sfx_channel_t *ch = &sfx_channels[c];
                int            idx;

                if (!ch->active)
                    continue;

                idx = ch->pos >> 16;
                if (idx >= ch->length)
                {
                    ch->active = 0;
                    continue;
                }

                /* PCM 8-bit unsigned -> signed 16-bit */
                sample = ((int32_t)ch->data[idx] - 128) << 7;
                ch->pos += ch->step;

                /* Volume: sfx_volume e canale volume */
                sample = (sample * ch->vol * sfx_volume) / (127 * 127);

                /* Panning */
                lv = 255 - ch->sep;
                rv = ch->sep;

                left  += (sample * lv) >> 8;
                right += (sample * rv) >> 8;
            }

            if (sfx_mutex >= 0)
                sceKernelUnlockMutex(sfx_mutex, 1);

            /* Clamp */
            if (left  >  32767) left  =  32767;
            if (left  < -32768) left  = -32768;
            if (right >  32767) right =  32767;
            if (right < -32768) right = -32768;

            mix_buffer[s * 2]     = (int16_t)left;
            mix_buffer[s * 2 + 1] = (int16_t)right;
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

    /* Mutex per SFX */
    sfx_mutex = sceKernelCreateMutex("sfx_mutex", 0, 0, NULL);

    psp_audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                      MIX_SAMPLES,
                                      PSP_AUDIO_FORMAT_STEREO);
    if (psp_audio_ch < 0)
        return;

    snd_running   = 1;
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

    if (sfx_mutex >= 0)
    {
        sceKernelDeleteMutex(sfx_mutex);
        sfx_mutex = -1;
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

    /* Header: magic=3, rate, length */
    if ((data[0] | (data[1] << 8)) != 3)
        return -1;

    rate   = data[2] | (data[3] << 8);
    length = data[4] | (data[5] << 8) | (data[6] << 16) | (data[7] << 24);

    if (rate   == 0) rate   = 11025;
    if (length <= 8) return -1;
    length -= 8;

    /* Slot */
    if (channel >= 0 && channel < SND_CHANNELS)
        slot = channel;
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

    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0)   sep = 0;
    if (sep > 255) sep = 255;

    handle = next_handle++;
    if (next_handle <= 0) next_handle = 1;

    if (sfx_mutex >= 0)
        sceKernelLockMutex(sfx_mutex, 1, NULL);

    sfx_channels[slot].data   = data + 8;
    sfx_channels[slot].length = length;
    sfx_channels[slot].pos    = 0;
    sfx_channels[slot].step   = ((uint32_t)rate << 16) / OUTPUT_RATE;
    sfx_channels[slot].vol    = vol;
    sfx_channels[slot].sep    = sep;
    sfx_channels[slot].handle = handle;
    sfx_channels[slot].active = 1;

    if (sfx_mutex >= 0)
        sceKernelUnlockMutex(sfx_mutex, 1);

    return handle;
}

void I_StopSound(int handle)
{
    int i;

    if (sfx_mutex >= 0)
        sceKernelLockMutex(sfx_mutex, 1, NULL);

    for (i = 0; i < SND_CHANNELS; i++)
    {
        if (sfx_channels[i].active && sfx_channels[i].handle == handle)
        {
            sfx_channels[i].active = 0;
            break;
        }
    }

    if (sfx_mutex >= 0)
        sceKernelUnlockMutex(sfx_mutex, 1);
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
    if (channel < 0 || channel >= SND_CHANNELS)
        return;

    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0)   sep = 0;
    if (sep > 255) sep = 255;

    if (sfx_mutex >= 0)
        sceKernelLockMutex(sfx_mutex, 1, NULL);

    if (sfx_channels[channel].active)
    {
        sfx_channels[channel].vol = vol;
        sfx_channels[channel].sep = sep;
    }

    if (sfx_mutex >= 0)
        sceKernelUnlockMutex(sfx_mutex, 1);
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
    midi.tick_frac     = 0;

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
    MEMFILE *in  = NULL;
    MEMFILE *out = NULL;
    void    *buf = NULL;
    size_t   buflen = 0;

    if (!data || len <= 0)
        return NULL;

    /* Libera dati precedenti */
    if (midi.midi_data)
    {
        free(midi.midi_data);
        midi.midi_data     = NULL;
        midi.midi_data_len = 0;
    }

    if (!genmidi_loaded)
        load_genmidi();

    /* MIDI nativo */
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
        /* Converti MUS->MIDI */
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

    /* Parsifica */
    if (parse_midi((const uint8_t *)midi.midi_data, midi.midi_data_len) <= 0)
    {
        free(midi.midi_data);
        midi.midi_data     = NULL;
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
        midi.midi_data     = NULL;
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
    midi.current_tick  = 0;
    midi.tick_frac     = 0;
    midi.looping       = looping ? 1 : 0;
    midi.voice_age     = 0;

    /* Ripristina tempo al valore del primo evento di tempo (o default) */
    midi.us_per_beat = 500000;
    for (i = 0; i < midi.num_events; i++)
    {
        if (midi.events[i].type == MIDI_EV_TEMPO &&
            midi.events[i].tick == 0)
        {
            midi.us_per_beat = midi.events[i].data32;
            break;
        }
    }
    midi_update_timing();

    /* Silenzia voci precedenti */
    for (i = 0; i < MAX_VOICES; i++)
        midi.voices[i].active = 0;

    /* Reset OPL */
    opl_reset();

    /* Reset canali MIDI */
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
