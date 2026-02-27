// dummy.c - Minimal stub implementations for PSP port (no headers)

typedef int boolean;
typedef unsigned char byte;
#define false 0
#define true  1

struct sfxinfo_s;
typedef struct sfxinfo_s sfxinfo_t;
struct wbstartstruct_s;
typedef struct wbstartstruct_s wbstartstruct_t;

int snd_musicdevice = 0;
int vanilla_keyboard_mapping = 1;

boolean I_SoundIsPlaying(int handle) { return false; }
int I_GetSfxLumpNum(sfxinfo_t *sfx) { return 0; }
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch) { return 0; }
void I_StopSound(int handle) {}
void I_UpdateSound(void) {}
void I_UpdateSoundParams(int channel, int vol, int sep) {}
void I_InitSound(boolean use_sfx_prefix) {}
void I_ShutdownSound(void) {}
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {}
void I_BindSoundVariables(void) {}

void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) {}
void I_PauseSong(void) {}
void I_ResumeSong(void) {}
void *I_RegisterSong(void *data, int len) { return (void*)0; }
void I_UnRegisterSong(void *handle) {}
void I_PlaySong(void *handle, boolean looping) {}
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return false; }

void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}

void I_InitInput(void) {}
void I_GetEvent(void) {}

void I_Endoom(byte *endoom_data) {}
void StatCopy(wbstartstruct_t *stats) {}
void StatDump(void) {}
