// dummy.c - Stub implementations for PSP port

typedef int boolean;
typedef unsigned char byte;
#define false 0
#define true  1

struct sfxinfo_s;
typedef struct sfxinfo_s sfxinfo_t;
struct wbstartstruct_s;
typedef struct wbstartstruct_s wbstartstruct_t;

// Global variables
int snd_musicdevice = 0;
int vanilla_keyboard_mapping = 1;

// Network variables
boolean drone = 0;
boolean net_client_connected = 0;

// Sound
boolean I_SoundIsPlaying(int handle) { return 0; }
int I_GetSfxLumpNum(sfxinfo_t *sfx) { return 0; }
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep, int pitch) { return 0; }
void I_StopSound(int handle) {}
void I_UpdateSound(void) {}
void I_UpdateSoundParams(int channel, int vol, int sep) {}
void I_InitSound(boolean use_sfx_prefix) {}
void I_ShutdownSound(void) {}
void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {}
void I_BindSoundVariables(void) {}

// Music
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) {}
void I_PauseSong(void) {}
void I_ResumeSong(void) {}
void *I_RegisterSong(void *data, int len) { return (void*)0; }
void I_UnRegisterSong(void *handle) {}
void I_PlaySong(void *handle, boolean looping) {}
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return 0; }

// Joystick
void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}

// Input - IMPORTANT: these must exist but stay empty
// The actual input is handled by poll_input() in doomgeneric_psp.c
// which feeds the key queue read by DG_GetKey()
void I_InitInput(void) {}
void I_GetEvent(void) {}

// Video - i_video stubs if not already provided
// These may already be defined in i_video.c, if so remove from here
// void I_InitGraphics(void) {}
// void I_ShutdownGraphics(void) {}

// Misc
void I_Endoom(byte *endoom_data) {}
void StatCopy(wbstartstruct_t *stats) {}
void StatDump(void) {}
