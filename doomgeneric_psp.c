/*
 * doomgeneric_psp.c - PSP platform for doomgeneric (Chex Quest)
 */

#include "doomgeneric.h"
#include "doomkeys.h"

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspgum.h>
#include <psppower.h>
#include <psprtc.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ==================== PSP Module Info ==================== */

PSP_MODULE_INFO("ChexQuest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

/* ==================== Constants ==================== */

#define SCR_W       480
#define SCR_H       272
#define BUF_W       512
#define FRAME_SIZE  (BUF_W * SCR_H * 4)

/* ==================== Exit Callbacks ==================== */

static int running = 1;

static int exit_cb(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    running = 0;
    sceKernelExitGame();
    return 0;
}

static int cb_thread(SceSize args, void *argp)
{
    (void)args; (void)argp;
    int cbid = sceKernelCreateCallback("exit_cb", exit_cb, NULL);
    sceKernelRegisterExitCallback(cbid);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int thid = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (thid >= 0) sceKernelStartThread(thid, 0, 0);
}

/* ==================== GU / Display ==================== */

static unsigned int __attribute__((aligned(16))) gu_list[262144];
static unsigned int __attribute__((aligned(16))) tex_buf[512 * 272];

typedef struct {
    unsigned short u, v;
    short x, y, z;
} Vertex;

static void gu_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list);

    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void*)FRAME_SIZE, BUF_W);

    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);

    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuEnable(GU_TEXTURE_2D);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void draw_framebuffer(void)
{
    int y, x;

    if (!DG_ScreenBuffer) return;

    /* DoomGeneric: XRGB 0x00RRGGBB  ->  PSP GU: ABGR 0xFFBBGGRR */
    for (y = 0; y < DOOMGENERIC_RESY && y < SCR_H; y++) {
        uint32_t *src = &DG_ScreenBuffer[y * DOOMGENERIC_RESX];
        uint32_t *dst = &tex_buf[y * 512];
        for (x = 0; x < DOOMGENERIC_RESX && x < SCR_W; x++) {
            uint32_t p = src[x];
            uint32_t r = (p >> 16) & 0xFF;
            uint32_t g = (p >> 8)  & 0xFF;
            uint32_t b = (p)       & 0xFF;
            dst[x] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }

    sceKernelDcacheWritebackInvalidateAll();

    sceGuStart(GU_DIRECT, gu_list);
    sceGuClearColor(0xFF000000);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuTexImage(0, 512, 512, 512, tex_buf);

    Vertex *v = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));
    v[0].u = 0;                  v[0].v = 0;
    v[0].x = 0;                  v[0].y = 0;                  v[0].z = 0;
    v[1].u = DOOMGENERIC_RESX;   v[1].v = DOOMGENERIC_RESY;
    v[1].x = SCR_W;              v[1].y = SCR_H;              v[1].z = 0;

    sceGuDrawArray(GU_SPRITES,
        GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
        2, 0, v);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ==================== Input ==================== */

#define KEYQ_SIZE 64

static struct {
    int pressed;
    unsigned char key;
} keyq[KEYQ_SIZE];

static int keyq_head = 0;
static int keyq_tail = 0;
static SceCtrlData pad_prev;

static void keyq_push(int pressed, unsigned char doomkey)
{
    int next = (keyq_head + 1) % KEYQ_SIZE;
    if (next == keyq_tail) return;
    keyq[keyq_head].pressed = pressed;
    keyq[keyq_head].key = doomkey;
    keyq_head = next;
}

static void check_btn(uint32_t old_b, uint32_t new_b,
                       uint32_t mask, unsigned char doomkey)
{
    if ((new_b & mask) && !(old_b & mask))  keyq_push(1, doomkey);
    if (!(new_b & mask) && (old_b & mask))  keyq_push(0, doomkey);
}

static void poll_input(void)
{
    SceCtrlData pad;
    int ax, ay, oax, oay;
    int thr = 50;

    sceCtrlPeekBufferPositive(&pad, 1);

    uint32_t ob = pad_prev.Buttons;
    uint32_t nb = pad.Buttons;

    check_btn(ob, nb, PSP_CTRL_UP,       KEY_UPARROW);
    check_btn(ob, nb, PSP_CTRL_DOWN,     KEY_DOWNARROW);
    check_btn(ob, nb, PSP_CTRL_LEFT,     KEY_LEFTARROW);
    check_btn(ob, nb, PSP_CTRL_RIGHT,    KEY_RIGHTARROW);
    check_btn(ob, nb, PSP_CTRL_CROSS,    KEY_FIRE);
    check_btn(ob, nb, PSP_CTRL_CIRCLE,   KEY_USE);
    check_btn(ob, nb, PSP_CTRL_SQUARE,   KEY_STRAFE_L);
    check_btn(ob, nb, PSP_CTRL_TRIANGLE, KEY_STRAFE_R);
    check_btn(ob, nb, PSP_CTRL_LTRIGGER, KEY_STRAFE_L);
    check_btn(ob, nb, PSP_CTRL_RTRIGGER, KEY_STRAFE_R);
    check_btn(ob, nb, PSP_CTRL_START,    KEY_ESCAPE);
    check_btn(ob, nb, PSP_CTRL_SELECT,   KEY_TAB);

    ax = pad.Lx - 128;
    ay = pad.Ly - 128;
    oax = pad_prev.Lx - 128;
    oay = pad_prev.Ly - 128;

    if (ax < -thr && oax >= -thr)  keyq_push(1, KEY_LEFTARROW);
    if (ax >= -thr && oax < -thr)  keyq_push(0, KEY_LEFTARROW);
    if (ax > thr && oax <= thr)    keyq_push(1, KEY_RIGHTARROW);
    if (ax <= thr && oax > thr)    keyq_push(0, KEY_RIGHTARROW);
    if (ay < -thr && oay >= -thr)  keyq_push(1, KEY_UPARROW);
    if (ay >= -thr && oay < -thr)  keyq_push(0, KEY_UPARROW);
    if (ay > thr && oay <= thr)    keyq_push(1, KEY_DOWNARROW);
    if (ay <= thr && oay > thr)    keyq_push(0, KEY_DOWNARROW);

    pad_prev = pad;
}

/* ==================== DoomGeneric Interface ==================== */

void DG_Init(void)
{
    setup_callbacks();
    scePowerSetClockFrequency(333, 333, 166);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    memset(&pad_prev, 0, sizeof(pad_prev));

    gu_init();
}

void DG_DrawFrame(void)
{
    poll_input();
    draw_framebuffer();
}

void DG_SleepMs(uint32_t ms)
{
    sceKernelDelayThread(ms * 1000);
}

uint32_t DG_GetTicksMs(void)
{
    uint64_t tick;
    sceRtcGetCurrentTick(&tick);
    return (uint32_t)(tick / 1000ULL);
}

int DG_GetKey(int *pressed, unsigned char *doomkey)
{
    if (keyq_tail == keyq_head) return 0;
    *pressed = keyq[keyq_tail].pressed;
    *doomkey = keyq[keyq_tail].key;
    keyq_tail = (keyq_tail + 1) % KEYQ_SIZE;
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

/* ==================== Main ==================== */

int main(int argc, char **argv)
{
    if (argc < 2) {
        char *new_argv[] = {
            "chexquest",
            "-iwad", "chex.wad",
            NULL
        };
        doomgeneric_Create(3, new_argv);
    } else {
        doomgeneric_Create(argc, argv);
    }

    while (running) {
        doomgeneric_Tick();
    }

    sceKernelExitGame();
    return 0;
}
