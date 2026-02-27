// dummy.c - Stub implementations for PSP port

#include <stdlib.h>
#include "doomtype.h"
#include "d_event.h"
#include "sounds.h"
#include "d_mode.h"

// ============================================================
// Sound stubs (FEATURE_SOUND=0)
// ============================================================

int snd_musicdevice = 0;

boolean I_SoundIsPlaying(int handle)
{
    return false;
}

int I_GetSfxLumpNum(sfxinfo_t *sfx)
{
    return 0;
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch)
{
    return 0;
}

void I_StopSound(int handle)
{
}

void I_UpdateSound(void)
{
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
}

void I_InitSound(boolean use_sfx_prefix)
{
}

void I_ShutdownSound(void)
{
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
}

void I_BindSoundVariables(void)
{
}

// ============================================================
// Music stubs
// ============================================================

void I_InitMusic(void)
{
}

void I_ShutdownMusic(void)
{
}

void I_SetMusicVolume(int volume)
{
}

void I_PauseSong(void)
{
}

void I_ResumeSong(void)
{
}

void *I_RegisterSong(void *data, int len)
{
    return NULL;
}

void I_UnRegisterSong(void *handle)
{
}

void I_PlaySong(void *handle, boolean looping)
{
}

void I_StopSong(void)
{
}

boolean I_MusicIsPlaying(void)
{
    return false;
}

// ============================================================
// Joystick stubs
// ============================================================

void I_InitJoystick(void)
{
}

void I_BindJoystickVariables(void)
{
}

// ============================================================
// Input stubs (se gestisci l'input in doomgeneric_psp.c tramite
// DG_GetTicksMs / DG_SleepMs / DG_GetKey, queste possono restare vuote)
// ============================================================

void I_InitInput(void)
{
}

void I_GetEvent(void)
{
}

// ============================================================
// Misc stubs
// ============================================================

int vanilla_keyboard_mapping = 1;

void I_Endoom(byte *endoom_data)
{
    // La PSP non ha una console testo, ignora l'endoom screen
}

#include "doomstat.h"

void StatCopy(wbstartstruct_t *stats)
{
}

void StatDump(void)
{
}
