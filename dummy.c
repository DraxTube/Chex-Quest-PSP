/*
 * dummy.c - Stub per funzioni non-audio su PSP
 * Audio spostato in psp_sound.c
 */

#include "doomgeneric.h"

typedef int boolean;
typedef unsigned char byte;
#define false 0
#define true  1

/* Event system */
typedef enum { ev_keydown, ev_keyup, ev_mouse, ev_joystick } evtype_t;
typedef struct { evtype_t type; int data1; int data2; int data3; } event_t;
extern void D_PostEvent(event_t *ev);

struct wbstartstruct_s;
typedef struct wbstartstruct_s wbstartstruct_t;

/* Variabili globali config */
int snd_musicdevice = 0;
int vanilla_keyboard_mapping = 1;

/* Network (non usato) */
boolean drone = 0;
boolean net_client_connected = 0;

/* Input init */
void I_InitInput(void) {}

/*
 * I_GetEvent: BRIDGE CRITICO tra input PSP e motore Doom
 * Legge dalla coda PSP (DG_GetKey) e passa a Doom (D_PostEvent)
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
    void I_GetEvent(void)
{
    event_t event;
    int pressed;
    unsigned char key;

    while (DG_GetKey(&pressed, &key))
    {
        /* DEBUG: logga ogni tasto ricevuto */
        {
            extern FILE *dbg_file;
            if (dbg_file) {
                fprintf(dbg_file, "KEY: pressed=%d key=%d (0x%02x)\n", pressed, key, key);
                fflush(dbg_file);
            }
        }

        event.type = pressed ? ev_keydown : ev_keyup;
        event.data1 = key;
        event.data2 = -1;
        event.data3 = -1;
        D_PostEvent(&event);
    }
}
}

/* Joystick (gestito via analog stick in poll_input) */
void I_InitJoystick(void) {}
void I_BindJoystickVariables(void) {}

/* Misc */
void I_Endoom(byte *endoom_data) { (void)endoom_data; }
void StatCopy(wbstartstruct_t *stats) { (void)stats; }
void StatDump(void) {}
