/*
 * psp_sound.c - Audio per Chex Quest PSP
 * Effetti sonori con mixing software + output hardware PSP
 * Musica: stub (non implementata)
 */

#include "doomtype.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

#include <pspaudio.h>
#include <pspthreadman.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ==================== Costanti ==================== */

#define SND_CHANNELS    8       /* Canali di mixing simultanei */
#define MIX_SAMPLES     512     /* Campioni per blocco (multiplo di 64) */
#define OUTPUT_RATE     48000   /* Frequenza output hardware PSP */

/* ==================== Canale audio ==================== */

typedef struct {
    const uint8_t  *pcm;       /* Dati PCM unsigned 8-bit */
    int             length;    /* Lunghezza in campioni sorgente */
    uint32_t        pos;       /* Posizione corrente (16.16 fixed point) */
    uint32_t        step;      /* Passo di avanzamento (16.16 fixed point) */
    int             vol;       /* Volume 0-127 */
    int             sep;       /* Separazione stereo 0-255 (128=centro) */
    int             handle;    /* Handle univoco */
    int             active;    /* 1 = in riproduzione */
} snd_ch_t;

static snd_ch_t snd_channels[SND_CHANNELS];
static int psp_audio_ch = -1;
static volatile int snd_running = 0;
static SceUID snd_thread_id = -1;
static int next_handle = 1;

static int16_t __attribute__((aligned(64))) mix_buf[MIX_SAMPLES * 2];

/* ==================== Thread di mixing ==================== */

static int snd_mix_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;

    while (snd_running)
    {
        int s, c;
        memset(mix_buf, 0, sizeof(mix_buf));

        for (s = 0; s < MIX_SAMPLES; s++)
        {
            int mix_l = 0, mix_r = 0;

            for (c = 0; c < SND_CHANNELS; c++)
            {
                int idx, sample, lv, rv;

                if (!snd_channels[c].active)
                    continue;

                /* Posizione nel buffer sorgente */
                idx = (int)(snd_channels[c].pos >> 16);
                if (idx >= snd_channels[c].length)
                {
                    snd_channels[c].active = 0;
                    continue;
                }

                /* Converti da unsigned 8-bit a signed 16-bit */
                sample = ((int)snd_channels[c].pcm[idx] - 128) << 8;

                /* Avanza posizione (resampling) */
                snd_channels[c].pos += snd_channels[c].step;

                /* Applica volume (0-127) */
                sample = (sample * snd_channels[c].vol) / 127;

                /* Applica separazione stereo */
                lv = 255 - snd_channels[c].sep;
                rv = snd_channels[c].sep;

                mix_l += (sample * lv) / 255;
                mix_r += (sample * rv) / 255;
            }

            /* Clamp a 16-bit */
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

/* ==================== Interfaccia Sound ==================== */

void I_InitSound(boolean use_sfx_prefix)
{
    (void)use_sfx_prefix;

    memset(snd_channels, 0, sizeof(snd_channels));

    psp_audio_ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL,
                                      MIX_SAMPLES,
                                      PSP_AUDIO_FORMAT_STEREO);
    if (psp_audio_ch < 0)
        return;

    snd_running = 1;
    snd_thread_id = sceKernelCreateThread("snd_mix", snd_mix_thread,
                                           0x12, 0x4000,
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
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[16];

    if (sfx->link)
        sfx = sfx->link;

    snprintf(namebuf, sizeof(namebuf), "ds%s", sfx->name);
    return W_GetNumForName(namebuf);
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
    unsigned char *raw;
    int rate, length, format_tag;
    int slot, handle;

    if (!sfxinfo || !snd_running)
        return -1;

    /* Carica dati dal WAD se necessario */
    if (!sfxinfo->data)
    {
        if (sfxinfo->lumpnum < 0)
            return -1;
        sfxinfo->data = W_CacheLumpNum(sfxinfo->lumpnum, PU_STATIC);
    }
    if (!sfxinfo->data)
        return -1;

    raw = (unsigned char *)sfxinfo->data;

    /* Verifica header formato Doom (tag = 0x0003) */
    format_tag = raw[0] | (raw[1] << 8);
    if (format_tag != 3)
        return -1;

    /* Leggi sample rate e lunghezza dall'header */
    rate   = raw[2] | (raw[3] << 8);
    length = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);

    if (rate == 0) rate = 11025;
    if (length <= 8) return -1;
    length -= 8;  /* Sottrai header */

    /* Applica pitch (128 = normale) */
    if (pitch <= 0) pitch = 128;
    rate = (rate * pitch) / 128;

    /* Usa il canale richiesto dal motore, oppure cerca uno libero */
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

    /* Clamp volume */
    if (vol < 0)   vol = 0;
    if (vol > 127) vol = 127;

    /* Clamp separazione */
    if (sep < 0)   sep = 0;
    if (sep > 255) sep = 255;

    snd_channels[slot].pcm    = raw + 8;   /* Salta header 8 byte */
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
    /* I suoni vengono caricati on-demand in I_StartSound */
}

void I_BindSoundVariables(void)
{
    /* Nessuna variabile config da registrare */
}

/* ==================== Musica (stub — non implementata) ==================== */

void I_InitMusic(void)          {}
void I_ShutdownMusic(void)      {}
void I_SetMusicVolume(int vol)  { (void)vol; }
void I_PauseSong(void)          {}
void I_ResumeSong(void)         {}
void I_StopSong(void)           {}
boolean I_MusicIsPlaying(void)  { return 0; }

void *I_RegisterSong(void *data, int len)
{
    (void)data; (void)len;
    return (void *)0;
}

void I_UnRegisterSong(void *handle) { (void)handle; }
void I_PlaySong(void *handle, boolean looping) { (void)handle; (void)looping; }
