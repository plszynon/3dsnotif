#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define MAX_N 16
typedef struct { char app[48]; char msg[96]; char icon[40]; int led, h, m, day; } Entry;
static Entry E[MAX_N];
static int n = 0, screenOn = 1, ledOk = 0, newsOk = 0, lcdOk = 0;
static PrintConsole top, bot;
static const char *DIR_ = "sdmc:/3ds/notifier";
static const char *PATH_ = "sdmc:/3ds/notifier/schedule.txt";

static void save(void) {
    mkdir("sdmc:/3ds", 0777); mkdir(DIR_, 0777);
    FILE *f = fopen(PATH_, "w"); if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%d|%d|%d|%s|%s|%s\n", E[i].h, E[i].m, E[i].led, E[i].app, E[i].msg, E[i].icon);
    fclose(f);
}
static void load(void) {
    FILE *f = fopen(PATH_, "r"); if (!f) return;
    char line[200];
    while (n < MAX_N && fgets(line, sizeof line, f)) {
        Entry e; memset(&e, 0, sizeof e); e.day = -1;
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        if (sscanf(line, "%d|%d|%d|%47[^|]|%95[^|]|%39[^\n]", &e.h, &e.m, &e.led, e.app, e.msg, e.icon) >= 5) E[n++] = e;
    }
    fclose(f);
}

typedef struct { u8 ticks, transition, repeats, pad; u8 r[32], g[32], b[32]; } LedPat;
static void led(int on) {
    if (!ledOk) return;
    LedPat p; memset(&p, 0, sizeof p);
    p.ticks = 0x30;
    if (on) {
        p.transition = 0x30;   /* smooth fade, like StreetPass */
        p.repeats = 0xFF;      /* repeat forever */
        for (int i = 0; i < 32; i++) p.g[i] = (i < 16) ? (i * 17) : ((31 - i) * 17);
    }
    MCUHWC_SetInfoLedPattern((const void *)&p);
}
static void toUtf16(const char *s, u16 *out, int max, int *len) {
    ssize_t k = utf8_to_utf16(out, (const uint8_t *)s, max - 1);
    if (k < 0) k = 0;
    out[k] = 0; *len = (int)k;
}
static u8 iconBuf[0x10000];
static void fire(Entry *e) {
    if (newsOk) {
        u16 t[64], m[128]; int tl, ml;
        toUtf16(e->app, t, 64, &tl); toUtf16(e->msg, m, 128, &ml);
        u32 isz = 0;
        if (e->icon[0]) {
            char path[96]; snprintf(path, sizeof path, "sdmc:/3ds/notifier/icons/%s", e->icon);
            FILE *f = fopen(path, "rb");
            if (f) { isz = fread(iconBuf, 1, sizeof iconBuf, f); fclose(f); }
        }
        Result r = -1;
        if (isz) r = NEWS_AddNotification(t, tl, m, ml, iconBuf, isz, true);
        if (R_FAILED(r)) NEWS_AddNotification(t, tl, m, ml, NULL, 0, false);
    }
    led(e->led);
}
static void ask(const char *hint, char *out, int max) {
    SwkbdState s; swkbdInit(&s, SWKBD_TYPE_NORMAL, 2, max - 1);
    swkbdSetHintText(&s, hint); swkbdSetValidation(&s, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    swkbdInputText(&s, out, max);
}
static void setScreen(int on) {
    if (!lcdOk) return;
    if (on) GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH); else GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
    screenOn = on;
}
#define C_RESET "\x1b[0m"
#define C_RED "\x1b[31;1m"
#define C_GRN "\x1b[32;1m"
#define C_YEL "\x1b[33;1m"
#define C_CYN "\x1b[36;1m"
#define C_WHT "\x1b[37;1m"
static void drawList(void) {
    consoleSelect(&top); consoleClear();
    time_t t = time(NULL); struct tm *lt = localtime(&t);
    printf(C_CYN "== 3DS NOTIFIER ==" C_RESET "      " C_WHT "%02d:%02d:%02d\n" C_RESET, lt->tm_hour, lt->tm_min, lt->tm_sec);
    printf(C_CYN "--------------------------------------------\n" C_RESET);
    if (!n) printf("\n  No notifications yet.\n  Press " C_GRN "A" C_RESET " to add one.\n");
    for (int i = 0; i < n; i++) {
        printf(C_YEL "%d) %02d:%02d " C_WHT "%s " C_RESET "[LED %s]\n", i + 1, E[i].h, E[i].m, E[i].app, E[i].led ? C_GRN "on" C_RESET : C_RED "off" C_RESET);
        printf("     %s\n", E[i].msg);
    }
    printf(C_CYN "\n--------------------------------------------\n" C_RESET);
    printf("news: %s   led: %s\n", newsOk ? C_GRN "ok" C_RESET : C_RED "FAIL" C_RESET, ledOk ? C_GRN "ok" C_RESET : C_RED "FAIL" C_RESET);
}
static void drawHelp(void) {
    consoleSelect(&bot); consoleClear();
    printf(C_CYN "   CONTROLS\n" C_RESET);
    printf(C_GRN " A" C_RESET "      add notification\n");
    printf(C_RED " X" C_RESET "      delete last\n");
    printf(C_YEL " Y" C_RESET "      screen off / on\n");
    printf(C_CYN " SELECT" C_RESET " test (green LED)\n");
    printf(C_WHT " START" C_RESET "  exit\n\n");
    printf("Keep the 3DS plugged in.\nSleep is blocked while this\napp is open.\n");
}

typedef struct { u64 id; u8 media; char name[48]; } Title;
#define MAX_T 200
static Title T[MAX_T];
static int nT = 0, titlesLoaded = 0, amOk = 0;
static u8 smdh[0x36C0];

static int readSmdh(u64 id, u8 media) {
    u32 arch[4] = { (u32)(id & 0xFFFFFFFF), (u32)(id >> 32), media, 0 };
    static const u32 fp[5] = { 0, 0, 2, 0x6F6E6369, 0 }; /* exefs:/icon */
    FS_Path ap = { PATH_BINARY, 16, arch }, fpth = { PATH_BINARY, 20, fp };
    Handle fh;
    if (R_FAILED(FSUSER_OpenFileDirectly(&fh, ARCHIVE_SAVEDATA_AND_CONTENT, ap, fpth, FS_OPEN_READ, 0))) return 0;
    u32 br = 0;
    Result r = FSFILE_Read(fh, &br, 0, smdh, sizeof smdh);
    FSFILE_Close(fh);
    return R_SUCCEEDED(r) && br >= sizeof smdh && memcmp(smdh, "SMDH", 4) == 0;
}
static void smdhName(char *out, int max) {
    for (int lang = 1; lang >= 0; lang--) {
        const u16 *src = (const u16 *)(smdh + 8 + lang * 0x200);
        if (src[0]) {
            u16 tmp[0x40]; memcpy(tmp, src, sizeof tmp); tmp[0x3F] = 0;
            ssize_t k = utf16_to_utf8((uint8_t *)out, tmp, max - 1);
            if (k < 0) k = 0;
            out[k] = 0;
            for (char *c = out; *c; c++) if (*c == '\n' || *c == '|') *c = ' ';
            return;
        }
    }
    snprintf(out, max, "(unnamed)");
}
static void loadTitles(void) {
    if (titlesLoaded) return;
    titlesLoaded = 1;
    if (!amOk) return;
    struct { u8 media; u32 high; } src[2] = { { MEDIATYPE_SD, 0x00040000 }, { MEDIATYPE_NAND, 0x00040010 } };
    for (int s2 = 0; s2 < 2; s2++) {
        u32 cnt = 0;
        if (R_FAILED(AM_GetTitleCount(src[s2].media, &cnt)) || !cnt) continue;
        u64 *ids = malloc(cnt * sizeof(u64)); if (!ids) continue;
        u32 rd = 0;
        if (R_SUCCEEDED(AM_GetTitleList(&rd, src[s2].media, cnt, ids))) {
            for (u32 i = 0; i < rd && nT < MAX_T; i++) {
                if ((u32)(ids[i] >> 32) != src[s2].high) continue;
                if (!readSmdh(ids[i], src[s2].media)) continue;
                T[nT].id = ids[i]; T[nT].media = src[s2].media;
                smdhName(T[nT].name, sizeof T[nT].name);
                nT++;
            }
        }
        free(ids);
    }
}
static u8 jpgBuf[0x10000];
static int jpgLen;
static void jpgWrite(void *ctx, void *data, int size) {
    (void)ctx;
    if (jpgLen + size <= (int)sizeof jpgBuf) { memcpy(jpgBuf + jpgLen, data, size); jpgLen += size; }
}
/* Reads the 48x48 icon of a title into a JPEG file in icons/. Returns 1 on success. */
static int saveIcon(Title *t, char *fname, int fmax) {
    if (!readSmdh(t->id, t->media)) return 0;
    const u16 *px = (const u16 *)(smdh + 0x24C0);
    static u8 rgb[48 * 48 * 3];
    for (int i = 0; i < 48 * 48; i++) {
        int tile = i / 64, k = i % 64;
        int tx = tile % 6, ty = tile / 6;
        int x = (k & 1) | ((k >> 1) & 2) | ((k >> 2) & 4);
        int y = ((k >> 1) & 1) | ((k >> 2) & 2) | ((k >> 3) & 4);
        u16 c = px[i];
        u8 *o = rgb + ((ty * 8 + y) * 48 + (tx * 8 + x)) * 3;
        o[0] = ((c >> 11) & 31) * 255 / 31;
        o[1] = ((c >> 5) & 63) * 255 / 63;
        o[2] = (c & 31) * 255 / 31;
    }
    jpgLen = 0;
    if (!stbi_write_jpg_to_func(jpgWrite, NULL, 48, 48, 3, rgb, 90) || jpgLen <= 0) return 0;
    mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/notifier", 0777); mkdir("sdmc:/3ds/notifier/icons", 0777);
    snprintf(fname, fmax, "%08lX%08lX.jpg", (unsigned long)(t->id >> 32), (unsigned long)(t->id & 0xFFFFFFFF));
    char path[96]; snprintf(path, sizeof path, "sdmc:/3ds/notifier/icons/%s", fname);
    FILE *f = fopen(path, "wb"); if (!f) return 0;
    fwrite(jpgBuf, 1, jpgLen, f); fclose(f);
    return 1;
}
/* Returns index into T, -1 for "custom name, no icon", -2 for cancel */
static int pickTitle(void) {
    consoleSelect(&top);
    loadTitles();
    int sel = 0, total = nT + 1;   /* row 0 = custom */
    while (aptMainLoop()) {
        consoleClear();
        printf("\x1b[36;1mChoose the app (icon + name)\n" C_RESET "\x1b[36;1m------------------------------\n" C_RESET);
        int first = sel - 8; if (first < 0) first = 0;
        for (int r = first; r < total && r < first + 17; r++) {
            const char *nm = r == 0 ? "[Custom name, no icon]" : T[r - 1].name;
            if (r == sel) printf("\x1b[33;1m> %.40s\n" C_RESET, nm); else printf("  %.40s\n", nm);
        }
        gspWaitForVBlank(); hidScanInput(); u32 d = hidKeysDown();
        if (d & KEY_DOWN) sel = (sel + 1) % total;
        if (d & KEY_UP) sel = (sel + total - 1) % total;
        if (d & KEY_R) sel = sel + 8 < total ? sel + 8 : total - 1;
        if (d & KEY_L) sel = sel - 8 > 0 ? sel - 8 : 0;
        if (d & KEY_B) return -2;
        if (d & KEY_A) return sel - 1;
        gfxFlushBuffers(); gfxSwapBuffers();
    }
    return -2;
}
static void addFlow(void) {
    if (n >= MAX_N) return;
    Entry e; memset(&e, 0, sizeof e); e.day = -1; e.led = 1;
    time_t t = time(NULL); struct tm *lt = localtime(&t); e.h = lt->tm_hour; e.m = (lt->tm_min + 1) % 60;
    int pk = pickTitle();
    if (pk == -2) { drawHelp(); return; }
    if (pk >= 0) {
        snprintf(e.app, sizeof e.app, "%s", T[pk].name);
        if (!saveIcon(&T[pk], e.icon, sizeof e.icon)) e.icon[0] = 0;
    } else {
        ask("App name (title)", e.app, sizeof e.app);
    }
    ask("Notification text", e.msg, sizeof e.msg);
    consoleSelect(&bot);
    while (aptMainLoop()) {
        consoleClear();
        printf("Time: %02d:%02d   LED: %s\n\nUP/DOWN hour  L/R minute\nX toggle LED\nA save   B cancel\n", e.h, e.m, e.led ? "on" : "off");
        gspWaitForVBlank(); hidScanInput(); u32 d = hidKeysDown();
        if (d & KEY_UP) e.h = (e.h + 1) % 24;
        if (d & KEY_DOWN) e.h = (e.h + 23) % 24;
        if (d & KEY_R) e.m = (e.m + 1) % 60;
        if (d & KEY_L) e.m = (e.m + 59) % 60;
        if (d & KEY_X) e.led = !e.led;
        if (d & KEY_B) break;
        if (d & KEY_A) { E[n++] = e; save(); break; }
        gfxFlushBuffers(); gfxSwapBuffers();
    }
    drawHelp();
}
int main(void) {
    gfxInitDefault();
    consoleInit(GFX_TOP, &top); consoleInit(GFX_BOTTOM, &bot);
    aptSetSleepAllowed(false);
    newsOk = R_SUCCEEDED(newsInit());
    ledOk = R_SUCCEEDED(mcuHwcInit());
    lcdOk = R_SUCCEEDED(gspLcdInit());
    amOk = R_SUCCEEDED(amInit());
    load(); drawHelp();
    int lastSec = -1;
    while (aptMainLoop()) {
        gspWaitForVBlank(); hidScanInput(); u32 d = hidKeysDown();
        if (d & KEY_START) break;
        if (!screenOn && d) { setScreen(1); d = 0; }
        if (d & KEY_Y) setScreen(0);
        if (d & KEY_A) addFlow();
        if ((d & KEY_X) && n) { n--; save(); }
        if (d & KEY_SELECT) { Entry t; memset(&t, 0, sizeof t); snprintf(t.app, sizeof t.app, "Notifier"); snprintf(t.msg, sizeof t.msg, "Test notification"); t.led = 1; t.day = -1; fire(&t); }
        time_t t = time(NULL); struct tm *lt = localtime(&t);
        if (lt->tm_sec != lastSec) {
            lastSec = lt->tm_sec;
            for (int i = 0; i < n; i++)
                if (E[i].h == lt->tm_hour && E[i].m == lt->tm_min && E[i].day != lt->tm_yday) {
                    E[i].day = lt->tm_yday; fire(&E[i]);
                    if (!screenOn) setScreen(1);
                }
            if (screenOn) drawList();
        }
        gfxFlushBuffers(); gfxSwapBuffers();
    }
    led(0); setScreen(1);
    if (amOk) amExit(); gspLcdExit(); mcuHwcExit(); newsExit(); gfxExit();
    return 0;
}
