/*
 * psp_sound.c - Audio per Chex Quest PSP
 * SFX: mixing multicanale dal WAD
 * Musica: MUS→MIDI → OPL2 FM Synthesis (emulazione Yamaha OPL2)
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
#define OUTPUT_RATE     48000

/* ==================== OPL2 FM Synthesis ==================== */

#define OPL_RATE        49716
#define OPL_CHANNELS    9
#define OPL_NUM_OPS     18

#define SINE_TABLE_SIZE 1024

#define ENV_ATTACK  0
#define ENV_DECAY   1
#define ENV_SUSTAIN 2
#define ENV_RELEASE 3
#define ENV_OFF     4

#define WAVE_SINE       0
#define WAVE_HALFSINE   1
#define WAVE_ABSSINE    2
#define WAVE_QUARTSINE  3

/* ==================== OPL2 Operator ==================== */

typedef struct {
    int         waveform;
    int         mult;
    int         ksl;
    int         tl;
    int         ar;
    int         dr;
    int         sl;
    int         rr;
    int         am;
    int         vib;
    int         egt;
    int         ksr;

    uint32_t    phase;
    uint32_t    phase_inc;
    int         env_stage;
    int32_t     env_level;
    int32_t     env_target;
    int32_t     env_rate;
    int         key_on;
} opl_op_t;

/* ==================== OPL2 Channel ==================== */

typedef struct {
    opl_op_t    op[2];
    int         freq;
    int         octave;
    int         key_on;
    int         feedback;
    int         algorithm;
    int32_t     fb_buf[2];
    int         vol_atten;      /* Attenuazione volume dinamica */
} opl_ch_t;

/* ==================== OPL2 Chip ==================== */

typedef struct {
    opl_ch_t    channels[OPL_CHANNELS];
    int32_t     sine_table[SINE_TABLE_SIZE];
    int32_t     exp_table[256];
    int         tremolo_pos;
    int         vibrato_pos;
    uint32_t    sample_cnt;
    
    int32_t     resample_accum;
    int32_t     resample_last;
} opl_chip_t;

static opl_chip_t opl;

/* ==================== GENMIDI Structures ==================== */

typedef struct {
    uint8_t     modulator_tremolo;
    uint8_t     carrier_tremolo;
    uint8_t     modulator_level;
    uint8_t     carrier_level;
    uint8_t     modulator_attack;
    uint8_t     carrier_attack;
    uint8_t     modulator_sustain;
    uint8_t     carrier_sustain;
    uint8_t     modulator_wave;
    uint8_t     carrier_wave;
    uint8_t     feedback;
} genmidi_voice_t;

typedef struct {
    uint16_t    flags;
    uint8_t     fine_tuning;
    uint8_t     fixed_note;
    genmidi_voice_t voice[2];
} genmidi_instr_t;

#define GENMIDI_NUM_INSTRS  175
#define GENMIDI_HEADER_SIZE 8
#define GENMIDI_FLAG_FIXED  0x0001
#define GENMIDI_FLAG_2VOICE 0x0004

static genmidi_instr_t genmidi_instrs[GENMIDI_NUM_INSTRS];
static int genmidi_loaded = 0;

/* ==================== MIDI Sequencer ==================== */

#define MIDI_CHANNELS   16
#define MAX_MIDI_EVENTS 32768
#define MAX_VOICES_ACTIVE 9

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
    int         is_drum;
} midi_ch_state_t;

typedef struct {
    int         active;
    int         midi_ch;
    int         note;
    int         opl_ch;
    int         velocity;
    uint32_t    age;
} voice_alloc_t;

typedef struct {
    midi_event_t    events[MAX_MIDI_EVENTS];
    int             num_events;
    int             current_event;
    uint16_t        ticks_per_beat;
    uint32_t        us_per_beat;
    double          samples_per_tick;
    double          tick_accum;
    uint32_t        current_tick;
    uint32_t        age_counter;

    midi_ch_state_t channels[MIDI_CHANNELS];
    voice_alloc_t   voices[MAX_VOICES_ACTIVE];

    int             playing;
    int             looping;
    int             music_volume;

    void           *midi_data;
    int             midi_data_len;
} music_state_t;

static music_state_t mus;

/* ==================== SFX ==================== */

typedef struct {
    const uint8_t  *pcm;
    int             length;
    uint32_t        pos;
    uint32_t        step;
    int             vol;
    int             sep;
    int             handle;
    int             active;
} snd_ch_t;

static snd_ch_t snd_channels[SND_CHANNELS];
static int psp_audio_ch = -1;
static volatile int snd_running = 0;
static SceUID snd_thread_id = -1;
static int next_handle = 1;

static void *sfx_cache[2048];
static int sfx_cache_init = 0;

static int16_t __attribute__((aligned(64))) mix_buf[MIX_SAMPLES * 2];

/* ==================== Tables ==================== */

static const int mult_table[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

static const int fnumber_table[12] = {
    0x158, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,
    0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287
};

static const int32_t ar_increment[16] = {
    0, 4, 8, 12, 16, 24, 32, 48,
    64, 96, 128, 192, 256, 384, 512, 0x1FF
};

static const int32_t dr_increment[16] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 10, 12, 16, 20, 24, 32, 48
};

/* ==================== OPL2 Init ==================== */

static void opl_init(void)
{
    int i;
    
    memset(&opl, 0, sizeof(opl));
    
    for (i = 0; i < SINE_TABLE_SIZE; i++)
    {
        double phase = ((double)i + 0.5) / SINE_TABLE_SIZE * 3.14159265358979323846 / 2.0;
        double val = sin(phase);
        
        if (val > 0.0)
        {
            double logval = -log(val) / log(2.0) * 256.0;
            opl.sine_table[i] = (int32_t)logval;
            if (opl.sine_table[i] > 0xFFF)
                opl.sine_table[i] = 0xFFF;
        }
        else
        {
            opl.sine_table[i] = 0xFFF;
        }
    }
    
    for (i = 0; i < 256; i++)
    {
        double val = pow(2.0, (double)(255 - i) / 256.0) * 1024.0;
        opl.exp_table[i] = (int32_t)(val + 0.5);
    }
    
    for (i = 0; i < OPL_CHANNELS; i++)
    {
        opl.channels[i].op[0].env_stage = ENV_OFF;
        opl.channels[i].op[1].env_stage = ENV_OFF;
        opl.channels[i].op[0].env_level = 0x1FF;
        opl.channels[i].op[1].env_level = 0x1FF;
        opl.channels[i].vol_atten = 0;
    }
}

/* ==================== OPL2 Lookup Waveform ==================== */

static int32_t opl_lookup_sine(int waveform, uint32_t phase)
{
    int index;
    int negate = 0;
    int32_t result;
    
    phase &= 0x3FF;
    
    switch (waveform)
    {
    case WAVE_SINE:
        if (phase & 0x200)
            negate = 1;
        if (phase & 0x100)
            index = (SINE_TABLE_SIZE - 1) - (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        else
            index = (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        break;
        
    case WAVE_HALFSINE:
        if (phase & 0x200)
            return 0xFFF;
        if (phase & 0x100)
            index = (SINE_TABLE_SIZE - 1) - (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        else
            index = (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        break;
        
    case WAVE_ABSSINE:
        if (phase & 0x100)
            index = (SINE_TABLE_SIZE - 1) - (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        else
            index = (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        break;
        
    case WAVE_QUARTSINE:
        if (phase & 0x100)
            return 0xFFF;
        index = (int)(phase & 0xFF) * SINE_TABLE_SIZE / 256;
        break;
        
    default:
        index = 0;
        break;
    }
    
    if (index < 0) index = 0;
    if (index >= SINE_TABLE_SIZE) index = SINE_TABLE_SIZE - 1;
    
    result = opl.sine_table[index];
    
    if (negate)
        result |= 0x8000;
    
    return result;
}

static int32_t opl_exp_out(int32_t log_sin)
{
    int negate = (log_sin & 0x8000) ? 1 : 0;
    int32_t level = log_sin & 0x7FFF;
    int32_t exp_index;
    int32_t result;
    int shift;
    
    if (level >= 0x1000)
        return 0;
    
    exp_index = level & 0xFF;
    shift = level >> 8;
    
    result = opl.exp_table[exp_index];
    result >>= shift;
    
    if (negate)
        result = -result;
    
    return result;
}

/* ==================== OPL2 Envelope ==================== */

static void opl_env_advance(opl_op_t *op)
{
    switch (op->env_stage)
    {
    case ENV_ATTACK:
        if (op->ar >= 15)
        {
            op->env_level = 0;
            op->env_stage = ENV_DECAY;
        }
        else if (op->ar > 0)
        {
            int32_t inc = ar_increment[op->ar];
            op->env_level -= inc;
            if (op->env_level <= 0)
            {
                op->env_level = 0;
                op->env_stage = ENV_DECAY;
            }
        }
        break;
        
    case ENV_DECAY:
        if (op->dr > 0)
        {
            int32_t target = (int32_t)op->sl << 5;
            int32_t inc = dr_increment[op->dr];
            op->env_level += inc;
            if (op->env_level >= target)
            {
                op->env_level = target;
                op->env_stage = ENV_SUSTAIN;
            }
        }
        else
        {
            op->env_stage = ENV_SUSTAIN;
        }
        break;
        
    case ENV_SUSTAIN:
        if (!op->egt)
        {
            int32_t inc = dr_increment[op->rr];
            op->env_level += inc;
            if (op->env_level >= 0x1FF)
            {
                op->env_level = 0x1FF;
                op->env_stage = ENV_OFF;
            }
        }
        break;
        
    case ENV_RELEASE:
        {
            int32_t inc = dr_increment[op->rr];
            if (inc == 0) inc = 1;
            op->env_level += inc;
            if (op->env_level >= 0x1FF)
            {
                op->env_level = 0x1FF;
                op->env_stage = ENV_OFF;
            }
        }
        break;
        
    case ENV_OFF:
        op->env_level = 0x1FF;
        break;
    }
}

/* ==================== OPL2 Key On/Off ==================== */

static void opl_key_on(int ch)
{
    opl_ch_t *c = &opl.channels[ch];
    
    c->key_on = 1;
    
    c->op[0].phase = 0;
    c->op[0].env_level = 0x1FF;
    c->op[0].env_stage = ENV_ATTACK;
    c->op[0].key_on = 1;
    
    c->op[1].phase = 0;
    c->op[1].env_level = 0x1FF;
    c->op[1].env_stage = ENV_ATTACK;
    c->op[1].key_on = 1;
    
    c->fb_buf[0] = 0;
    c->fb_buf[1] = 0;
}

static void opl_key_off(int ch)
{
    opl_ch_t *c = &opl.channels[ch];
    
    c->key_on = 0;
    
    if (c->op[0].env_stage != ENV_OFF)
        c->op[0].env_stage = ENV_RELEASE;
    c->op[0].key_on = 0;
    
    if (c->op[1].env_stage != ENV_OFF)
        c->op[1].env_stage = ENV_RELEASE;
    c->op[1].key_on = 0;
}

/* ==================== OPL2 Set Frequency (FIXED) ==================== */

static void opl_set_freq(int ch, int fnum, int block)
{
    opl_ch_t *c = &opl.channels[ch];
    uint32_t freq_val;
    uint32_t rate_scale;
    
    c->freq = fnum;
    c->octave = block;
    
    freq_val = (uint32_t)fnum << block;
    
    /* FIX: Compensa per OUTPUT_RATE vs OPL_RATE */
    rate_scale = (OUTPUT_RATE << 8) / OPL_RATE;
    
    c->op[0].phase_inc = ((freq_val * (uint32_t)mult_table[c->op[0].mult] * rate_scale) >> 8) >> 1;
    c->op[1].phase_inc = ((freq_val * (uint32_t)mult_table[c->op[1].mult] * rate_scale) >> 8) >> 1;
}

/* ==================== OPL2 Program Operator ==================== */

static void opl_program_op(opl_op_t *op, const uint8_t tremolo_byte,
                            const uint8_t level_byte,
                            const uint8_t attack_byte,
                            const uint8_t sustain_byte,
                            const uint8_t wave_byte)
{
    op->am   = (tremolo_byte >> 7) & 1;
    op->vib  = (tremolo_byte >> 6) & 1;
    op->egt  = (tremolo_byte >> 5) & 1;
    op->ksr  = (tremolo_byte >> 4) & 1;
    op->mult = tremolo_byte & 0x0F;
    
    op->ksl = (level_byte >> 6) & 3;
    op->tl  = level_byte & 0x3F;
    
    op->ar = (attack_byte >> 4) & 0x0F;
    op->dr = attack_byte & 0x0F;
    
    op->sl = (sustain_byte >> 4) & 0x0F;
    op->rr = sustain_byte & 0x0F;
    
    op->waveform = wave_byte & 0x03;
}

/* ==================== OPL2 Generate Sample (FIXED) ==================== */

static int32_t opl_generate_sample(void)
{
    int ch;
    int32_t output = 0;
    
    opl.sample_cnt++;
    
    opl.tremolo_pos = (opl.tremolo_pos + 1) % (OUTPUT_RATE * 1000 / 3700);
    opl.vibrato_pos = (opl.vibrato_pos + 1) % (OUTPUT_RATE * 1000 / 6100);
    
    for (ch = 0; ch < OPL_CHANNELS; ch++)
    {
        opl_ch_t *c = &opl.channels[ch];
        opl_op_t *mod = &c->op[0];
        opl_op_t *car = &c->op[1];
        int32_t mod_out, car_out;
        int32_t mod_log, car_log;
        uint32_t mod_phase, car_phase;
        int32_t feedback_val;
        int32_t mod_env_total, car_env_total;
        
        if (mod->env_stage == ENV_OFF && car->env_stage == ENV_OFF)
            continue;
        
        opl_env_advance(mod);
        opl_env_advance(car);
        
        mod->phase += mod->phase_inc;
        car->phase += car->phase_inc;
        
        mod_env_total = mod->env_level + ((int32_t)mod->tl << 2);
        /* FIX: Applica vol_atten solo al carrier, non modifica TL originale */
        car_env_total = car->env_level + ((int32_t)car->tl << 2) + c->vol_atten;
        
        if (mod_env_total > 0x1FF) mod_env_total = 0x1FF;
        if (car_env_total > 0x1FF) car_env_total = 0x1FF;
        
        mod_phase = (mod->phase >> 6) & 0x3FF;
        
        if (c->feedback > 0)
        {
            feedback_val = (c->fb_buf[0] + c->fb_buf[1]) >> (9 - c->feedback);
            mod_phase = (mod_phase + (uint32_t)feedback_val) & 0x3FF;
        }
        
        mod_log = opl_lookup_sine(mod->waveform, mod_phase);
        mod_log = (mod_log & 0x8000) | 
                  ((mod_log & 0x7FFF) + (mod_env_total << 3));
        mod_out = opl_exp_out(mod_log);
        
        c->fb_buf[1] = c->fb_buf[0];
        c->fb_buf[0] = mod_out;
        
        car_phase = (car->phase >> 6) & 0x3FF;
        
        if (c->algorithm == 0)
        {
            car_phase = (car_phase + (uint32_t)(mod_out >> 1)) & 0x3FF;
        }
        
        car_log = opl_lookup_sine(car->waveform, car_phase);
        car_log = (car_log & 0x8000) |
                  ((car_log & 0x7FFF) + (car_env_total << 3));
        car_out = opl_exp_out(car_log);
        
        if (c->algorithm == 0)
        {
            output += car_out;
        }
        else
        {
            output += mod_out + car_out;
        }
    }
    
    output >>= 1;
    
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    
    return output;
}

/* ==================== GENMIDI Loading ==================== */

static void load_genmidi(void)
{
    int lumpnum;
    const uint8_t *data;
    int len, i, pos;
    
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
    
    if (len < (int)(GENMIDI_HEADER_SIZE + sizeof(genmidi_instr_t) * GENMIDI_NUM_INSTRS))
    {
        genmidi_loaded = -1;
        return;
    }
    
    if (memcmp(data, "#OPL_II#", 8) != 0)
    {
        genmidi_loaded = -1;
        return;
    }
    
    pos = GENMIDI_HEADER_SIZE;
    for (i = 0; i < GENMIDI_NUM_INSTRS; i++)
    {
        const uint8_t *p = &data[pos];
        int v;
        
        genmidi_instrs[i].flags = p[0] | (p[1] << 8);
        genmidi_instrs[i].fine_tuning = p[2];
        genmidi_instrs[i].fixed_note = p[3];
        
        pos += 4;
        
        for (v = 0; v < 2; v++)
        {
            const uint8_t *vp = &data[pos];
            genmidi_instrs[i].voice[v].modulator_tremolo = vp[0];
            genmidi_instrs[i].voice[v].carrier_tremolo   = vp[1];
            genmidi_instrs[i].voice[v].modulator_level   = vp[2];
            genmidi_instrs[i].voice[v].carrier_level     = vp[3];
            genmidi_instrs[i].voice[v].modulator_attack  = vp[4];
            genmidi_instrs[i].voice[v].carrier_attack    = vp[5];
            genmidi_instrs[i].voice[v].modulator_sustain = vp[6];
            genmidi_instrs[i].voice[v].carrier_sustain   = vp[7];
            genmidi_instrs[i].voice[v].modulator_wave    = vp[8];
            genmidi_instrs[i].voice[v].carrier_wave      = vp[9];
            genmidi_instrs[i].voice[v].feedback          = vp[10];
            pos += 11;
        }
    }
    
    genmidi_loaded = 1;
}

/* ==================== Apply GENMIDI voice to OPL channel ==================== */

static void apply_genmidi_voice(int opl_ch, const genmidi_voice_t *gv)
{
    opl_ch_t *c = &opl.channels[opl_ch];
    
    opl_program_op(&c->op[0],
                   gv->modulator_tremolo,
                   gv->modulator_level,
                   gv->modulator_attack,
                   gv->modulator_sustain,
                   gv->modulator_wave);
    
    opl_program_op(&c->op[1],
                   gv->carrier_tremolo,
                   gv->carrier_level,
                   gv->carrier_attack,
                   gv->carrier_sustain,
                   gv->carrier_wave);
    
    c->feedback = (gv->feedback >> 1) & 0x07;
    c->algorithm = gv->feedback & 0x01;
    c->vol_atten = 0; /* Reset attenuazione */
}

/* ==================== MIDI → OPL2 ==================== */

static int alloc_opl_voice(int midi_ch, int note)
{
    int i;
    int oldest_idx = -1;
    uint32_t oldest_age = 0xFFFFFFFF;
    
    (void)midi_ch;
    (void)note;
    
    for (i = 0; i < MAX_VOICES_ACTIVE; i++)
    {
        if (!mus.voices[i].active)
            return i;
    }
    
    for (i = 0; i < MAX_VOICES_ACTIVE; i++)
    {
        if (mus.voices[i].age < oldest_age)
        {
            oldest_age = mus.voices[i].age;
            oldest_idx = i;
        }
    }
    
    if (oldest_idx >= 0)
    {
        opl_key_off(oldest_idx);
        mus.voices[oldest_idx].active = 0;
    }
    
    return oldest_idx;
}

static void midi_note_on(int midi_ch, int note, int velocity)
{
    int slot, opl_ch_idx;
    int instr_idx;
    const genmidi_instr_t *instr;
    int fnum, block, real_note;
    float vol_scale;
    int vol_atten;
    
    if (velocity == 0)
    {
        int i;
        for (i = 0; i < MAX_VOICES_ACTIVE; i++)
        {
            if (mus.voices[i].active && 
                mus.voices[i].midi_ch == midi_ch &&
                mus.voices[i].note == note)
            {
                opl_key_off(i);
                mus.voices[i].active = 0;
                break;
            }
        }
        return;
    }
    
    if (genmidi_loaded != 1)
        return;
    
    if (mus.channels[midi_ch].is_drum)
    {
        instr_idx = 128 + (note - 35);
        if (instr_idx < 128 || instr_idx >= GENMIDI_NUM_INSTRS)
            return;
    }
    else
    {
        instr_idx = mus.channels[midi_ch].program;
        if (instr_idx >= 128)
            instr_idx = 0;
    }
    
    instr = &genmidi_instrs[instr_idx];
    
    slot = alloc_opl_voice(midi_ch, note);
    if (slot < 0)
        return;
    
    opl_ch_idx = slot;
    
    apply_genmidi_voice(opl_ch_idx, &instr->voice[0]);
    
    if (instr->flags & GENMIDI_FLAG_FIXED)
        real_note = instr->fixed_note;
    else
        real_note = note;
    
    if (real_note < 0) real_note = 0;
    if (real_note > 127) real_note = 127;
    
    block = (real_note / 12) - 1;
    if (block < 0) block = 0;
    if (block > 7) block = 7;
    
    fnum = fnumber_table[real_note % 12];
    
    /* FIX: Calcola attenuazione senza modificare TL */
    vol_scale = ((float)velocity / 127.0f) *
                ((float)mus.channels[midi_ch].volume / 127.0f) *
                ((float)mus.channels[midi_ch].expression / 127.0f) *
                ((float)mus.music_volume / 127.0f);
    
    vol_atten = (int)((1.0f - vol_scale) * 128.0f);
    if (vol_atten < 0) vol_atten = 0;
    if (vol_atten > 0x1FF) vol_atten = 0x1FF;
    
    opl.channels[opl_ch_idx].vol_atten = vol_atten;
    
    opl_set_freq(opl_ch_idx, fnum, block);
    opl_key_on(opl_ch_idx);
    
    mus.voices[slot].active = 1;
    mus.voices[slot].midi_ch = midi_ch;
    mus.voices[slot].note = note;
    mus.voices[slot].opl_ch = opl_ch_idx;
    mus.voices[slot].velocity = velocity;
    mus.voices[slot].age = mus.age_counter++;
}

static void midi_note_off(int midi_ch, int note)
{
    int i;
    for (i = 0; i < MAX_VOICES_ACTIVE; i++)
    {
        if (mus.voices[i].active &&
            mus.voices[i].midi_ch == midi_ch &&
            mus.voices[i].note == note)
        {
            opl_key_off(mus.voices[i].opl_ch);
            mus.voices[i].active = 0;
            break;
        }
    }
}

static void midi_control_change(int channel, int cc, int value)
{
    midi_ch_state_t *ch = &mus.channels[channel];
    
    switch (cc)
    {
    case 7:
        ch->volume = (uint8_t)value;
        break;
    case 10:
        ch->pan = (uint8_t)value;
        break;
    case 11:
        ch->expression = (uint8_t)value;
        break;
    case 123:
    case 120:
        {
            int i;
            for (i = 0; i < MAX_VOICES_ACTIVE; i++)
            {
                if (mus.voices[i].active && mus.voices[i].midi_ch == channel)
                {
                    opl_key_off(mus.voices[i].opl_ch);
                    mus.voices[i].active = 0;
                }
            }
        }
        break;
    }
}

static void midi_program_change(int channel, int program)
{
    mus.channels[channel].program = (uint8_t)program;
}

/* ==================== MIDI Parser ==================== */

static uint32_t read_vlq(const uint8_t *data, int *pos, int len)
{
    uint32_t val = 0;
    uint8_t b;
    int max_bytes = 4;
    
    do {
        if (*pos >= len)
            return val;
        b = data[(*pos)++];
        val = (val << 7) | (b & 0x7F);
        max_bytes--;
    } while ((b & 0x80) && max_bytes > 0);
    
    return val;
}

static int parse_midi(const uint8_t *data, int len)
{
    int pos = 0;
    int num_tracks, format, track;
    
    mus.num_events = 0;
    mus.ticks_per_beat = 140;
    mus.us_per_beat = 500000;
    
    if (len < 14)
        return 0;
    if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
        return 0;
    
    pos = 8;
    format = (data[pos] << 8) | data[pos + 1]; pos += 2;
    num_tracks = (data[pos] << 8) | data[pos + 1]; pos += 2;
    mus.ticks_per_beat = (data[pos] << 8) | data[pos + 1]; pos += 2;
    
    if (mus.ticks_per_beat == 0)
        mus.ticks_per_beat = 140;
    
    (void)format;
    
    for (track = 0; track < num_tracks && pos < len; track++)
    {
        int track_end, track_len;
        uint32_t abs_tick = 0;
        uint8_t running_status = 0;
        
        if (pos + 8 > len) break;
        if (data[pos] != 'M' || data[pos+1] != 'T' ||
            data[pos+2] != 'r' || data[pos+3] != 'k')
            break;
        pos += 4;
        
        track_len = (data[pos] << 24) | (data[pos+1] << 16) |
                    (data[pos+2] << 8) | data[pos+3];
        pos += 4;
        
        track_end = pos + track_len;
        if (track_end > len) track_end = len;
        
        while (pos < track_end && mus.num_events < MAX_MIDI_EVENTS)
        {
            uint32_t delta;
            uint8_t status, type, chan;
            midi_event_t *ev;
            
            delta = read_vlq(data, &pos, track_end);
            abs_tick += delta;
            
            if (pos >= track_end) break;
            
            status = data[pos];
            if (status & 0x80)
            {
                running_status = status;
                pos++;
            }
            else
            {
                status = running_status;
            }
            
            type = status & 0xF0;
            chan = status & 0x0F;
            
            switch (type)
            {
            case 0x80:
                if (pos + 1 < track_end)
                {
                    ev = &mus.events[mus.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0x80;
                    ev->channel = chan;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos + 1];
                    pos += 2;
                }
                break;
            case 0x90:
                if (pos + 1 < track_end)
                {
                    ev = &mus.events[mus.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0x90;
                    ev->channel = chan;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos + 1];
                    pos += 2;
                }
                break;
            case 0xA0:
                pos += 2;
                break;
            case 0xB0:
                if (pos + 1 < track_end)
                {
                    ev = &mus.events[mus.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0xB0;
                    ev->channel = chan;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos + 1];
                    pos += 2;
                }
                break;
            case 0xC0:
                if (pos < track_end)
                {
                    ev = &mus.events[mus.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0xC0;
                    ev->channel = chan;
                    ev->data1 = data[pos];
                    ev->data2 = 0;
                    pos += 1;
                }
                break;
            case 0xD0:
                pos += 1;
                break;
            case 0xE0:
                if (pos + 1 < track_end)
                {
                    ev = &mus.events[mus.num_events++];
                    ev->tick = abs_tick;
                    ev->type = 0xE0;
                    ev->channel = chan;
                    ev->data1 = data[pos];
                    ev->data2 = data[pos + 1];
                    pos += 2;
                }
                break;
            case 0xF0:
                if (status == 0xFF)
                {
                    uint8_t meta_type;
                    uint32_t meta_len;
                    
                    if (pos >= track_end) goto done_track;
                    meta_type = data[pos++];
                    meta_len = read_vlq(data, &pos, track_end);
                    
                    if (meta_type == 0x51 && meta_len == 3 && pos + 3 <= track_end)
                    {
                        mus.us_per_beat = ((uint32_t)data[pos] << 16) |
                                          ((uint32_t)data[pos+1] << 8) |
                                          (uint32_t)data[pos+2];
                        if (mus.us_per_beat == 0) mus.us_per_beat = 500000;
                    }
                    else if (meta_type == 0x2F)
                    {
                        pos += meta_len;
                        goto done_track;
                    }
                    pos += meta_len;
                }
                else if (status == 0xF0 || status == 0xF7)
                {
                    uint32_t sysex_len = read_vlq(data, &pos, track_end);
                    pos += sysex_len;
                }
                break;
            default:
                break;
            }
        }
done_track:
        pos = track_end;
    }
    
    if (mus.ticks_per_beat > 0 && mus.us_per_beat > 0)
    {
        double secs_per_tick = (double)mus.us_per_beat /
                               (double)mus.ticks_per_beat / 1000000.0;
        mus.samples_per_tick = secs_per_tick * OUTPUT_RATE;
    }
    else
    {
        mus.samples_per_tick = OUTPUT_RATE / 140.0;
    }
    
    return mus.num_events;
}

static void sort_events(void)
{
    int i, j;
    midi_event_t tmp;
    for (i = 1; i < mus.num_events; i++)
    {
        tmp = mus.events[i];
        j = i - 1;
        while (j >= 0 && mus.events[j].tick > tmp.tick)
        {
            mus.events[j + 1] = mus.events[j];
            j--;
        }
        mus.events[j + 1] = tmp;
    }
}

/* ==================== Process MIDI event ==================== */

static void process_midi_event(midi_event_t *ev)
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
    case 0xE0:
        break;
    }
}

/* ==================== Music advance ==================== */

static void music_advance(int num_samples)
{
    if (!mus.playing || mus.num_events == 0)
        return;
    
    mus.tick_accum += (double)num_samples;
    
    while (mus.tick_accum >= mus.samples_per_tick)
    {
        mus.tick_accum -= mus.samples_per_tick;
        mus.current_tick++;
        
        while (mus.current_event < mus.num_events &&
               mus.events[mus.current_event].tick <= mus.current_tick)
        {
            process_midi_event(&mus.events[mus.current_event]);
            mus.current_event++;
        }
        
        if (mus.current_event >= mus.num_events)
        {
            if (mus.looping)
            {
                int i;
                mus.current_event = 0;
                mus.current_tick = 0;
                mus.tick_accum = 0;
                for (i = 0; i < MAX_VOICES_ACTIVE; i++)
                {
                    if (mus.voices[i].active)
                        opl_key_off(mus.voices[i].opl_ch);
                    mus.voices[i].active = 0;
                }
            }
            else
            {
                mus.playing = 0;
            }
            break;
        }
    }
}

/* ==================== Audio Mix Thread ==================== */

static int snd_mix_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;

    while (snd_running)
    {
        int s, c;
        memset(mix_buf, 0, sizeof(mix_buf));

        if (mus.playing)
            music_advance(MIX_SAMPLES);

        for (s = 0; s < MIX_SAMPLES; s++)
        {
            int mix_l = 0, mix_r = 0;

            for (c = 0; c < SND_CHANNELS; c++)
            {
                int idx, sample, lv, rv;
                if (!snd_channels[c].active) continue;
                idx = (int)(snd_channels[c].pos >> 16);
                if (idx >= snd_channels[c].length)
                {
                    snd_channels[c].active = 0;
                    continue;
                }
                sample = ((int)snd_channels[c].pcm[idx] - 128) << 8;
                snd_channels[c].pos += snd_channels[c].step;
                sample = (sample * snd_channels[c].vol) / 127;
                lv = 255 - snd_channels[c].sep;
                rv = snd_channels[c].sep;
                mix_l += (sample * lv) / 255;
                mix_r += (sample * rv) / 255;
            }

            if (mus.playing)
            {
                int32_t opl_sample = opl_generate_sample();
                mix_l += opl_sample;
                mix_r += opl_sample;
            }

            if (mix_l >  32767) mix_l =  32767;
            if (mix_l < -32768) mix_l = -32768;
            if (mix_r >  32767) mix_r =  32767;
            if (mix_r < -32768) mix_r = -32768;

            mix_buf[s * 2]     = (int16_t)mix_l;
            mix_buf[s * 2 + 1] = (int16_t)mix_r;
        }

        sceAudioOutputBlocking(psp_audio_ch, PSP_AUDIO_VOLUME_MAX, mix_buf);
    }
    return 0;
}

/* ==================== Sound Interface ==================== */

void I_InitSound(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;
    
    memset(snd_channels, 0, sizeof(snd_channels));
    if (!sfx_cache_init)
    {
        memset(sfx_cache, 0, sizeof(sfx_cache));
        sfx_cache_init = 1;
    }
    
    opl_init();
    
    memset(&mus, 0, sizeof(mus));
    mus.music_volume = 100;
    mus.us_per_beat = 500000;
    {
        int i;
        for (i = 0; i < MIDI_CHANNELS; i++)
        {
            mus.channels[i].volume = 100;
            mus.channels[i].pan = 64;
            mus.channels[i].expression = 127;
            mus.channels[i].program = 0;
            mus.channels[i].pitch_bend = 0;
            mus.channels[i].is_drum = (i == 9) ? 1 : 0;
        }
    }
    
    psp_audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                      MIX_SAMPLES,
                                      PSP_AUDIO_FORMAT_STEREO);
    if (psp_audio_ch < 0) return;
    
    snd_running = 1;
    snd_thread_id = sceKernelCreateThread("snd_mix", snd_mix_thread,
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
    if (mus.midi_data)
    {
        free(mus.midi_data);
        mus.midi_data = NULL;
    }
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[16];
    if (sfx->link) sfx = sfx->link;
    snprintf(namebuf, sizeof(namebuf), "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    void *raw_data;
    unsigned char *raw;
    int lumpnum, rate, length, format_tag, slot, handle;

    if (!sfxinfo || !snd_running) return -1;
    lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0 || lumpnum >= 2048) return -1;
    if (!sfx_cache[lumpnum])
        sfx_cache[lumpnum] = W_CacheLumpNum(lumpnum, PU_STATIC);
    raw_data = sfx_cache[lumpnum];
    if (!raw_data) return -1;
    raw = (unsigned char *)raw_data;
    format_tag = raw[0] | (raw[1] << 8);
    if (format_tag != 3) return -1;
    rate   = raw[2] | (raw[3] << 8);
    length = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);
    if (rate == 0) rate = 11025;
    if (length <= 8) return -1;
    length -= 8;

    if (channel >= 0 && channel < SND_CHANNELS)
        slot = channel;
    else
    {
        int i; slot = 0;
        for (i = 0; i < SND_CHANNELS; i++)
            if (!snd_channels[i].active) { slot = i; break; }
    }
    handle = next_handle++;
    if (vol < 0) vol = 0; if (vol > 127) vol = 127;
    if (sep < 0) sep = 0; if (sep > 255) sep = 255;
    snd_channels[slot].pcm    = raw + 8;
    snd_channels[slot].length = length;
    snd_channels[slot].pos    = 0;
    snd_channels[slot].step   = ((uint32_t)rate << 16) / OUTPUT_RATE;
    snd_channels[slot].vol    = vol;
    snd_channels[slot].sep    = sep;
    snd_channels[slot].handle = handle;
    snd_channels[slot].active = 1;
    return handle;
}

void I_StopSound(int handle)
{
    int i;
    for (i = 0; i < SND_CHANNELS; i++)
        if (snd_channels[i].active && snd_channels[i].handle == handle)
        { snd_channels[i].active = 0; break; }
}

boolean I_SoundIsPlaying(int handle)
{
    int i;
    for (i = 0; i < SND_CHANNELS; i++)
        if (snd_channels[i].active && snd_channels[i].handle == handle)
            return 1;
    return 0;
}

void I_UpdateSound(void) {}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel >= 0 && channel < SND_CHANNELS && snd_channels[channel].active)
    {
        if (vol < 0) vol = 0; if (vol > 127) vol = 127;
        if (sep < 0) sep = 0; if (sep > 255) sep = 255;
        snd_channels[channel].vol = vol;
        snd_channels[channel].sep = sep;
    }
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{ (void)sounds; (void)num_sounds; }

void I_BindSoundVariables(void) {}

/* ==================== Music Interface ==================== */

void I_InitMusic(void)
{
    load_genmidi();
}

void I_ShutdownMusic(void)
{
    I_StopSong();
    if (mus.midi_data) { free(mus.midi_data); mus.midi_data = NULL; }
}

void I_SetMusicVolume(int vol)
{
    if (vol < 0) vol = 0; if (vol > 127) vol = 127;
    mus.music_volume = vol;
}

void I_PauseSong(void)  { mus.playing = 0; }

void I_ResumeSong(void)
{
    if (mus.num_events > 0) mus.playing = 1;
}

void I_StopSong(void)
{
    int i;
    mus.playing = 0;
    mus.current_event = 0;
    mus.current_tick = 0;
    mus.tick_accum = 0;
    for (i = 0; i < MAX_VOICES_ACTIVE; i++)
    {
        if (mus.voices[i].active)
            opl_key_off(mus.voices[i].opl_ch);
        mus.voices[i].active = 0;
    }
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        mus.channels[i].volume = 100;
        mus.channels[i].pan = 64;
        mus.channels[i].expression = 127;
        mus.channels[i].program = 0;
        mus.channels[i].pitch_bend = 0;
    }
}

boolean I_MusicIsPlaying(void) { return mus.playing ? 1 : 0; }

void *I_RegisterSong(void *data, int len)
{
    MEMFILE *instream = NULL;
    MEMFILE *outstream = NULL;
    void *outbuf = NULL;
    size_t outlen = 0;

    if (!data || len <= 0) return NULL;
    if (mus.midi_data) { free(mus.midi_data); mus.midi_data = NULL; mus.midi_data_len = 0; }

    if (!genmidi_loaded)
        load_genmidi();

    if (len >= 4 &&
        ((uint8_t *)data)[0] == 'M' && ((uint8_t *)data)[1] == 'T' &&
        ((uint8_t *)data)[2] == 'h' && ((uint8_t *)data)[3] == 'd')
    {
        mus.midi_data = malloc(len);
        if (!mus.midi_data) return NULL;
        memcpy(mus.midi_data, data, len);
        mus.midi_data_len = len;
    }
    else
    {
        instream = mem_fopen_read(data, len);
        if (!instream) return NULL;
        outstream = mem_fopen_write();
        if (!outstream) { mem_fclose(instream); return NULL; }
        if (mus2mid(instream, outstream) != 0)
        { mem_fclose(instream); mem_fclose(outstream); return NULL; }

        mem_get_buf(outstream, &outbuf, &outlen);
        if (!outbuf || outlen == 0)
        { mem_fclose(instream); mem_fclose(outstream); return NULL; }

        mus.midi_data = malloc(outlen);
        if (!mus.midi_data)
        { mem_fclose(instream); mem_fclose(outstream); return NULL; }
        memcpy(mus.midi_data, outbuf, outlen);
        mus.midi_data_len = (int)outlen;
        mem_fclose(instream);
        mem_fclose(outstream);
    }

    if (parse_midi((const uint8_t *)mus.midi_data, mus.midi_data_len) <= 0)
    { free(mus.midi_data); mus.midi_data = NULL; mus.midi_data_len = 0; return NULL; }

    sort_events();
    return (void *)1;
}

void I_UnRegisterSong(void *handle)
{
    (void)handle;
    I_StopSong();
    if (mus.midi_data) { free(mus.midi_data); mus.midi_data = NULL; mus.midi_data_len = 0; }
    mus.num_events = 0;
}

void I_PlaySong(void *handle, boolean looping)
{
    int i;
    (void)handle;
    if (mus.num_events == 0) return;

    mus.current_event = 0;
    mus.current_tick = 0;
    mus.tick_accum = 0;
    mus.looping = looping ? 1 : 0;
    mus.age_counter = 0;

    if (mus.ticks_per_beat > 0 && mus.us_per_beat > 0)
    {
        double secs_per_tick = (double)mus.us_per_beat /
                               (double)mus.ticks_per_beat / 1000000.0;
        mus.samples_per_tick = secs_per_tick * OUTPUT_RATE;
    }

    for (i = 0; i < MAX_VOICES_ACTIVE; i++)
        mus.voices[i].active = 0;

    opl_init();

    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        mus.channels[i].volume = 100;
        mus.channels[i].pan = 64;
        mus.channels[i].expression = 127;
        mus.channels[i].pitch_bend = 0;
    }

    mus.playing = 1;
}
