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

// Network variables (NEW - fixes d_loop.c and d_main.c)
boolean drone = false;
boolean net_client_connected = false;

// Sound
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
boolean I_MusicIsPlaying(void) { return false; }

// Joystick
void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}

// Input
void I_InitInput(void) {}
void I_GetEvent(void) {}

// Misc
void I_Endoom(byte *endoom_data) {}
void StatCopy(wbstartstruct_t *stats) {}
void StatDump(void) {}
