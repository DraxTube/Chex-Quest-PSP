/*
 * doomgeneric_psp.c - PSP platform for doomgeneric (Chex Quest)
 * Con debug log su ms0:/PSP/GAME/ChexQuest/debug.txt
 * FIX: fullscreen scaling, input mapping, analog stick
 */

#include "doomgeneric.h"
#include "doomkeys.h"

#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <psppower.h>
#include <psprtc.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ==================== Debug Log ==================== */

static FILE *dbg_file = NULL;

static void dbg_init(void)
{
    dbg_file = fopen("debug.txt", "w");
    if (!dbg_file)
        dbg_file = fopen("ms0:/PSP/GAME/ChexQuest/debug.txt", "w");
}

static void dbg_log(const char *msg)
{
    if (dbg_file)
    {
        fprintf(dbg_file, "%s\n", msg);
        fflush(dbg_file);
    }
}

static void dbg_log2(const char *msg, const char *arg)
{
    if (dbg_file)
    {
        fprintf(dbg_file, "%s: %s\n", msg, arg);
        fflush(dbg_file);
    }
}

static void dbg_logn(const char *msg, int val)
{
    if (dbg_file)
    {
        fprintf(dbg_file, "%s: %d\n", msg, val);
        fflush(dbg_file);
    }
}

static void dbg_close(void)
{
    if (dbg_file)
    {
        fclose(dbg_file);
        dbg_file = NULL;
    }
}

/* ==================== PSP Module Info ==================== */

PSP_MODULE_INFO("ChexQuest", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
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
                                     0x11, 0xFA0, PSP_THREAD_ATTR_USER, 0);
    if (thid >= 0)
        sceKernelStartThread(thid, 0, NULL);
}

/* ==================== GU / Display ==================== */

static uint32_t __attribute__((aligned(16))) gu_list[262144];
static uint32_t __attribute__((aligned(16))) tex_buf[512 * 512];

typedef struct {
    unsigned short u, v;
    short x, y, z;
} Vertex;

static void gu_init(void)
{
    dbg_log("gu_init: start");

    sceGuInit();
    sceGuStart(GU_DIRECT, gu_list);

    sceGuDrawBuffer(GU_PSM_8888, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FRAME_SIZE, BUF_W);

    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);

    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);

    sceGuTexMode(GU_PSM_8888, 0, 0, GU_FALSE);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
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

    dbg_log("gu_init: done");
}

static int frame_count = 0;

static void draw_framebuffer(void)
{
    int y, x;
    int src_w, src_h;

    if (!DG_ScreenBuffer)
        return;

    src_w = DOOMGENERIC_RESX;
    src_h = DOOMGENERIC_RESY;

    /* Clamp sorgente alla texture 512x512 */
    if (src_w > 512) src_w = 512;
    if (src_h > 512) src_h = 512;

    /* Copia pixel da DG_ScreenBuffer a tex_buf con conversione colore RGB->BGR per GU */
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

    sceKernelDcacheWritebackAll();

    sceGuStart(GU_DIRECT, gu_list);
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGuTexImage(0, 512, 512, 512, tex_buf);

    /*
     * FIX FULLSCREEN: Disegna la texture scalata a tutto schermo 480x272
     * usando strip da 64 pixel per rispettare il limite hardware PSP
     */
    {
        int strip_w = 64;
        int sx;
        for (sx = 0; sx < src_w; sx += strip_w)
        {
            int sw = strip_w;
            if (sx + sw > src_w)
                sw = src_w - sx;

            /* Mappa coordinate texture -> coordinate schermo proporzionalmente */
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
            v[1].y = (short)SCR_H;
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

    frame_count++;
    if (frame_count <= 5 || (frame_count % 60) == 0)
    {
        dbg_logn("frame", frame_count);
    }
}

/* ==================== Input ==================== */

#define KEYQ_SIZE 256

/* Fallback se doomkeys.h non definisce F6/F9 */
#ifndef KEY_F6
#define KEY_F6 (0x80+0x40)
#endif
#ifndef KEY_F9
#define KEY_F9 (0x80+0x43)
#endif

static struct {
    unsigned char pressed;
    unsigned char key;
} keyq[KEYQ_SIZE];

static int keyq_head = 0;
static int keyq_tail = 0;
static SceCtrlData pad_prev;
static int pad_initialized = 0;
static int current_weapon = 1;   /* Per ciclare armi 1-7 */

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

static int analog_left = 0, analog_right = 0, analog_up = 0, analog_down = 0;

static void poll_input(void)
{
    SceCtrlData pad;
    int ax, ay;
    int thr = 40;
    int state;

    sceCtrlPeekBufferPositive(&pad, 1);

    if (!pad_initialized)
    {
        memset(&pad_prev, 0, sizeof(pad_prev));
        pad_prev.Lx = 128;
        pad_prev.Ly = 128;
        pad_initialized = 1;
    }

    uint32_t ob = pad_prev.Buttons;
    uint32_t nb = pad.Buttons;

    /* ===== D-PAD: Armi + Quick Save/Load ===== */
    {
        int was, now;

        /* D-pad Destra: arma successiva */
        was = (ob & PSP_CTRL_RIGHT) != 0;
        now = (nb & PSP_CTRL_RIGHT) != 0;
        if (now && !was)
        {
            current_weapon++;
            if (current_weapon > 7) current_weapon = 1;
            keyq_push(1, '0' + current_weapon);
            keyq_push(0, '0' + current_weapon);
        }

        /* D-pad Sinistra: arma precedente */
        was = (ob & PSP_CTRL_LEFT) != 0;
        now = (nb & PSP_CTRL_LEFT) != 0;
        if (now && !was)
        {
            current_weapon--;
            if (current_weapon < 1) current_weapon = 7;
            keyq_push(1, '0' + current_weapon);
            keyq_push(0, '0' + current_weapon);
        }

        /* D-pad Su: Quick Save (F6) */
        was = (ob & PSP_CTRL_UP) != 0;
        now = (nb & PSP_CTRL_UP) != 0;
        if (now && !was)
        {
            keyq_push(1, KEY_F6);
            keyq_push(0, KEY_F6);
        }

        /* D-pad Giù: Quick Load (F9) */
        was = (ob & PSP_CTRL_DOWN) != 0;
        now = (nb & PSP_CTRL_DOWN) != 0;
        if (now && !was)
        {
            keyq_push(1, KEY_F9);
            keyq_push(0, KEY_F9);
        }
    }

    /* ===== BOTTONI AZIONE ===== */
    check_btn(ob, nb, PSP_CTRL_CROSS,    KEY_RCTRL);    /* Fuoco */
    check_btn(ob, nb, PSP_CTRL_CROSS,    KEY_ENTER);    /* Conferma menu */
    check_btn(ob, nb, PSP_CTRL_CROSS,    'y');           /* Sì per prompt */
    check_btn(ob, nb, PSP_CTRL_CIRCLE,   ' ');           /* Usa / Apri */
    check_btn(ob, nb, PSP_CTRL_TRIANGLE, KEY_RSHIFT);    /* Corri */
    check_btn(ob, nb, PSP_CTRL_SQUARE,   KEY_TAB);       /* Automap */

    /* ===== TRIGGER: Strafe ===== */
    check_btn(ob, nb, PSP_CTRL_LTRIGGER, ',');            /* Strafe sx */
    check_btn(ob, nb, PSP_CTRL_RTRIGGER, '.');            /* Strafe dx */

    /* ===== START / SELECT ===== */
    check_btn(ob, nb, PSP_CTRL_START,    KEY_ESCAPE);    /* Menu */
    check_btn(ob, nb, PSP_CTRL_SELECT,   KEY_ENTER);     /* Conferma alt */

    /* ===== ANALOG STICK: Movimento ===== */
    ax = (int)pad.Lx - 128;
    ay = (int)pad.Ly - 128;

    state = (ax < -thr) ? 1 : 0;
    if (state != analog_left)  { keyq_push(state, KEY_LEFTARROW);  analog_left = state; }

    state = (ax > thr) ? 1 : 0;
    if (state != analog_right) { keyq_push(state, KEY_RIGHTARROW); analog_right = state; }

    state = (ay < -thr) ? 1 : 0;
    if (state != analog_up)    { keyq_push(state, KEY_UPARROW);    analog_up = state; }

    state = (ay > thr) ? 1 : 0;
    if (state != analog_down)  { keyq_push(state, KEY_DOWNARROW);  analog_down = state; }

    pad_prev = pad;
}
/* ==================== DoomGeneric Interface ==================== */

void DG_Init(void)
{
    dbg_log("DG_Init: start");

    setup_callbacks();
    dbg_log("DG_Init: callbacks OK");

    scePowerSetClockFrequency(333, 333, 166);
    dbg_log("DG_Init: clock 333 OK");

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    memset(&pad_prev, 0, sizeof(pad_prev));
    pad_prev.Lx = 128;
    pad_prev.Ly = 128;
    pad_initialized = 0;
    dbg_log("DG_Init: ctrl OK");

    gu_init();
    dbg_log("DG_Init: done");
}

void DG_DrawFrame(void)
{
    if (!running)
    {
        dbg_log("DG_DrawFrame: exit requested");
        dbg_close();
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
    if (res == 0) res = 1000000;
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
    static char *default_argv[4];
    static int default_argc = 3;
    const char *found_path = NULL;
    int i;

    /* Init debug log FIRST */
    dbg_init();
    dbg_log("=== Chex Quest PSP starting ===");
    dbg_logn("argc", argc);
    dbg_logn("DOOMGENERIC_RESX", DOOMGENERIC_RESX);
    dbg_logn("DOOMGENERIC_RESY", DOOMGENERIC_RESY);

    const char *search_paths[] = {
        "ms0:/PSP/GAME/ChexQuest/chex.wad",
        "ms0:/PSP/GAME/chexquest/chex.wad",
        "./chex.wad",
        "chex.wad",
        NULL
    };

    dbg_log("Searching for WAD file...");
    for (i = 0; search_paths[i] != NULL; i++)
    {
        FILE *test = fopen(search_paths[i], "rb");
        if (test != NULL)
        {
            fclose(test);
            found_path = search_paths[i];
            dbg_log2("WAD FOUND", found_path);
            break;
        }
        else
        {
            dbg_log2("WAD not at", search_paths[i]);
        }
    }

    if (found_path == NULL)
    {
        dbg_log("ERROR: WAD file not found anywhere!");
        dbg_close();
        sceKernelDelayThread(3000000);
        sceKernelExitGame();
        return 1;
    }

    default_argv[0] = "chexquest";
    default_argv[1] = "-iwad";
    default_argv[2] = (char *)found_path;
    default_argv[3] = NULL;

    dbg_log("Calling doomgeneric_Create...");

    if (argc < 2)
        doomgeneric_Create(default_argc, default_argv);
    else
        doomgeneric_Create(argc, argv);

    dbg_log("doomgeneric_Create done, entering main loop");

    while (running)
        doomgeneric_Tick();

    dbg_log("Main loop ended, shutting down");
    dbg_close();
    sceGuTerm();
    sceKernelExitGame();
    return 0;
}
