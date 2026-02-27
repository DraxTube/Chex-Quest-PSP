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
PSP_HEAP_SIZE_KB(-1);

/* ==================== Constants ==================== */

#define SCR_W       480
#define SCR_H       272
#define BUF_W       512
#define FRAME_SIZE  (BUF_W * SCR_H * 4)

/* ==================== Exit Callbacks ==================== */

static volatile int running = 1;

static int exit_cb(int arg1, int arg2, void *common)
{
    (void)arg1; (void)arg2; (void)common;
    running = 0;
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
    int thid = sceKernelCreateThread("cb", cb_thread,
                                     0x11, 0x1000, PSP_THREAD_ATTR_USER, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

/* ==================== GU / Display ==================== */

static uint32_t __attribute__((aligned(16))) gu_list[262144];
static uint32_t __attribute__((aligned(16))) tex_buf[512 * 512];
static void *vram_base = NULL;

typedef struct {
    unsigned short u, v;
    short x, y, z;
} Vertex;

static void gu_init(void)
{
    vram_base = (void *)(0x40000000);

    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list);

    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME_SIZE, BUF_W);
    sceGuDepthBuffer((void *)(FRAME_SIZE * 2), BUF_W);

    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);

    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGB);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuEnable(GU_TEXTURE_2D);

    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_LIGHTING);
    sceGuDisable(GU_CULL_FACE);

    sceGuClearColor(0xFF000000);

    sceGuFinish();
    sceGuSync(0, 0);

    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
}

static void draw_framebuffer(void)
{
    int y, x;
    int src_w, src_h;

    if (!DG_ScreenBuffer)
        return;

    src_w = DOOMGENERIC_RESX;
    src_h = DOOMGENERIC_RESY;
    if (src_w > SCR_W) src_w = SCR_W;
    if (src_h > SCR_H) src_h = SCR_H;
    if (src_w > 512)   src_w = 512;
    if (src_h > 512)   src_h = 512;

    /* DoomGeneric: XRGB 0x00RRGGBB  ->  PSP GU: ABGR 0xFFBBGGRR */
    for (y = 0; y < src_h; y++)
    {
        const uint32_t *src = (const uint32_t *)&DG_ScreenBuffer[y * DOOMGENERIC_RESX];
        uint32_t *dst = &tex_buf[y * 512];
        for (x = 0; x < src_w; x++)
        {
            uint32_t p = src[x];
            uint32_t r = (p >> 16) & 0xFFu;
            uint32_t g = (p >> 8)  & 0xFFu;
            uint32_t b =  p        & 0xFFu;
            dst[x] = 0xFF000000u | (b << 16) | (g << 8) | r;
        }
    }

    sceKernelDcacheWritebackInvalidateAll();

    sceGuStart(GU_DIRECT, gu_list);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuTexImage(0, 512, 512, 512, tex_buf);

    {
        int strip_w = 64;
        int sx;
        for (sx = 0; sx < src_w; sx += strip_w)
        {
            int sw = strip_w;
            if (sx + sw > src_w)
                sw = src_w - sx;

            short dx0 = (short)((sx * SCR_W) / src_w);
            short dx1 = (short)(((sx + sw) * SCR_W) / src_w);

            Vertex *v = (Vertex *)sceGuGetMemory(2 * sizeof(Vertex));
            if (!v) continue;

            v[0].u = (unsigned short)sx;
            v[0].v = 0;
            v[0].x = dx0;
            v[0].y = 0;
            v[0].z = 0;

            v[1].u = (unsigned short)(sx + sw);
            v[1].v = (unsigned short)src_h;
            v[1].x = dx1;
            v[1].y = SCR_H;
            v[1].z = 0;

            sceGuDrawArray(GU_SPRITES,
                GU_TEXTURE_16BIT | GU_VERTEX_16BIT | GU_TRANSFORM_2D,
                2, NULL, v);
        }
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* ==================== Input ==================== */

#define KEYQ_SIZE 128

static struct {
    unsigned char pressed;
    unsigned char key;
} keyq[KEYQ_SIZE];

static int keyq_head = 0;
static int keyq_tail = 0;
static SceCtrlData pad_prev;
static int pad_initialized = 0;

static void keyq_push(int pressed, unsigned char doomkey)
{
    int next = (keyq_head + 1) % KEYQ_SIZE;
    if (next == keyq_tail)
        return;
    keyq[keyq_head].pressed = (unsigned char)pressed;
    keyq[keyq_head].key = doomkey;
    keyq_head = next;
}

static void check_btn(uint32_t old_b, uint32_t new_b,
                       uint32_t mask, unsigned char doomkey)
{
    int was = (old_b & mask) != 0;
    int now = (new_b & mask) != 0;
    if (now && !was)  keyq_push(1, doomkey);
    if (!now && was)  keyq_push(0, doomkey);
}

static int analog_state[4] = {0, 0, 0, 0};

static void poll_input(void)
{
    SceCtrlData pad;
    int ax, ay;
    int thr = 50;
    int state;

    sceCtrlPeekBufferPositive(&pad, 1);

    if (!pad_initialized)
    {
        pad_prev = pad;
        pad_initialized = 1;
        return;
    }

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

    ax = (int)pad.Lx - 128;
    ay = (int)pad.Ly - 128;

    state = (ax < -thr) ? 1 : 0;
    if (state != analog_state[0])
    {
        keyq_push(state, KEY_LEFTARROW);
        analog_state[0] = state;
    }

    state = (ax > thr) ? 1 : 0;
    if (state != analog_state[1])
    {
        keyq_push(state, KEY_RIGHTARROW);
        analog_state[1] = state;
    }

    state = (ay < -thr) ? 1 : 0;
    if (state != analog_state[2])
    {
        keyq_push(state, KEY_UPARROW);
        analog_state[2] = state;
    }

    state = (ay > thr) ? 1 : 0;
    if (state != analog_state[3])
    {
        keyq_push(state, KEY_DOWNARROW);
        analog_state[3] = state;
    }

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
    pad_initialized = 0;

    gu_init();
}

void DG_DrawFrame(void)
{
    if (!running)
    {
        sceKernelExitGame();
        return;
    }
    poll_input();
    draw_framebuffer();
}

void DG_SleepMs(uint32_t ms)
{
    sceKernelDelayThread(ms * 1000u);
}

uint32_t DG_GetTicksMs(void)
{
    uint64_t tick;
    uint32_t res;
    sceRtcGetCurrentTick(&tick);
    res = sceRtcGetTickResolution();
    return (uint32_t)(tick / (res / 1000u));
}

int DG_GetKey(int *pressed, unsigned char *doomkey)
{
    if (keyq_tail == keyq_head)
        return 0;
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
    static char *default_argv[] = {
        "chexquest",
        "-iwad", "chex.wad",
        NULL
    };
    static int default_argc = 3;

    if (argc < 2)
    {
        doomgeneric_Create(default_argc, default_argv);
    }
    else
    {
        doomgeneric_Create(argc, argv);
    }

    while (running)
    {
        doomgeneric_Tick();
    }

    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
