/*
 * psp_sound.c - Audio completo per Chex Quest PSP
 * Effetti sonori + Musica MIDI con sintetizzatore software
 * 
 * - SFX: mixing multicanale dal WAD
 * - Musica: MUS→MIDI→sintesi wavetable software
 * - Tutto mixato in un unico thread audio PSP
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

/* ==================== Costanti Musica ==================== */

#define MIDI_CHANNELS   16
#define MAX_VOICES      32
#define MAX_MIDI_EVENTS 32768
#define PI_F            3.14159265358979323846f

/* Frequenze nota MIDI (A4 = 440Hz) precalcolate */
static float midi_freq_table[128];
static int freq_table_init = 0;

/* ==================== Waveform Types ==================== */

typedef enum {
    WAVE_SQUARE,
    WAVE_SAW,
    WAVE_TRIANGLE,
    WAVE_SINE,
    WAVE_NOISE
} wave_type_t;

/* Mapping GM program → waveform (semplificato per Doom/Chex) */
static wave_type_t gm_to_wave[128];

/* ==================== MIDI Event ==================== */

typedef struct {
    uint32_t    tick;
    uint8_t     type;       /* status byte */
    uint8_t     channel;
    uint8_t     data1;
    uint8_t     data2;
} midi_event_t;

/* ==================== MIDI Channel State ==================== */

typedef struct {
    uint8_t     program;
    uint8_t     volume;     /* CC7 */
    uint8_t     pan;        /* CC10 */
    uint8_t     expression; /* CC11 */
    int16_t     pitch_bend; /* -8192..+8191 */
    int         is_drum;    /* canale 9 = percussioni */
} midi_ch_state_t;

/* ==================== Synth Voice ==================== */

typedef struct {
    int         active;
    int         midi_ch;
    int         note;
    float       freq;
    float       phase;
    float       volume;     /* 0.0 .. 1.0 */
    float       pan;        /* 0.0 .. 1.0 */
    wave_type_t wave;
    
    /* Envelope ADSR semplice */
    float       env;
    float       env_target;
    float       env_speed;
    int         env_stage;  /* 0=attack, 1=sustain, 2=release, 3=off */
    
    /* Per noise */
    uint32_t    noise_state;
} synth_voice_t;

/* ==================== Stato Musica ==================== */

typedef struct {
    midi_event_t    events[MAX_MIDI_EVENTS];
    int             num_events;
    int             current_event;
    uint16_t        ticks_per_beat;
    uint32_t        us_per_beat;        /* microsecondi per beat (tempo) */
    double          samples_per_tick;
    double          tick_accum;
    uint32_t        current_tick;
    
    midi_ch_state_t channels[MIDI_CHANNELS];
    synth_voice_t   voices[MAX_VOICES];
    
    int             playing;
    int             looping;
    int             music_volume;       /* 0..127 */
    
    /* Buffer MIDI convertito */
    void           *midi_data;
    int             midi_data_len;
} music_state_t;

static music_state_t mus;

/* ==================== SFX (invariato) ==================== */

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

/* ==================== Inizializzazione tabelle ==================== */

static void init_freq_table(void)
{
    int i;
    if (freq_table_init)
        return;
    for (i = 0; i < 128; i++)
    {
        /* A4 (nota 69) = 440 Hz */
        midi_freq_table[i] = 440.0f * powf(2.0f, (i - 69) / 12.0f);
    }
    freq_table_init = 1;
}

static void init_gm_map(void)
{
    int i;
    for (i = 0; i < 128; i++)
    {
        if (i < 8)        gm_to_wave[i] = WAVE_SQUARE;    /* Piano → square */
        else if (i < 16)  gm_to_wave[i] = WAVE_TRIANGLE;  /* Chromatic perc */
        else if (i < 24)  gm_to_wave[i] = WAVE_SINE;      /* Organ → sine */
        else if (i < 32)  gm_to_wave[i] = WAVE_SAW;       /* Guitar → saw */
        else if (i < 40)  gm_to_wave[i] = WAVE_SAW;       /* Bass → saw */
        else if (i < 48)  gm_to_wave[i] = WAVE_TRIANGLE;  /* Strings */
        else if (i < 56)  gm_to_wave[i] = WAVE_TRIANGLE;  /* Ensemble */
        else if (i < 64)  gm_to_wave[i] = WAVE_SQUARE;    /* Brass → square */
        else if (i < 72)  gm_to_wave[i] = WAVE_SAW;       /* Reed → saw */
        else if (i < 80)  gm_to_wave[i] = WAVE_SINE;      /* Pipe → sine */
        else if (i < 88)  gm_to_wave[i] = WAVE_SQUARE;    /* Synth lead */
        else if (i < 96)  gm_to_wave[i] = WAVE_TRIANGLE;  /* Synth pad */
        else if (i < 104) gm_to_wave[i] = WAVE_SAW;       /* Synth effects */
        else if (i < 112) gm_to_wave[i] = WAVE_TRIANGLE;  /* Ethnic */
        else if (i < 120) gm_to_wave[i] = WAVE_NOISE;     /* Percussive */
        else               gm_to_wave[i] = WAVE_NOISE;     /* SFX */
    }
}

/* ==================== Generatore Waveform ==================== */

static float generate_sample(synth_voice_t *v)
{
    float s = 0.0f;
    float p;
    
    switch (v->wave)
    {
    case WAVE_SQUARE:
        s = (v->phase < 0.5f) ? 0.8f : -0.8f;
        break;
        
    case WAVE_SAW:
        s = 2.0f * v->phase - 1.0f;
        s *= 0.6f;
        break;
        
    case WAVE_TRIANGLE:
        p = v->phase;
        if (p < 0.25f)       s = p * 4.0f;
        else if (p < 0.75f)  s = 1.0f - (p - 0.25f) * 4.0f;
        else                  s = -1.0f + (p - 0.75f) * 4.0f;
        s *= 0.7f;
        break;
        
    case WAVE_SINE:
        /* Approssimazione veloce seno con polinomio */
        p = v->phase * 2.0f - 1.0f; /* -1..1 */
        s = p * (2.0f - (p < 0 ? -p : p)); /* parabola */
        s *= 0.7f;
        break;
        
    case WAVE_NOISE:
        v->noise_state ^= v->noise_state << 13;
        v->noise_state ^= v->noise_state >> 17;
        v->noise_state ^= v->noise_state << 5;
        s = ((float)(int32_t)v->noise_state / (float)0x7FFFFFFF) * 0.4f;
        break;
    }
    
    return s;
}

/* ==================== Envelope ==================== */

static void update_envelope(synth_voice_t *v)
{
    switch (v->env_stage)
    {
    case 0: /* Attack */
        v->env += 0.01f;
        if (v->env >= 1.0f)
        {
            v->env = 1.0f;
            v->env_stage = 1;
        }
        break;
        
    case 1: /* Sustain */
        /* Leggero decay */
        v->env *= 0.99998f;
        if (v->env < 0.001f)
        {
            v->env = 0.0f;
            v->env_stage = 3;
            v->active = 0;
        }
        break;
        
    case 2: /* Release */
        v->env -= 0.002f;
        if (v->env <= 0.0f)
        {
            v->env = 0.0f;
            v->env_stage = 3;
            v->active = 0;
        }
        break;
        
    case 3: /* Off */
        v->active = 0;
        break;
    }
}

/* ==================== MIDI Parser ==================== */

/* Legge variable-length quantity da buffer MIDI */
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
    int num_tracks;
    int format;
    int track;
    
    mus.num_events = 0;
    mus.ticks_per_beat = 140; /* default */
    mus.us_per_beat = 500000; /* 120 BPM default */
    
    /* Verifica header "MThd" */
    if (len < 14)
        return 0;
    if (data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
        return 0;
    
    /* Lunghezza header (sempre 6) */
    pos = 8;
    
    format = (data[pos] << 8) | data[pos + 1]; pos += 2;
    num_tracks = (data[pos] << 8) | data[pos + 1]; pos += 2;
    mus.ticks_per_beat = (data[pos] << 8) | data[pos + 1]; pos += 2;
    
    if (mus.ticks_per_beat == 0)
        mus.ticks_per_beat = 140;
    
    (void)format;
    
    /* Parse ogni traccia */
    for (track = 0; track < num_tracks && pos < len; track++)
    {
        int track_end;
        int track_len;
        uint32_t abs_tick = 0;
        uint8_t running_status = 0;
        
        /* Verifica "MTrk" */
        if (pos + 8 > len)
            break;
        if (data[pos] != 'M' || data[pos+1] != 'T' || 
            data[pos+2] != 'r' || data[pos+3] != 'k')
        {
            break;
        }
        pos += 4;
        
        track_len = (data[pos] << 24) | (data[pos+1] << 16) |
                    (data[pos+2] << 8) | data[pos+3];
        pos += 4;
        
        track_end = pos + track_len;
        if (track_end > len)
            track_end = len;
        
        while (pos < track_end && mus.num_events < MAX_MIDI_EVENTS)
        {
            uint32_t delta;
            uint8_t status, type, chan;
            midi_event_t *ev;
            
            delta = read_vlq(data, &pos, track_end);
            abs_tick += delta;
            
            if (pos >= track_end)
                break;
            
            /* Leggi status byte */
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
            case 0x80: /* Note Off */
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
                
            case 0x90: /* Note On */
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
                
            case 0xA0: /* Poly Aftertouch */
                pos += 2;
                break;
                
            case 0xB0: /* Control Change */
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
                
            case 0xC0: /* Program Change */
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
                
            case 0xD0: /* Channel Aftertouch */
                pos += 1;
                break;
                
            case 0xE0: /* Pitch Bend */
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
                
            case 0xF0: /* System / Meta */
                if (status == 0xFF) /* Meta event */
                {
                    uint8_t meta_type;
                    uint32_t meta_len;
                    
                    if (pos >= track_end)
                        goto done_track;
                    
                    meta_type = data[pos++];
                    meta_len = read_vlq(data, &pos, track_end);
                    
                    if (meta_type == 0x51 && meta_len == 3 && pos + 3 <= track_end)
                    {
                        /* Set Tempo */
                        mus.us_per_beat = ((uint32_t)data[pos] << 16) |
                                          ((uint32_t)data[pos+1] << 8) |
                                          (uint32_t)data[pos+2];
                        if (mus.us_per_beat == 0)
                            mus.us_per_beat = 500000;
                    }
                    else if (meta_type == 0x2F)
                    {
                        /* End of Track */
                        pos += meta_len;
                        goto done_track;
                    }
                    
                    pos += meta_len;
                }
                else if (status == 0xF0 || status == 0xF7) /* SysEx */
                {
                    uint32_t sysex_len = read_vlq(data, &pos, track_end);
                    pos += sysex_len;
                }
                else
                {
                    /* Skip sconosciuto */
                    break;
                }
                break;
                
            default:
                /* Status sconosciuto, skip */
                break;
            }
        }
        
done_track:
        pos = track_end;
    }
    
    /* Calcola samples_per_tick */
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

/* ==================== Sorting eventi per tick ==================== */

static void sort_events(void)
{
    /* Insertion sort - gli eventi sono quasi ordinati per traccia */
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

/* ==================== Synth: gestione voci ==================== */

static synth_voice_t *alloc_voice(void)
{
    int i;
    synth_voice_t *oldest = NULL;
    float lowest_env = 999.0f;
    
    /* Cerca voce libera */
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (!mus.voices[i].active)
            return &mus.voices[i];
    }
    
    /* Ruba voce con envelope più basso */
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (mus.voices[i].env < lowest_env)
        {
            lowest_env = mus.voices[i].env;
            oldest = &mus.voices[i];
        }
    }
    
    return oldest;
}

static void synth_note_on(int channel, int note, int velocity)
{
    synth_voice_t *v;
    midi_ch_state_t *ch;
    float vel_f;
    
    if (note < 0 || note > 127 || velocity == 0)
    {
        /* velocity 0 = note off */
        synth_voice_t *vv;
        int i;
        for (i = 0; i < MAX_VOICES; i++)
        {
            vv = &mus.voices[i];
            if (vv->active && vv->midi_ch == channel && vv->note == note)
            {
                vv->env_stage = 2; /* Release */
            }
        }
        return;
    }
    
    ch = &mus.channels[channel];
    v = alloc_voice();
    if (!v)
        return;
    
    vel_f = (float)velocity / 127.0f;
    
    v->active   = 1;
    v->midi_ch  = channel;
    v->note     = note;
    v->freq     = midi_freq_table[note];
    v->phase    = 0.0f;
    v->volume   = vel_f * ((float)ch->volume / 127.0f) * 
                  ((float)ch->expression / 127.0f);
    v->pan      = (float)ch->pan / 127.0f;
    v->env      = 0.0f;
    v->env_stage = 0; /* Attack */
    
    if (ch->is_drum)
    {
        v->wave = WAVE_NOISE;
        v->noise_state = 0x12345678 ^ (uint32_t)(note * 7919);
    }
    else
    {
        v->wave = gm_to_wave[ch->program];
        v->noise_state = 0xDEADBEEF;
    }
}

static void synth_note_off(int channel, int note)
{
    int i;
    for (i = 0; i < MAX_VOICES; i++)
    {
        if (mus.voices[i].active && 
            mus.voices[i].midi_ch == channel && 
            mus.voices[i].note == note)
        {
            mus.voices[i].env_stage = 2; /* Release */
        }
    }
}

static void synth_control_change(int channel, int cc, int value)
{
    midi_ch_state_t *ch = &mus.channels[channel];
    
    switch (cc)
    {
    case 7:  /* Volume */
        ch->volume = (uint8_t)value;
        break;
    case 10: /* Pan */
        ch->pan = (uint8_t)value;
        break;
    case 11: /* Expression */
        ch->expression = (uint8_t)value;
        break;
    case 123: /* All Notes Off */
    case 120: /* All Sound Off */
        {
            int i;
            for (i = 0; i < MAX_VOICES; i++)
            {
                if (mus.voices[i].active && mus.voices[i].midi_ch == channel)
                {
                    mus.voices[i].env_stage = 2;
                }
            }
        }
        break;
    }
}

static void synth_program_change(int channel, int program)
{
    mus.channels[channel].program = (uint8_t)program;
}

static void synth_pitch_bend(int channel, int lsb, int msb)
{
    int bend = ((int)msb << 7) | lsb;
    mus.channels[channel].pitch_bend = (int16_t)(bend - 8192);
}

/* ==================== Process MIDI events ==================== */

static void process_midi_event(midi_event_t *ev)
{
    uint8_t type = ev->type & 0xF0;
    int ch = ev->channel;
    
    switch (type)
    {
    case 0x90:
        synth_note_on(ch, ev->data1, ev->data2);
        break;
    case 0x80:
        synth_note_off(ch, ev->data1);
        break;
    case 0xB0:
        synth_control_change(ch, ev->data1, ev->data2);
        break;
    case 0xC0:
        synth_program_change(ch, ev->data1);
        break;
    case 0xE0:
        synth_pitch_bend(ch, ev->data1, ev->data2);
        break;
    }
}

/* ==================== Synth render (un sample stereo) ==================== */

static void synth_render(float *out_l, float *out_r)
{
    int i;
    float ml = 0.0f, mr = 0.0f;
    float master;
    
    for (i = 0; i < MAX_VOICES; i++)
    {
        synth_voice_t *v = &mus.voices[i];
        float s, lv, rv;
        float pb_mult;
        
        if (!v->active)
            continue;
        
        update_envelope(v);
        if (!v->active)
            continue;
        
        /* Pitch bend */
        pb_mult = 1.0f;
        if (mus.channels[v->midi_ch].pitch_bend != 0)
        {
            float bend_semi = (float)mus.channels[v->midi_ch].pitch_bend 
                              / 8192.0f * 2.0f;
            pb_mult = powf(2.0f, bend_semi / 12.0f);
        }
        
        /* Genera sample */
        s = generate_sample(v);
        s *= v->env * v->volume;
        
        /* Avanza fase */
        v->phase += (v->freq * pb_mult) / (float)OUTPUT_RATE;
        while (v->phase >= 1.0f)
            v->phase -= 1.0f;
        
        /* Pan */
        lv = 1.0f - v->pan;
        rv = v->pan;
        
        ml += s * lv;
        mr += s * rv;
    }
    
    /* Volume master musica */
    master = (float)mus.music_volume / 127.0f * 0.3f;
    
    *out_l = ml * master;
    *out_r = mr * master;
}

/* ==================== Avanza sequencer ==================== */

static void music_advance(int num_samples)
{
    if (!mus.playing || mus.num_events == 0)
        return;
    
    mus.tick_accum += (double)num_samples;
    
    while (mus.tick_accum >= mus.samples_per_tick)
    {
        mus.tick_accum -= mus.samples_per_tick;
        mus.current_tick++;
        
        /* Esegui tutti gli eventi al tick corrente */
        while (mus.current_event < mus.num_events &&
               mus.events[mus.current_event].tick <= mus.current_tick)
        {
            process_midi_event(&mus.events[mus.current_event]);
            mus.current_event++;
        }
        
        /* Fine brano */
        if (mus.current_event >= mus.num_events)
        {
            if (mus.looping)
            {
                /* Reset e riparti */
                int i;
                mus.current_event = 0;
                mus.current_tick = 0;
                mus.tick_accum = 0;
                
                /* Spegni tutte le voci */
                for (i = 0; i < MAX_VOICES; i++)
                {
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

/* ==================== Thread di mixing unificato ==================== */

static int snd_mix_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;

    while (snd_running)
    {
        int s, c;
        memset(mix_buf, 0, sizeof(mix_buf));

        /* Avanza il sequencer MIDI */
        if (mus.playing)
            music_advance(MIX_SAMPLES);

        for (s = 0; s < MIX_SAMPLES; s++)
        {
            int mix_l = 0, mix_r = 0;

            /* === SFX mixing === */
            for (c = 0; c < SND_CHANNELS; c++)
            {
                int idx, sample, lv, rv;

                if (!snd_channels[c].active)
                    continue;

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

            /* === Musica mixing === */
            if (mus.playing)
            {
                float mus_l = 0.0f, mus_r = 0.0f;
                synth_render(&mus_l, &mus_r);
                mix_l += (int)(mus_l * 32767.0f);
                mix_r += (int)(mus_r * 32767.0f);
            }

            /* Clamp */
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

/* ==================== Interfaccia Sound (SFX) ==================== */

void I_InitSound(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;

    memset(snd_channels, 0, sizeof(snd_channels));

    if (!sfx_cache_init)
    {
        memset(sfx_cache, 0, sizeof(sfx_cache));
        sfx_cache_init = 1;
    }

    init_freq_table();
    init_gm_map();

    /* Init stato musica */
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
    if (psp_audio_ch < 0)
        return;

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
    
    /* Libera dati MIDI */
    if (mus.midi_data)
    {
        free(mus.midi_data);
        mus.midi_data = NULL;
    }
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[16];

    if (sfx->link)
        sfx = sfx->link;

    snprintf(namebuf, sizeof(namebuf), "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    void *raw_data;
    unsigned char *raw;
    int lumpnum;
    int rate, length, format_tag;
    int slot, handle;

    if (!sfxinfo || !snd_running)
        return -1;

    lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0 || lumpnum >= 2048)
        return -1;

    if (!sfx_cache[lumpnum])
        sfx_cache[lumpnum] = W_CacheLumpNum(lumpnum, PU_STATIC);

    raw_data = sfx_cache[lumpnum];
    if (!raw_data)
        return -1;

    raw = (unsigned char *)raw_data;

    format_tag = raw[0] | (raw[1] << 8);
    if (format_tag != 3)
        return -1;

    rate   = raw[2] | (raw[3] << 8);
    length = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);

    if (rate == 0) rate = 11025;
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
            if (!snd_channels[i].active)
            {
                slot = i;
                break;
            }
        }
    }

    handle = next_handle++;

    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    if (sep < 0)   sep = 0;
    if (sep > 255) sep = 255;

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
    {
        if (snd_channels[i].active && snd_channels[i].handle == handle)
        {
            snd_channels[i].active = 0;
            break;
        }
    }
}

boolean I_SoundIsPlaying(int handle)
{
    int i;
    for (i = 0; i < SND_CHANNELS; i++)
    {
        if (snd_channels[i].active && snd_channels[i].handle == handle)
            return 1;
    }
    return 0;
}

void I_UpdateSound(void)
{
    /* Il mixing avviene nel thread dedicato */
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (channel >= 0 && channel < SND_CHANNELS && snd_channels[channel].active)
    {
        if (vol < 0)   vol = 0;
        if (vol > 127) vol = 127;
        if (sep < 0)   sep = 0;
        if (sep > 255) sep = 255;
        snd_channels[channel].vol = vol;
        snd_channels[channel].sep = sep;
    }
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    (void)sounds; (void)num_sounds;
}

void I_BindSoundVariables(void)
{
}

/* ==================== Musica: Implementazione reale ==================== */

void I_InitMusic(void)
{
    /* Già inizializzato in I_InitSound */
}

void I_ShutdownMusic(void)
{
    I_StopSong();
    if (mus.midi_data)
    {
        free(mus.midi_data);
        mus.midi_data = NULL;
    }
}

void I_SetMusicVolume(int vol)
{
    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;
    mus.music_volume = vol;
}

void I_PauseSong(void)
{
    mus.playing = 0;
}

void I_ResumeSong(void)
{
    if (mus.num_events > 0)
        mus.playing = 1;
}

void I_StopSong(void)
{
    int i;
    
    mus.playing = 0;
    mus.current_event = 0;
    mus.current_tick = 0;
    mus.tick_accum = 0;
    
    /* Spegni tutte le voci */
    for (i = 0; i < MAX_VOICES; i++)
    {
        mus.voices[i].active = 0;
    }
    
    /* Reset canali */
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        mus.channels[i].volume = 100;
        mus.channels[i].pan = 64;
        mus.channels[i].expression = 127;
        mus.channels[i].program = 0;
        mus.channels[i].pitch_bend = 0;
    }
}

boolean I_MusicIsPlaying(void)
{
    return mus.playing ? 1 : 0;
}

void *I_RegisterSong(void *data, int len)
{
    MEMFILE *instream = NULL;
    MEMFILE *outstream = NULL;
    void *outbuf = NULL;
    size_t outlen = 0;
    
    if (!data || len <= 0)
        return NULL;
    
    /* Libera dati precedenti */
    if (mus.midi_data)
    {
        free(mus.midi_data);
        mus.midi_data = NULL;
        mus.midi_data_len = 0;
    }
    
    /* Controlla se è già MIDI (header "MThd") */
    if (len >= 4 && 
        ((uint8_t *)data)[0] == 'M' && 
        ((uint8_t *)data)[1] == 'T' &&
        ((uint8_t *)data)[2] == 'h' && 
        ((uint8_t *)data)[3] == 'd')
    {
        /* È già MIDI */
        mus.midi_data = malloc(len);
        if (!mus.midi_data)
            return NULL;
        memcpy(mus.midi_data, data, len);
        mus.midi_data_len = len;
    }
    else
    {
        /* Prova conversione MUS → MIDI */
        instream = mem_fopen_read(data, len);
        if (!instream)
            return NULL;
        
        outstream = mem_fopen_write();
        if (!outstream)
        {
            mem_fclose(instream);
            return NULL;
        }
        
        if (mus2mid(instream, outstream) != 0)
        {
            /* Conversione fallita */
            mem_fclose(instream);
            mem_fclose(outstream);
            return NULL;
        }
        
        outbuf = mem_fread_all(outstream, &outlen);
        
        mem_fclose(instream);
        mem_fclose(outstream);
        
        if (!outbuf || outlen == 0)
            return NULL;
        
        mus.midi_data = malloc(outlen);
        if (!mus.midi_data)
        {
            free(outbuf);
            return NULL;
        }
        memcpy(mus.midi_data, outbuf, outlen);
        mus.midi_data_len = (int)outlen;
        free(outbuf);
    }
    
    /* Parse il MIDI */
    if (parse_midi((const uint8_t *)mus.midi_data, mus.midi_data_len) <= 0)
    {
        free(mus.midi_data);
        mus.midi_data = NULL;
        mus.midi_data_len = 0;
        return NULL;
    }
    
    /* Ordina eventi per tick */
    sort_events();
    
    /* Ritorna handle non-NULL */
    return (void *)1;
}

void I_UnRegisterSong(void *handle)
{
    (void)handle;
    
    I_StopSong();
    
    if (mus.midi_data)
    {
        free(mus.midi_data);
        mus.midi_data = NULL;
        mus.midi_data_len = 0;
    }
    mus.num_events = 0;
}

void I_PlaySong(void *handle, boolean looping)
{
    int i;
    
    (void)handle;
    
    if (mus.num_events == 0)
        return;
    
    /* Reset playback */
    mus.current_event = 0;
    mus.current_tick = 0;
    mus.tick_accum = 0;
    mus.looping = looping ? 1 : 0;
    
    /* Ricalcola samples_per_tick */
    if (mus.ticks_per_beat > 0 && mus.us_per_beat > 0)
    {
        double secs_per_tick = (double)mus.us_per_beat / 
                               (double)mus.ticks_per_beat / 1000000.0;
        mus.samples_per_tick = secs_per_tick * OUTPUT_RATE;
    }
    
    /* Spegni voci precedenti */
    for (i = 0; i < MAX_VOICES; i++)
        mus.voices[i].active = 0;
    
    /* Reset canali */
    for (i = 0; i < MIDI_CHANNELS; i++)
    {
        mus.channels[i].volume = 100;
        mus.channels[i].pan = 64;
        mus.channels[i].expression = 127;
        mus.channels[i].pitch_bend = 0;
    }
    
    mus.playing = 1;
}
