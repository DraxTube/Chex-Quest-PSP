// dummy.c - Stub implementations for PSP doomgeneric port
// No-op implementations for sound, music, joystick, input,
// endoom, statistics, and keyboard mapping.

#include "doomtype.h"
#include "sounds.h"
#include "d_event.h"

// Forward declaration to avoid heavy includes
struct wbstartstruct_s;
typedef struct wbstartstruct_s wbstartstruct_t;

// =============================================================
// Sound stubs
// =============================================================

void I_InitSound(boolean use_sfx_prefix) {}
void I_ShutdownSound(void) {}
int I_GetSfxLumpNum(sfxinfo_t *sfx) { return 0; }
void I_UpdateSound(void) {}
void I_UpdateSoundParams(int handle, int vol, int sep) {}
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) { return 0; }
void I_StopSound(int handle) {}
boolean I_SoundIsPlaying(int handle) { return false; }
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {}

// =============================================================
// Music stubs
// =============================================================

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) {}
void I_PauseSong(void) {}
void I_ResumeSong(void) {}
void *I_RegisterSong(void *data, int len) { return NULL; }
void I_UnRegisterSong(void *handle) {}
void I_PlaySong(void *handle, boolean looping) {}
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return false; }

// =============================================================
// Sound/Music config stubs and variables
// =============================================================

void I_BindSoundVariables(void) {}
int snd_musicdevice = 0;

// =============================================================
// Joystick stubs
// =============================================================

void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}

// =============================================================
// Input stubs (actual input handled via DG_GetKey in
// doomgeneric_psp.c)
// =============================================================

void I_InitInput(void) {}
void I_GetEvent(void) {}

// =============================================================
// Endoom screen stub
// =============================================================

void I_Endoom(byte *data) {}

// =============================================================
// Keyboard mapping variable
// =============================================================

int vanilla_keyboard_mapping = 0;

// =============================================================
// Statistics stubs
// =============================================================

void StatCopy(wbstartstruct_t *stats) {}
void StatDump(void) {}
