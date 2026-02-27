// dummy.c - Stub implementations for PSP port
// FIX: I_GetEvent now bridges DG_GetKey -> Doom engine

#include "doomgeneric.h"

typedef int boolean;
typedef unsigned char byte;
#define false 0
#define true  1

/* Event types - deve corrispondere a d_event.h */
typedef enum { ev_keydown, ev_keyup, ev_mouse, ev_joystick } evtype_t;
typedef struct { evtype_t type; int data1; int data2; int data3; } event_t;
extern void D_PostEvent(event_t *ev);

struct sfxinfo_s;
typedef struct sfxinfo_s sfxinfo_t;
struct wbstartstruct_s;
typedef struct wbstartstruct_s wbstartstruct_t;

/* Global variables */
int snd_musicdevice = 0;
int vanilla_keyboard_mapping = 1;

/* Network variables */
boolean drone = 0;
boolean net_client_connected = 0;

/* Sound */
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

/* Music */
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

/* Joystick */
void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}

/* Input init - può restare vuota */
void I_InitInput(void) {}

/*
 * ===== FIX CRITICO =====
 * I_GetEvent: legge i tasti dalla coda PSP (via DG_GetKey)
 * e li passa al motore Doom (via D_PostEvent).
 * PRIMA era vuota → Doom non riceveva MAI nessun tasto!
 */
void I_GetEvent(void)
{
    event_t event;
    int pressed;
    unsigned char key;

    while (DG_GetKey(&pressed, &key))
    {
        event.type = pressed ? ev_keydown : ev_keyup;
        event.data1 = key;
        event.data2 = -1;
        event.data3 = -1;
        D_PostEvent(&event);
    }
}

/* Misc */
void I_Endoom(byte *endoom_data) {}
void StatCopy(wbstartstruct_t *stats) {}
void StatDump(void) {}
