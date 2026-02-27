/*
 * psp_sound.c - Audio per Chex Quest PSP
 * Effetti sonori con mixing software + output hardware PSP
 * Corretto per doomgeneric API (I_StartSound a 4 parametri, no sfxinfo->data)
 */

#include "doomtype.h"
#include "i_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

#include <pspaudio.h>
#include <pspthreadman.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ==================== Costanti ==================== */

#define SND_CHANNELS    8
#define MIX_SAMPLES     512
#define OUTPUT_RATE     48000

/* ==================== Canale audio ==================== */

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

/* Cache dati audio per ogni lump */
static void *sfx_cache[2048];
static int sfx_cache_init = 0;

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

    if (!sfx_cache_init)
    {
        memset(sfx_cache, 0, sizeof(sfx_cache));
        sfx_cache_init = 1;
    }

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

/*
 * I_StartSound - firma corretta per doomgeneric: 4 parametri, NO pitch
 */
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    void *raw_data;
    unsigned char *raw;
    int lumpnum;
    int rate, length, format_tag;
    int slot, handle;

    if (!sfxinfo || !snd_running)
        return -1;

    /* Ottieni il numero del lump */
    lumpnum = sfxinfo->lumpnum;
    if (lumpnum < 0 || lumpnum >= 2048)
        return -1;

    /* Carica dati dal WAD usando cache locale */
    if (!sfx_cache[lumpnum])
        sfx_cache[lumpnum] = W_CacheLumpNum(lumpnum, PU_STATIC);

    raw_data = sfx_cache[lumpnum];
    if (!raw_data)
        return -1;

    raw = (unsigned char *)raw_data;

    /* Verifica header formato Doom (tag = 0x0003) */
    format_tag = raw[0] | (raw[1] << 8);
    if (format_tag != 3)
        return -1;

    /* Leggi sample rate e lunghezza dall'header */
    rate   = raw[2] | (raw[3] << 8);
    length = raw[4] | (raw[5] << 8) | (raw[6] << 16) | (raw[7] << 24);

    if (rate == 0) rate = 11025;
    if (length <= 8) return -1;
    length -= 8;

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

/* ==================== Musica (stub) ==================== */

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
