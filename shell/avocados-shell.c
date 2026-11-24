/*
 * avocados shell: a Windows 11 / Windhawk-style desktop for ReactOS.
 *
 *  - wallpaper desktop
 *  - floating, centered, frosted dock (Start, Search, pinned apps, running windows)
 *  - frosted clock/tray pill on the right
 *  - rounded frosted Start menu with search, pinned grid, recommended list and a power menu
 *
 * Plain Win32 + GDI only. Shapes are drawn by a small software renderer (anti-aliased rounded
 * rectangles with alpha, blurred translucent backgrounds), so nothing depends on GDI+, DWM or
 * layered windows, which ReactOS lacks.
 *
 * Build: i686-w64-mingw32-gcc -Os -s -mwindows -o avocados-shell.exe avocados-shell.c \
 *        -luser32 -lgdi32 -lshell32 -ladvapi32
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef unsigned int u32;

#define MAX_APPS   48
#define MAX_TASKS  20
#define MAX_WALLS  32
#define MAX_HITS   160
#define MAX_STEPS  48
#define DOCK_H     56
#define DOCK_GAP   10
#define WORK_RESERVE 76
#define CELL       48
#define TRAY_W     176
#define START_W    600

#define PK(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))
#define RGBU(c) RGB(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

/* ======================================================================== */
/* logging                                                                  */
/* ======================================================================== */

static char g_dir[MAX_PATH];

static void Log(const char *fmt, ...)
{
    char path[MAX_PATH], msg[700];
    va_list ap;
    FILE *f;
    SYSTEMTIME st;

    wsprintfA(path, "%s\\shell.log", g_dir);
    f = fopen(path, "a");
    if (!f) return;
    GetLocalTime(&st);
    va_start(ap, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = 0;
    fprintf(f, "%02d:%02d:%02d %s\n", st.wHour, st.wMinute, st.wSecond, msg);
    fclose(f);
}

/* ======================================================================== */
/* software surface                                                         */
/* ======================================================================== */

typedef struct {
    int w, h;
    u32 *px;
    HDC dc;
    HBITMAP bmp;
    HGDIOBJ old;
} Surf;

static int SurfCreate(Surf *s, int w, int h)
{
    BITMAPINFO bi;
    HDC sdc;

    ZeroMemory(s, sizeof(*s));
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    sdc = GetDC(NULL);
    s->dc = CreateCompatibleDC(sdc);
    s->bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, (void **)&s->px, NULL, 0);
    ReleaseDC(NULL, sdc);
    if (!s->dc || !s->bmp || !s->px) {
        Log("SurfCreate %dx%d failed (error %lu)", w, h, GetLastError());
        if (s->bmp) DeleteObject(s->bmp);
        if (s->dc) DeleteDC(s->dc);
        ZeroMemory(s, sizeof(*s));
        return 0;
    }
    s->old = SelectObject(s->dc, s->bmp);
    s->w = w;
    s->h = h;
    SetBkMode(s->dc, TRANSPARENT);
    return 1;
}

static void SurfFree(Surf *s)
{
    if (s->dc) {
        SelectObject(s->dc, s->old);
        DeleteDC(s->dc);
    }
    if (s->bmp) DeleteObject(s->bmp);
    ZeroMemory(s, sizeof(*s));
}

static double Clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static void Blend(u32 *p, int r, int g, int b, int a)
{
    u32 d;
    int dr, dg, db;
    if (a <= 0) return;
    if (a > 255) a = 255;
    d = *p;
    dr = (int)((d >> 16) & 255);
    dg = (int)((d >> 8) & 255);
    db = (int)(d & 255);
    dr += ((r - dr) * a) / 255;
    dg += ((g - dg) * a) / 255;
    db += ((b - db) * a) / 255;
    *p = ((u32)dr << 16) | ((u32)dg << 8) | (u32)db;
}

static double SdfRR(double dx, double dy, double hx, double hy, double r)
{
    double qx = fabs(dx) - (hx - r), qy = fabs(dy) - (hy - r);
    double mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
    double m = qx > qy ? qx : qy;
    return sqrt(mx * mx + my * my) + (m < 0 ? m : 0) - r;
}

/* anti-aliased rounded rectangle, solid colour with alpha (0..255) */
static void FillRR(Surf *s, double x, double y, double w, double h, double r, u32 col, int alpha)
{
    int x0 = (int)floor(x) - 1, y0 = (int)floor(y) - 1;
    int x1 = (int)ceil(x + w) + 1, y1 = (int)ceil(y + h) + 1, px, py;
    double cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    int cr = (int)((col >> 16) & 255), cg = (int)((col >> 8) & 255), cb = (int)(col & 255);

    if (r > hx) r = hx;
    if (r > hy) r = hy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++) {
            double cov = 0.5 - SdfRR(px + 0.5 - cx, py + 0.5 - cy, hx, hy, r);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            Blend(&s->px[py * s->w + px], cr, cg, cb, (int)(cov * alpha));
        }
}

/* the same with a vertical gradient from top to bottom colour */
static void FillRRGrad(Surf *s, double x, double y, double w, double h, double r,
                       u32 top, u32 bot, int alpha)
{
    int x0 = (int)floor(x) - 1, y0 = (int)floor(y) - 1;
    int x1 = (int)ceil(x + w) + 1, y1 = (int)ceil(y + h) + 1, px, py;
    double cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;

    if (r > hx) r = hx;
    if (r > hy) r = hy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (py = y0; py < y1; py++) {
        double t = Clamp01((py + 0.5 - y) / h);
        int cr = (int)(((top >> 16) & 255) * (1 - t) + ((bot >> 16) & 255) * t);
        int cg = (int)(((top >> 8) & 255) * (1 - t) + ((bot >> 8) & 255) * t);
        int cb = (int)((top & 255) * (1 - t) + (bot & 255) * t);
        for (px = x0; px < x1; px++) {
            double cov = 0.5 - SdfRR(px + 0.5 - cx, py + 0.5 - cy, hx, hy, r);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            Blend(&s->px[py * s->w + px], cr, cg, cb, (int)(cov * alpha));
        }
    }
}

/* rounded rectangle outline of width bw (drawn just inside the shape) */
static void StrokeRR(Surf *s, double x, double y, double w, double h, double r,
                     double bw, u32 col, int alpha)
{
    int x0 = (int)floor(x) - 1, y0 = (int)floor(y) - 1;
    int x1 = (int)ceil(x + w) + 1, y1 = (int)ceil(y + h) + 1, px, py;
    double cx = x + w / 2, cy = y + h / 2, hx = w / 2, hy = h / 2;
    int cr = (int)((col >> 16) & 255), cg = (int)((col >> 8) & 255), cb = (int)(col & 255);

    if (r > hx) r = hx;
    if (r > hy) r = hy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++) {
            double d = SdfRR(px + 0.5 - cx, py + 0.5 - cy, hx, hy, r);
            double cov = Clamp01(0.5 - d) - Clamp01(0.5 - (d + bw));
            if (cov <= 0) continue;
            Blend(&s->px[py * s->w + px], cr, cg, cb, (int)(cov * alpha));
        }
}

static void FillEllipse(Surf *s, double cx, double cy, double a, double b, u32 col, int alpha)
{
    int x0 = (int)floor(cx - a) - 1, y0 = (int)floor(cy - b) - 1;
    int x1 = (int)ceil(cx + a) + 1, y1 = (int)ceil(cy + b) + 1, px, py;
    double m = a < b ? a : b;
    int cr = (int)((col >> 16) & 255), cg = (int)((col >> 8) & 255), cb = (int)(col & 255);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++) {
            double dx = (px + 0.5 - cx) / a, dy = (py + 0.5 - cy) / b;
            double cov = 0.5 - (sqrt(dx * dx + dy * dy) - 1) * m;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            Blend(&s->px[py * s->w + px], cr, cg, cb, (int)(cov * alpha));
        }
}

/* ring (circle outline); keepTop: 0 full ring, 1 only upper arc (wifi), 2 ring with a gap on top (power) */
static void Ring(Surf *s, double cx, double cy, double r, double bw, u32 col, int alpha, int mode)
{
    int x0 = (int)floor(cx - r - bw) - 1, y0 = (int)floor(cy - r - bw) - 1;
    int x1 = (int)ceil(cx + r + bw) + 1, y1 = (int)ceil(cy + r + bw) + 1, px, py;
    int cr = (int)((col >> 16) & 255), cg = (int)((col >> 8) & 255), cb = (int)(col & 255);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++) {
            double dx = px + 0.5 - cx, dy = py + 0.5 - cy;
            double d = fabs(sqrt(dx * dx + dy * dy) - r) - bw / 2;
            double cov = 0.5 - d;
            if (mode == 1 && !(dy < 0 && fabs(dx) < -dy * 0.95)) continue;
            if (mode == 2 && dy < 0 && fabs(dx) < r * 0.45) continue;
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            Blend(&s->px[py * s->w + px], cr, cg, cb, (int)(cov * alpha));
        }
}

/* line segment with round caps */
static void Capsule(Surf *s, double ax, double ay, double bx, double by, double w, u32 col, int alpha)
{
    double minx = (ax < bx ? ax : bx) - w, maxx = (ax > bx ? ax : bx) + w;
    double miny = (ay < by ? ay : by) - w, maxy = (ay > by ? ay : by) + w;
    int x0 = (int)floor(minx), y0 = (int)floor(miny), x1 = (int)ceil(maxx), y1 = (int)ceil(maxy), px, py;
    double vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    int cr = (int)((col >> 16) & 255), cg = (int)((col >> 8) & 255), cb = (int)(col & 255);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    for (py = y0; py < y1; py++)
        for (px = x0; px < x1; px++) {
            double dx = px + 0.5 - ax, dy = py + 0.5 - ay;
            double t = len2 > 0 ? (dx * vx + dy * vy) / len2 : 0, ex, ey, cov;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            ex = dx - vx * t;
            ey = dy - vy * t;
            cov = 0.5 - (sqrt(ex * ex + ey * ey) - w / 2);
            if (cov <= 0) continue;
            if (cov > 1) cov = 1;
            Blend(&s->px[py * s->w + px], cr, cg, cb, (int)(cov * alpha));
        }
}

/* ======================================================================== */
/* blurred translucent backgrounds ("acrylic")                              */
/* ======================================================================== */

/*
 * Blur plain (w x h), tint it, and write to out, but only inside a rounded rectangle of the
 * given radius. Outside the rounded corners out keeps the untouched plain pixels.
 */
static void FrostCompose(const u32 *plain, u32 *out, int w, int h, double radius,
                         int tr, int tg, int tb, int ta)
{
    const int F = 6;
    int sw = (w + F - 1) / F, sh = (h + F - 1) / F, x, y, bx, by, c;
    float *sm = (float *)malloc((size_t)sw * sh * 3 * sizeof(float));
    float *sb = (float *)malloc((size_t)sw * sh * 3 * sizeof(float));

    if (!sm || !sb) {
        memcpy(out, plain, (size_t)w * h * sizeof(u32));
        free(sm);
        free(sb);
        return;
    }
    /* block average */
    for (by = 0; by < sh; by++)
        for (bx = 0; bx < sw; bx++) {
            double sum[3] = {0, 0, 0};
            int n = 0, yy, xx;
            for (yy = by * F; yy < by * F + F && yy < h; yy++)
                for (xx = bx * F; xx < bx * F + F && xx < w; xx++) {
                    u32 p = plain[yy * w + xx];
                    sum[0] += (p >> 16) & 255;
                    sum[1] += (p >> 8) & 255;
                    sum[2] += p & 255;
                    n++;
                }
            if (n == 0) n = 1;
            for (c = 0; c < 3; c++) sm[(by * sw + bx) * 3 + c] = (float)(sum[c] / n);
        }
    /* 3x3 smoothing */
    for (by = 0; by < sh; by++)
        for (bx = 0; bx < sw; bx++)
            for (c = 0; c < 3; c++) {
                double acc = 0;
                int n = 0, yy, xx;
                for (yy = by - 1; yy <= by + 1; yy++)
                    for (xx = bx - 1; xx <= bx + 1; xx++) {
                        if (yy < 0 || xx < 0 || yy >= sh || xx >= sw) continue;
                        acc += sm[(yy * sw + xx) * 3 + c];
                        n++;
                    }
                sb[(by * sw + bx) * 3 + c] = (float)(acc / n);
            }
    /* bilinear upsample, tint, mask */
    for (y = 0; y < h; y++) {
        double fy = (y + 0.5) / F - 0.5;
        int y0 = (int)floor(fy), y1;
        double ty = fy - y0;
        y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y1 < 0) y1 = 0;
        if (y0 >= sh) y0 = sh - 1;
        if (y1 >= sh) y1 = sh - 1;
        for (x = 0; x < w; x++) {
            double fx = (x + 0.5) / F - 0.5;
            int x0 = (int)floor(fx), x1;
            double tx = fx - x0, v[3], cov, pr, pg, pb;
            u32 p = plain[y * w + x];
            x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x1 < 0) x1 = 0;
            if (x0 >= sw) x0 = sw - 1;
            if (x1 >= sw) x1 = sw - 1;
            for (c = 0; c < 3; c++) {
                double a = sb[(y0 * sw + x0) * 3 + c] * (1 - tx) + sb[(y0 * sw + x1) * 3 + c] * tx;
                double b = sb[(y1 * sw + x0) * 3 + c] * (1 - tx) + sb[(y1 * sw + x1) * 3 + c] * tx;
                v[c] = a * (1 - ty) + b * ty;
            }
            v[0] += (tr - v[0]) * ta / 255.0 + 6;
            v[1] += (tg - v[1]) * ta / 255.0 + 6;
            v[2] += (tb - v[2]) * ta / 255.0 + 6;
            cov = Clamp01(0.5 - SdfRR(x + 0.5 - w / 2.0, y + 0.5 - h / 2.0, w / 2.0, h / 2.0, radius));
            pr = (p >> 16) & 255;
            pg = (p >> 8) & 255;
            pb = p & 255;
            pr += (v[0] - pr) * cov;
            pg += (v[1] - pg) * cov;
            pb += (v[2] - pb) * cov;
            if (pr > 255) pr = 255;
            if (pg > 255) pg = 255;
            if (pb > 255) pb = 255;
            out[y * w + x] = ((u32)pr << 16) | ((u32)pg << 8) | (u32)pb;
        }
    }
    free(sm);
    free(sb);
}

/* ======================================================================== */
/* fonts and text                                                           */
/* ======================================================================== */

static HFONT g_fNormal, g_fSmall, g_fBold, g_fTitle, g_fGlyph, g_fClock;
static char g_face[40] = "Segoe UI";

static HFONT MakeFont(const char *face, int px, int weight)
{
    return CreateFontA(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, face);
}

static void InitFonts(void)
{
    static const char *faces[] = {"Segoe UI", "Tahoma", "Arial", "Microsoft Sans Serif"};
    HDC dc = GetDC(NULL);
    int i;
    for (i = 0; i < 4; i++) {
        HFONT f = MakeFont(faces[i], 14, FW_NORMAL);
        HGDIOBJ old = SelectObject(dc, f);
        char got[64] = "";
        GetTextFaceA(dc, sizeof(got), got);
        SelectObject(dc, old);
        DeleteObject(f);
        if (!lstrcmpiA(got, faces[i])) {
            lstrcpyA(g_face, faces[i]);
            break;
        }
        if (i == 3) lstrcpyA(g_face, "Arial");
    }
    ReleaseDC(NULL, dc);
    g_fNormal = MakeFont(g_face, 14, FW_NORMAL);
    g_fSmall = MakeFont(g_face, 12, FW_NORMAL);
    g_fBold = MakeFont(g_face, 14, FW_SEMIBOLD);
    g_fTitle = MakeFont(g_face, 16, FW_SEMIBOLD);
    g_fGlyph = MakeFont(g_face, 22, FW_BOLD);
    g_fClock = MakeFont(g_face, 13, FW_SEMIBOLD);
    Log("font face: %s", g_face);
}

static void TextR(Surf *s, HFONT f, const char *t, int x, int y, int w, int h, u32 col, UINT flags)
{
    RECT r;
    HGDIOBJ old;
    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;
    old = SelectObject(s->dc, f);
    SetTextColor(s->dc, RGBU(col));
    DrawTextA(s->dc, t, -1, &r, flags | DT_NOPREFIX | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(s->dc, old);
}

/* ======================================================================== */
/* configuration                                                            */
/* ======================================================================== */

typedef struct {
    char name[48], cmd[300], args[300], glyph[4];
    u32 badge;
    int hasBadge, installedChecked, installed;
    HICON icon;
    int iconTried;
} App;

static App g_apps[MAX_APPS];
static int g_napps;
static int g_dockApps[MAX_APPS], g_ndockApps;
static int g_rec[8], g_nrec;
static char g_recSub[8][64];

static void AddApp(const char *line)
{
    const char *eq = strchr(line, '=');
    char val[700], *fld[4] = {"", "", "", ""}, *p;
    char rawcmd[300], rawargs[300];
    int i;
    App *a;

    if (!eq || g_napps >= MAX_APPS) return;
    a = &g_apps[g_napps];
    ZeroMemory(a, sizeof(*a));
    i = (int)(eq - line);
    if (i >= (int)sizeof(a->name)) i = (int)sizeof(a->name) - 1;
    memcpy(a->name, line, (size_t)i);
    lstrcpynA(val, eq + 1, sizeof(val));

    fld[0] = val;
    for (i = 1, p = val; i < 4 && (p = strchr(p, '|')) != NULL; i++) {
        *p++ = 0;
        fld[i] = p;
    }
    lstrcpynA(rawcmd, fld[0], sizeof(rawcmd));
    lstrcpynA(rawargs, fld[1], sizeof(rawargs));
    ExpandEnvironmentStringsA(rawcmd, a->cmd, sizeof(a->cmd));
    ExpandEnvironmentStringsA(rawargs, a->args, sizeof(a->args));
    if (fld[2][0]) {
        a->badge = (u32)strtoul(fld[2], NULL, 16);
        a->hasBadge = 1;
    }
    lstrcpynA(a->glyph, fld[3][0] ? fld[3] : "", sizeof(a->glyph));
    g_napps++;
}

static int FindApp(const char *name)
{
    int i;
    for (i = 0; i < g_napps; i++)
        if (!lstrcmpiA(g_apps[i].name, name)) return i;
    return -1;
}

static void LoadConfig(void)
{
    char ini[MAX_PATH], buf[16384], *p;
    DWORD n;

    wsprintfA(ini, "%s\\config\\apps.ini", g_dir);
    n = GetPrivateProfileSectionA("Apps", buf, sizeof(buf), ini);
    if (n > 0)
        for (p = buf; *p; p += strlen(p) + 1) AddApp(p);
    if (g_napps == 0) {
        AddApp("Notepad=notepad.exe|");
        AddApp("Terminal=cmd.exe|");
    }
    n = GetPrivateProfileSectionA("Dock", buf, sizeof(buf), ini);
    if (n > 0)
        for (p = buf; *p; p += strlen(p) + 1) {
            int i = FindApp(p);
            if (i >= 0 && g_ndockApps < MAX_APPS) g_dockApps[g_ndockApps++] = i;
        }
    n = GetPrivateProfileSectionA("Recommended", buf, sizeof(buf), ini);
    if (n > 0)
        for (p = buf; *p; p += strlen(p) + 1) {
            char *eq = strchr(p, '=');
            int i;
            if (eq) *eq = 0;
            i = FindApp(p);
            if (i >= 0 && g_nrec < 8) {
                lstrcpynA(g_recSub[g_nrec], eq ? eq + 1 : "", sizeof(g_recSub[0]));
                g_rec[g_nrec++] = i;
            }
        }
    Log("config: %d apps, %d in dock, %d recommended", g_napps, g_ndockApps, g_nrec);
}

static int AppInstalled(App *a)
{
    if (a->installedChecked) return a->installed;
    a->installedChecked = 1;
    if (a->cmd[0] == '@' || (!strchr(a->cmd, '\\') && !strchr(a->cmd, ':')))
        a->installed = 1;
    else
        a->installed = GetFileAttributesA(a->cmd) != INVALID_FILE_ATTRIBUTES;
    return a->installed;
}

/* ======================================================================== */
/* icons                                                                    */
/* ======================================================================== */

static void DrawAvocado(Surf *s, double cx, double cy, double size, int alpha)
{
    double r = size / 2;
    /* skin: big lower circle plus a smaller top circle */
    FillEllipse(s, cx, cy + r * 0.28, r * 0.80, r * 0.80, PK(60, 104, 34), alpha);
    FillEllipse(s, cx, cy - r * 0.38, r * 0.54, r * 0.60, PK(60, 104, 34), alpha);
    /* flesh */
    FillEllipse(s, cx, cy + r * 0.30, r * 0.64, r * 0.64, PK(196, 226, 104), alpha);
    FillEllipse(s, cx, cy - r * 0.34, r * 0.40, r * 0.46, PK(196, 226, 104), alpha);
    /* pit */
    FillEllipse(s, cx, cy + r * 0.34, r * 0.30, r * 0.30, PK(128, 82, 44), alpha);
    FillEllipse(s, cx - r * 0.08, cy + r * 0.27, r * 0.09, r * 0.09, PK(176, 124, 80), alpha);
}

static void DrawBadge(Surf *s, double x, double y, double size, u32 col, const char *glyph)
{
    u32 top = PK(((col >> 16) & 255) + (255 - ((col >> 16) & 255)) / 4,
                 ((col >> 8) & 255) + (255 - ((col >> 8) & 255)) / 4,
                 (col & 255) + (255 - (col & 255)) / 4);
    FillRRGrad(s, x, y, size, size, size * 0.24, top, col, 255);
    StrokeRR(s, x, y, size, size, size * 0.24, 1, PK(255, 255, 255), 40);
    if (glyph[0]) TextR(s, g_fGlyph, glyph, (int)x, (int)y, (int)size, (int)size, PK(255, 255, 255),
                        DT_CENTER | DT_VCENTER);
}

static void LoadAppIcon(App *a)
{
    HICON large = NULL, small = NULL;
    if (a->iconTried) return;
    a->iconTried = 1;
    if (a->cmd[0] == '@') return;
    if (ExtractIconExA(a->cmd, 0, &large, &small, 1) > 0) {
        if (large) a->icon = large;
        else a->icon = small;
        if (large && small) DestroyIcon(small);
    }
}

static void DrawAppIcon(Surf *s, App *a, int x, int y, int size)
{
    if (a->hasBadge) {
        DrawBadge(s, x, y, size, a->badge, a->glyph);
        return;
    }
    LoadAppIcon(a);
    if (a->icon) {
        GdiFlush();
        DrawIconEx(s->dc, x, y, a->icon, size, size, 0, NULL, DI_NORMAL);
        GdiFlush();
        return;
    }
    {
        char g[2];
        g[0] = a->name[0];
        g[1] = 0;
        DrawBadge(s, x, y, size, PK(96, 120, 80), g);
    }
}

/* ======================================================================== */
/* window panels (dock, tray, start)                                        */
/* ======================================================================== */

enum {
    HK_START = 1, HK_SEARCH, HK_APP, HK_WIN, HK_REC, HK_RESULT, HK_BOX, HK_POWER,
    HK_RESTART, HK_SHUTDOWN, HK_WALL
};

typedef struct {
    int x, y, w, h, kind, arg;
} Hit;

typedef struct {
    HWND hwnd;
    Surf s;
    u32 *bg;          /* composed translucent background, same size as the surface */
    int x, y, w, h;
    Hit hits[MAX_HITS];
    int nh, hot;
    int tracking;
} Panel;

static Panel g_dock, g_tray, g_start;
static Surf g_desk;
static HWND g_deskWnd;
static int g_sw, g_sh;
static char g_wallName[64];

static void HitAdd(Panel *p, int x, int y, int w, int h, int kind, int arg)
{
    if (p->nh >= MAX_HITS) return;
    p->hits[p->nh].x = x;
    p->hits[p->nh].y = y;
    p->hits[p->nh].w = w;
    p->hits[p->nh].h = h;
    p->hits[p->nh].kind = kind;
    p->hits[p->nh].arg = arg;
    p->nh++;
}

static int HitAt(Panel *p, int x, int y)
{
    int i;
    for (i = p->nh - 1; i >= 0; i--)
        if (x >= p->hits[i].x && y >= p->hits[i].y && x < p->hits[i].x + p->hits[i].w &&
            y < p->hits[i].y + p->hits[i].h)
            return i;
    return -1;
}

static int IsHot(Panel *p, int kind, int arg)
{
    return p->hot >= 0 && p->hot < p->nh && p->hits[p->hot].kind == kind && p->hits[p->hot].arg == arg;
}

static void Present(Panel *p)
{
    if (p->hwnd) InvalidateRect(p->hwnd, NULL, FALSE);
}

/* ---- tasks (running windows) ---- */

typedef struct {
    HWND h;
    char title[80];
} Task;

static Task g_tasks[MAX_TASKS];
static int g_ntasks;

static BOOL CALLBACK EnumTasks(HWND h, LPARAM lp)
{
    char t[80], cls[64];
    (void)lp;
    if (h == g_deskWnd || h == g_dock.hwnd || h == g_tray.hwnd || h == g_start.hwnd) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindow(h, GW_OWNER)) return TRUE;
    if (GetWindowLongA(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (!GetWindowTextA(h, t, sizeof(t))) return TRUE;
    GetClassNameA(h, cls, sizeof(cls));
    if (!lstrcmpiA(cls, "Progman") || !lstrcmpiA(cls, "Shell_TrayWnd")) return TRUE;
    if (g_ntasks < MAX_TASKS) {
        g_tasks[g_ntasks].h = h;
        lstrcpynA(g_tasks[g_ntasks].title, t, sizeof(g_tasks[g_ntasks].title));
        g_ntasks++;
    }
    return TRUE;
}

static int RefreshTasks(void)
{
    HWND prev[MAX_TASKS];
    int pn = g_ntasks, i, changed = 0;
    for (i = 0; i < pn; i++) prev[i] = g_tasks[i].h;
    g_ntasks = 0;
    EnumWindows(EnumTasks, 0);
    if (pn != g_ntasks) changed = 1;
    else
        for (i = 0; i < pn; i++)
            if (prev[i] != g_tasks[i].h) changed = 1;
    return changed;
}

static HICON WindowIcon(HWND h)
{
    DWORD_PTR r = 0;
    HICON ic = NULL;
    if (SendMessageTimeoutA(h, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &r) && r) ic = (HICON)r;
    if (!ic) ic = (HICON)GetClassLongA(h, GCL_HICON);
    if (!ic && SendMessageTimeoutA(h, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, &r) && r) ic = (HICON)r;
    if (!ic) ic = (HICON)GetClassLongA(h, GCL_HICONSM);
    return ic;
}

/* ---- wallpaper ---- */

static int ListWalls(char names[][64])
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;

    wsprintfA(pat, "%s\\wallpapers\\*.bmp", g_dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (n < MAX_WALLS) {
            lstrcpynA(names[n], fd.cFileName, 64);
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

/* read a 24 or 32 bit uncompressed BMP into 0x00RRGGBB pixels */
static u32 *ReadBmp(const char *path, int *ow, int *oh)
{
    FILE *f = fopen(path, "rb");
    unsigned char hdr[54];
    int w, h, bpp, stride, x, y, topdown = 0;
    unsigned char *row;
    u32 *px;
    long off;

    if (!f) return NULL;
    if (fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') {
        fclose(f);
        return NULL;
    }
    off = *(long *)(hdr + 10);
    w = *(int *)(hdr + 18);
    h = *(int *)(hdr + 22);
    bpp = *(short *)(hdr + 28);
    if (*(int *)(hdr + 30) != 0 || (bpp != 24 && bpp != 32) || w < 1 || h == 0) {
        fclose(f);
        return NULL;
    }
    if (h < 0) {
        h = -h;
        topdown = 1;
    }
    stride = ((w * bpp / 8) + 3) & ~3;
    row = (unsigned char *)malloc((size_t)stride);
    px = (u32 *)malloc((size_t)w * h * sizeof(u32));
    if (!row || !px) {
        free(row);
        free(px);
        fclose(f);
        return NULL;
    }
    fseek(f, off, SEEK_SET);
    for (y = 0; y < h; y++) {
        int dy = topdown ? y : h - 1 - y;
        if (fread(row, 1, (size_t)stride, f) != (size_t)stride) break;
        for (x = 0; x < w; x++) {
            unsigned char *q = row + x * (bpp / 8);
            px[dy * w + x] = ((u32)q[2] << 16) | ((u32)q[1] << 8) | q[0];
        }
    }
    free(row);
    fclose(f);
    *ow = w;
    *oh = h;
    return px;
}

/* scale src to cover the whole screen (bilinear, centred crop) */
static void CoverScale(const u32 *src, int sw, int sh, u32 *dst, int dw, int dh)
{
    double sx = (double)dw / sw, sy = (double)dh / sh, k = sx > sy ? sx : sy;
    double ox = (sw - dw / k) / 2, oy = (sh - dh / k) / 2;
    int x, y, c;

    for (y = 0; y < dh; y++) {
        double fy = oy + (y + 0.5) / k - 0.5;
        int y0 = (int)floor(fy), y1;
        double ty = fy - y0;
        y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y1 < 0) y1 = 0;
        if (y0 >= sh) y0 = sh - 1;
        if (y1 >= sh) y1 = sh - 1;
        for (x = 0; x < dw; x++) {
            double fx = ox + (x + 0.5) / k - 0.5;
            int x0 = (int)floor(fx), x1, out[3];
            double tx = fx - x0;
            u32 p00, p10, p01, p11;
            x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x1 < 0) x1 = 0;
            if (x0 >= sw) x0 = sw - 1;
            if (x1 >= sw) x1 = sw - 1;
            p00 = src[y0 * sw + x0];
            p10 = src[y0 * sw + x1];
            p01 = src[y1 * sw + x0];
            p11 = src[y1 * sw + x1];
            for (c = 0; c < 3; c++) {
                int sh2 = 16 - c * 8;
                double a = (double)((p00 >> sh2) & 255) * (1 - tx) + (double)((p10 >> sh2) & 255) * tx;
                double b = (double)((p01 >> sh2) & 255) * (1 - tx) + (double)((p11 >> sh2) & 255) * tx;
                out[c] = (int)(a * (1 - ty) + b * ty);
            }
            dst[y * dw + x] = ((u32)out[0] << 16) | ((u32)out[1] << 8) | (u32)out[2];
        }
    }
}

static void FallbackWallpaper(void)
{
    int x, y;
    for (y = 0; y < g_desk.h; y++)
        for (x = 0; x < g_desk.w; x++) {
            double t = (double)y / g_desk.h, u = (double)x / g_desk.w;
            int r = (int)(16 + 40 * u * (1 - t)), g = (int)(40 + 90 * (1 - t) * (0.4 + u * 0.6)), b = (int)(30 + 40 * t);
            g_desk.px[y * g_desk.w + x] = PK(r, g, b);
        }
}

static void LoadWallpaper(const char *file)
{
    char path[MAX_PATH];
    int w = 0, h = 0;
    u32 *src;

    wsprintfA(path, "%s\\wallpapers\\%s", g_dir, file);
    src = ReadBmp(path, &w, &h);
    if (src) {
        CoverScale(src, w, h, g_desk.px, g_desk.w, g_desk.h);
        free(src);
        lstrcpynA(g_wallName, file, sizeof(g_wallName));
        Log("wallpaper %s (%dx%d) ok", file, w, h);
    } else {
        FallbackWallpaper();
        Log("wallpaper %s failed to load, using gradient", file);
    }
}

static void SettingsPath(char *out) { wsprintfA(out, "%s\\config\\settings.ini", g_dir); }

static void InitWallpaper(void)
{
    char names[MAX_WALLS][64], ini[MAX_PATH], cur[64];
    int n = ListWalls(names);
    SettingsPath(ini);
    if (n == 0) {
        FallbackWallpaper();
        Log("no wallpapers found");
        return;
    }
    GetPrivateProfileStringA("Shell", "Wallpaper", names[0], cur, sizeof(cur), ini);
    LoadWallpaper(cur);
}

/* ---- backgrounds for the dock, tray and start ---- */

static void CropDesk(u32 *dst, int x, int y, int w, int h)
{
    int row;
    for (row = 0; row < h; row++) {
        int sy = y + row;
        if (sy < 0) sy = 0;
        if (sy >= g_desk.h) sy = g_desk.h - 1;
        if (x >= 0 && x + w <= g_desk.w)
            memcpy(dst + row * w, g_desk.px + sy * g_desk.w + x, (size_t)w * sizeof(u32));
        else {
            int col;
            for (col = 0; col < w; col++) {
                int sx = x + col;
                if (sx < 0) sx = 0;
                if (sx >= g_desk.w) sx = g_desk.w - 1;
                dst[row * w + col] = g_desk.px[sy * g_desk.w + sx];
            }
        }
    }
}

static void ComposeStaticBg(Panel *p, int ta)
{
    u32 *plain = (u32 *)malloc((size_t)p->w * p->h * sizeof(u32));
    if (!plain) return;
    free(p->bg);
    p->bg = (u32 *)malloc((size_t)p->w * p->h * sizeof(u32));
    if (!p->bg) {
        free(plain);
        return;
    }
    CropDesk(plain, p->x, p->y, p->w, p->h);
    FrostCompose(plain, p->bg, p->w, p->h, p->h / 2.0, 22, 30, 26, ta);
    free(plain);
}

static void ResizePanel(Panel *p, int x, int y, int w, int h)
{
    if (p->s.px && (p->s.w != w || p->s.h != h)) SurfFree(&p->s);
    if (!p->s.px) SurfCreate(&p->s, w, h);
    p->x = x;
    p->y = y;
    p->w = w;
    p->h = h;
    if (p->hwnd) SetWindowPos(p->hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

/* ======================================================================== */
/* state                                                                    */
/* ======================================================================== */

static int g_startOpen;
static DWORD g_startClosedAt;
static char g_q[64];
static int g_qlen, g_sel, g_powerOpen;
static int g_rowsP = 3, g_rowsR = 3;

static void ShowStart(int open);
static void LaunchApp(int idx);
static void RunText(const char *text);
static void NextWallpaper(void);

/* ======================================================================== */
/* dock                                                                     */
/* ======================================================================== */

typedef struct {
    int kind; /* 0 start, 1 search, 2 divider, 3 app, 4 task */
    int arg;
} DockItem;

static DockItem g_items[MAX_APPS + MAX_TASKS + 8];
static int g_nitems;

static int DockWidth(void)
{
    int i, w = 20;
    for (i = 0; i < g_nitems; i++) w += g_items[i].kind == 2 ? 14 : CELL;
    return w;
}

static void BuildDockItems(void)
{
    int i;
    g_nitems = 0;
    g_items[g_nitems].kind = 0;
    g_items[g_nitems++].arg = 0;
    g_items[g_nitems].kind = 1;
    g_items[g_nitems++].arg = 0;
    for (i = 0; i < g_ndockApps; i++) {
        g_items[g_nitems].kind = 3;
        g_items[g_nitems++].arg = g_dockApps[i];
    }
    if (g_ntasks > 0) {
        g_items[g_nitems].kind = 2;
        g_items[g_nitems++].arg = 0;
        for (i = 0; i < g_ntasks; i++) {
            g_items[g_nitems].kind = 4;
            g_items[g_nitems++].arg = i;
        }
    }
}

static void LayoutDock(void)
{
    int w, x, y;
    BuildDockItems();
    w = DockWidth();
    if (w > g_sw - 2 * TRAY_W - 60) w = g_sw - 2 * TRAY_W - 60;
    x = (g_sw - w) / 2;
    y = g_sh - DOCK_GAP - DOCK_H;
    ResizePanel(&g_dock, x, y, w, DOCK_H);
    ComposeStaticBg(&g_dock, 150);
}

static void PaintDock(void)
{
    Surf *s = &g_dock.s;
    int i, x = 10, y = 4, fg = 0;
    HWND fgw = GetForegroundWindow();

    if (!s->px) return;
    if (g_dock.bg) memcpy(s->px, g_dock.bg, (size_t)s->w * s->h * sizeof(u32));
    StrokeRR(s, 0, 0, s->w, s->h, s->h / 2.0, 1, PK(255, 255, 255), 46);
    g_dock.nh = 0;
    (void)fg;

    for (i = 0; i < g_nitems; i++) {
        DockItem *it = &g_items[i];
        int hot;
        if (it->kind == 2) {
            FillRR(s, x + 6, 16, 1.5, s->h - 32, 0.75, PK(255, 255, 255), 50);
            x += 14;
            continue;
        }
        hot = 0;
        if (it->kind == 0) {
            hot = IsHot(&g_dock, HK_START, 0) || g_startOpen;
            HitAdd(&g_dock, x, 0, CELL, s->h, HK_START, 0);
        } else if (it->kind == 1) {
            hot = IsHot(&g_dock, HK_SEARCH, 0);
            HitAdd(&g_dock, x, 0, CELL, s->h, HK_SEARCH, 0);
        } else if (it->kind == 3) {
            hot = IsHot(&g_dock, HK_APP, it->arg);
            HitAdd(&g_dock, x, 0, CELL, s->h, HK_APP, it->arg);
        } else {
            hot = IsHot(&g_dock, HK_WIN, it->arg);
            HitAdd(&g_dock, x, 0, CELL, s->h, HK_WIN, it->arg);
        }
        if (hot) FillRR(s, x + 2, y, CELL - 4, s->h - 2 * y, 10, PK(255, 255, 255), 30);

        if (it->kind == 0) {
            DrawAvocado(s, x + CELL / 2.0, s->h / 2.0 - 1, 30, 255);
        } else if (it->kind == 1) {
            double cx = x + CELL / 2.0 - 2, cy = s->h / 2.0 - 2;
            Ring(s, cx, cy, 7, 2.2, PK(240, 244, 238), 255, 0);
            Capsule(s, cx + 5, cy + 5, cx + 11, cy + 11, 2.6, PK(240, 244, 238), 255);
        } else if (it->kind == 3) {
            DrawAppIcon(s, &g_apps[it->arg], x + (CELL - 30) / 2, (s->h - 30) / 2 - 2, 30);
        } else {
            HICON ic = WindowIcon(g_tasks[it->arg].h);
            int active = g_tasks[it->arg].h == fgw;
            if (ic) {
                GdiFlush();
                DrawIconEx(s->dc, x + (CELL - 28) / 2, (s->h - 28) / 2 - 2, ic, 28, 28, 0, NULL, DI_NORMAL);
                GdiFlush();
            } else {
                char g[2];
                g[0] = g_tasks[it->arg].title[0];
                g[1] = 0;
                DrawBadge(s, x + (CELL - 28) / 2, (s->h - 28) / 2 - 2, 28, PK(88, 112, 78), g);
            }
            if (active)
                FillRR(s, x + CELL / 2.0 - 9, s->h - 8, 18, 3.5, 1.75, PK(126, 206, 60), 255);
            else
                FillRR(s, x + CELL / 2.0 - 3, s->h - 8, 6, 3.5, 1.75, PK(210, 220, 206), 160);
        }
        x += CELL;
    }
}

/* ======================================================================== */
/* tray                                                                     */
/* ======================================================================== */

static void LayoutTray(void)
{
    ResizePanel(&g_tray, g_sw - 14 - TRAY_W, g_sh - DOCK_GAP - DOCK_H, TRAY_W, DOCK_H);
    ComposeStaticBg(&g_tray, 150);
}

static void PaintTray(void)
{
    Surf *s = &g_tray.s;
    SYSTEMTIME st;
    char tm[16], dt[24];
    double cx;

    if (!s->px) return;
    if (g_tray.bg) memcpy(s->px, g_tray.bg, (size_t)s->w * s->h * sizeof(u32));
    StrokeRR(s, 0, 0, s->w, s->h, s->h / 2.0, 1, PK(255, 255, 255), 46);
    g_tray.nh = 0;

    /* wifi */
    cx = 30;
    Ring(s, cx, 36, 14, 2.2, PK(236, 240, 234), 255, 1);
    Ring(s, cx, 36, 9, 2.2, PK(236, 240, 234), 255, 1);
    FillEllipse(s, cx, 34.5, 2.6, 2.6, PK(236, 240, 234), 255);
    /* speaker */
    FillRR(s, 58, 22, 6, 12, 1.5, PK(236, 240, 234), 255);
    Capsule(s, 61, 21, 70, 15, 5, PK(236, 240, 234), 255);
    Capsule(s, 61, 35, 70, 41, 5, PK(236, 240, 234), 255);
    Capsule(s, 70, 15, 70, 41, 6, PK(236, 240, 234), 255);
    Ring(s, 70, 28, 9, 2, PK(236, 240, 234), 200, 0);

    GetLocalTime(&st);
    wsprintfA(tm, "%02d:%02d", st.wHour, st.wMinute);
    wsprintfA(dt, "%02d/%02d/%04d", st.wDay, st.wMonth, st.wYear);
    TextR(s, g_fClock, tm, 92, 8, TRAY_W - 92 - 16, 20, PK(246, 248, 244), DT_RIGHT | DT_VCENTER);
    TextR(s, g_fSmall, dt, 92, 28, TRAY_W - 92 - 16, 18, PK(206, 214, 204), DT_RIGHT | DT_VCENTER);
}

/* ======================================================================== */
/* start menu                                                               */
/* ======================================================================== */

static int Contains(const char *hay, const char *needle)
{
    char a[96], b[96];
    int i;
    lstrcpynA(a, hay, sizeof(a));
    lstrcpynA(b, needle, sizeof(b));
    for (i = 0; a[i]; i++)
        if (a[i] >= 'A' && a[i] <= 'Z') a[i] += 32;
    for (i = 0; b[i]; i++)
        if (b[i] >= 'A' && b[i] <= 'Z') b[i] += 32;
    return strstr(a, b) != NULL;
}

static int Results(int *out, int max)
{
    int i, n = 0;
    for (i = 0; i < g_napps && n < max; i++)
        if (g_apps[i].cmd[0] != '@' && Contains(g_apps[i].name, g_q)) out[n++] = i;
    return n;
}

static void StartMetrics(int *w, int *h)
{
    int avail = g_sh - WORK_RESERVE - 24;
    int rp = 3, rr = 3;
    for (;;) {
        int hh = 22 + 40 + 20 + 22 + rp * 92 + 14 + 22 + 8 + rr * 52 + 12 + 64;
        if (hh <= avail || (rp == 1 && rr == 1)) {
            *h = hh;
            break;
        }
        if (rr > 1 && (rr >= rp)) rr--;
        else if (rp > 1) rp--;
        else rr--;
    }
    g_rowsP = rp;
    g_rowsR = rr;
    *w = START_W;
    if (*w > g_sw - 20) *w = g_sw - 20;
}

static void PaintStart(void)
{
    Surf *s = &g_start.s;
    int w = s->w, h = s->h, i, x, y;
    int cols = 6, cellw = (w - 48) / cols;
    char buf[96];

    if (!s->px) return;
    if (g_start.bg) memcpy(s->px, g_start.bg, (size_t)w * h * sizeof(u32));
    g_start.nh = 0;
    StrokeRR(s, 0, 0, w, h, 20, 1, PK(255, 255, 255), 50);

    /* search box */
    FillRR(s, 24, 22, w - 48, 40, 20, PK(255, 255, 255), IsHot(&g_start, HK_BOX, 0) ? 40 : 28);
    StrokeRR(s, 24, 22, w - 48, 40, 20, 1, PK(255, 255, 255), 50);
    HitAdd(&g_start, 24, 22, w - 48, 40, HK_BOX, 0);
    Ring(s, 46, 41, 6, 2, PK(230, 236, 228), 230, 0);
    Capsule(s, 50.5, 45.5, 55, 50, 2.2, PK(230, 236, 228), 230);
    if (g_qlen > 0) {
        lstrcpynA(buf, g_q, sizeof(buf));
        lstrcatA(buf, "|");
        TextR(s, g_fNormal, buf, 66, 22, w - 48 - 54, 40, PK(250, 252, 248), DT_LEFT | DT_VCENTER);
    } else {
        TextR(s, g_fNormal, "Search apps, or type a command", 66, 22, w - 48 - 54, 40,
              PK(198, 208, 196), DT_LEFT | DT_VCENTER);
    }

    if (g_qlen > 0) {
        int res[16], n = Results(res, 14), ry = 82;
        TextR(s, g_fBold, n ? "Best match" : "No apps found", 28, ry, 200, 20, PK(246, 248, 244), DT_LEFT);
        ry += 28;
        for (i = 0; i < n; i++) {
            int sel = (i == g_sel);
            if (sel || IsHot(&g_start, HK_RESULT, i))
                FillRR(s, 20, ry, w - 40, 46, 9, PK(255, 255, 255), sel ? 38 : 26);
            HitAdd(&g_start, 20, ry, w - 40, 46, HK_RESULT, i);
            DrawAppIcon(s, &g_apps[res[i]], 32, ry + 8, 30);
            TextR(s, g_fNormal, g_apps[res[i]].name, 76, ry, w - 100, 46, PK(248, 250, 246), DT_LEFT | DT_VCENTER);
            ry += 48;
            if (ry > h - 64 - 60) break;
        }
        wsprintfA(buf, "Run \"%s\"", g_q);
        i = g_sel >= 0 && g_sel == n;
        if (i || IsHot(&g_start, HK_RESULT, 100))
            FillRR(s, 20, h - 64 - 56, w - 40, 44, 9, PK(255, 255, 255), i ? 38 : 26);
        HitAdd(&g_start, 20, h - 64 - 56, w - 40, 44, HK_RESULT, 100);
        TextR(s, g_fNormal, buf, 32, h - 64 - 56, w - 64, 44, PK(226, 234, 224), DT_LEFT | DT_VCENTER);
    } else {
        /* pinned */
        TextR(s, g_fBold, "Pinned", 28, 82, 200, 22, PK(246, 248, 244), DT_LEFT | DT_VCENTER);
        y = 112;
        for (i = 0; i < g_napps && i < cols * g_rowsP; i++) {
            int cx = 24 + (i % cols) * cellw, cy = y + (i / cols) * 92;
            if (g_apps[i].cmd[0] == '@') {
                /* special actions keep their place in the grid */
            }
            if (IsHot(&g_start, HK_APP, i))
                FillRR(s, cx + 2, cy, cellw - 4, 88, 9, PK(255, 255, 255), 30);
            HitAdd(&g_start, cx, cy, cellw, 88, HK_APP, i);
            DrawAppIcon(s, &g_apps[i], cx + (cellw - 40) / 2, cy + 10, 40);
            TextR(s, g_fSmall, g_apps[i].name, cx + 2, cy + 58, cellw - 4, 24, PK(240, 244, 238),
                  DT_CENTER | DT_VCENTER);
        }
        /* recommended */
        y = 112 + g_rowsP * 92 + 14;
        TextR(s, g_fBold, "Recommended", 28, y, 220, 22, PK(246, 248, 244), DT_LEFT | DT_VCENTER);
        y += 30;
        for (i = 0; i < g_nrec && i < g_rowsR * 2; i++) {
            int cx = 24 + (i % 2) * ((w - 48) / 2), cy = y + (i / 2) * 52;
            int cw = (w - 48) / 2;
            if (IsHot(&g_start, HK_REC, i)) FillRR(s, cx, cy, cw - 6, 48, 9, PK(255, 255, 255), 30);
            HitAdd(&g_start, cx, cy, cw - 6, 48, HK_REC, i);
            DrawAppIcon(s, &g_apps[g_rec[i]], cx + 8, cy + 9, 30);
            TextR(s, g_fNormal, g_apps[g_rec[i]].name, cx + 50, cy + 5, cw - 62, 20, PK(248, 250, 246),
                  DT_LEFT | DT_VCENTER);
            TextR(s, g_fSmall, g_recSub[i], cx + 50, cy + 25, cw - 62, 18, PK(190, 202, 188),
                  DT_LEFT | DT_VCENTER);
        }
    }

    /* footer */
    FillRR(s, 0, h - 64, w, 64, 0, PK(0, 0, 0), 60);
    x = 28;
    FillEllipse(s, x + 16, h - 32, 16, 16, PK(126, 196, 56), 255);
    TextR(s, g_fGlyph, "a", x, h - 48, 32, 32, PK(255, 255, 255), DT_CENTER | DT_VCENTER);
    TextR(s, g_fNormal, "avocados", x + 44, h - 48, 200, 32, PK(246, 248, 244), DT_LEFT | DT_VCENTER);
    /* wallpaper shortcut */
    if (IsHot(&g_start, HK_WALL, 0)) FillRR(s, w - 24 - 40 - 8 - 40, h - 52, 40, 40, 9, PK(255, 255, 255), 30);
    HitAdd(&g_start, w - 24 - 40 - 8 - 40, h - 52, 40, 40, HK_WALL, 0);
    FillRR(s, w - 24 - 40 - 8 - 40 + 10, h - 52 + 12, 20, 16, 3, PK(236, 240, 234), 230);
    FillEllipse(s, w - 24 - 40 - 8 - 40 + 15, h - 52 + 17, 2.5, 2.5, PK(60, 90, 50), 255);
    /* power */
    if (IsHot(&g_start, HK_POWER, 0) || g_powerOpen)
        FillRR(s, w - 24 - 40, h - 52, 40, 40, 9, PK(255, 255, 255), 30);
    HitAdd(&g_start, w - 24 - 40, h - 52, 40, 40, HK_POWER, 0);
    Ring(s, w - 24 - 20, h - 32 + 1, 8, 2.2, PK(240, 244, 238), 255, 2);
    Capsule(s, w - 24 - 20, h - 32 - 9, w - 24 - 20, h - 32 - 1, 2.4, PK(240, 244, 238), 255);

    if (g_powerOpen) {
        int px = w - 24 - 160, py = h - 64 - 100;
        FillRR(s, px, py, 160, 92, 12, PK(30, 38, 32), 245);
        StrokeRR(s, px, py, 160, 92, 12, 1, PK(255, 255, 255), 60);
        if (IsHot(&g_start, HK_RESTART, 0)) FillRR(s, px + 6, py + 6, 148, 38, 8, PK(255, 255, 255), 34);
        HitAdd(&g_start, px + 6, py + 6, 148, 38, HK_RESTART, 0);
        TextR(s, g_fNormal, "Restart", px + 20, py + 6, 120, 38, PK(246, 248, 244), DT_LEFT | DT_VCENTER);
        if (IsHot(&g_start, HK_SHUTDOWN, 0)) FillRR(s, px + 6, py + 48, 148, 38, 8, PK(255, 255, 255), 34);
        HitAdd(&g_start, px + 6, py + 48, 148, 38, HK_SHUTDOWN, 0);
        TextR(s, g_fNormal, "Shut down", px + 20, py + 48, 120, 38, PK(246, 248, 244), DT_LEFT | DT_VCENTER);
    }
}

static void ShowStart(int open)
{
    int w, h, x, y;
    HDC sdc;
    Surf cap;

    if (open == g_startOpen) return;
    if (!open) {
        g_startOpen = 0;
        g_startClosedAt = GetTickCount();
        ShowWindow(g_start.hwnd, SW_HIDE);
        PaintDock();
        Present(&g_dock);
        return;
    }
    g_qlen = 0;
    g_q[0] = 0;
    g_sel = 0;
    g_powerOpen = 0;
    StartMetrics(&w, &h);
    x = (g_sw - w) / 2;
    y = g_sh - DOCK_GAP - DOCK_H - 12 - h;
    if (y < 8) y = 8;
    if (g_start.s.px && (g_start.s.w != w || g_start.s.h != h)) SurfFree(&g_start.s);
    if (!g_start.s.px) SurfCreate(&g_start.s, w, h);
    g_start.x = x;
    g_start.y = y;
    g_start.w = w;
    g_start.h = h;
    SetWindowPos(g_start.hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);

    /* blur whatever is really behind the menu */
    if (SurfCreate(&cap, w, h)) {
        sdc = GetDC(NULL);
        BitBlt(cap.dc, 0, 0, w, h, sdc, x, y, SRCCOPY);
        ReleaseDC(NULL, sdc);
        GdiFlush();
        free(g_start.bg);
        g_start.bg = (u32 *)malloc((size_t)w * h * sizeof(u32));
        if (g_start.bg) FrostCompose(cap.px, g_start.bg, w, h, 20, 22, 30, 26, 172);
        SurfFree(&cap);
    }
    g_startOpen = 1;
    g_start.hot = -1;
    PaintStart();
    ShowWindow(g_start.hwnd, SW_SHOWNA);
    SetForegroundWindow(g_start.hwnd);
    SetFocus(g_start.hwnd);
    InvalidateRect(g_start.hwnd, NULL, FALSE);
    PaintDock();
    Present(&g_dock);
}

/* ======================================================================== */
/* actions                                                                  */
/* ======================================================================== */

static void DoPower(int restart)
{
    HANDLE tok;
    TOKEN_PRIVILEGES tp;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) {
        LookupPrivilegeValueA(NULL, "SeShutdownPrivilege", &tp.Privileges[0].Luid);
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(tok, FALSE, &tp, 0, NULL, NULL);
        CloseHandle(tok);
    }
    Log("power: %s", restart ? "restart" : "shutdown");
    ExitWindowsEx((restart ? EWX_REBOOT : (EWX_SHUTDOWN | EWX_POWEROFF)) | EWX_FORCEIFHUNG, 0);
}

static void LaunchApp(int idx)
{
    App *a = &g_apps[idx];
    HINSTANCE r;

    if (!lstrcmpiA(a->cmd, "@wallpaper")) {
        NextWallpaper();
        return;
    }
    if (!lstrcmpiA(a->cmd, "@shutdown")) {
        DoPower(0);
        return;
    }
    if (!lstrcmpiA(a->cmd, "@restart")) {
        DoPower(1);
        return;
    }
    if (!AppInstalled(a)) {
        char m[500];
        Log("launch %s: not installed (%s)", a->name, a->cmd);
        wsprintfA(m, "%s is not installed on this system.", a->name);
        MessageBoxA(NULL, m, "avocados", MB_OK | MB_ICONINFORMATION);
        return;
    }
    r = ShellExecuteA(NULL, "open", a->cmd, a->args[0] ? a->args : NULL, NULL, SW_SHOWNORMAL);
    Log("launch %s: ShellExecute returned %ld (error %lu)", a->name, (long)(INT_PTR)r, GetLastError());
    if ((INT_PTR)r <= 32) {
        char m[500];
        wsprintfA(m, "%s could not be started (code %ld).", a->name, (long)(INT_PTR)r);
        MessageBoxA(NULL, m, "avocados", MB_OK | MB_ICONINFORMATION);
    }
}

static void RunText(const char *text)
{
    HINSTANCE r = ShellExecuteA(NULL, "open", text, NULL, NULL, SW_SHOWNORMAL);
    Log("run '%s': %ld", text, (long)(INT_PTR)r);
    if ((INT_PTR)r <= 32) MessageBoxA(NULL, "Windows cannot find that command.", "avocados", MB_OK | MB_ICONINFORMATION);
}

static void RebuildBackgrounds(void)
{
    ComposeStaticBg(&g_dock, 150);
    ComposeStaticBg(&g_tray, 150);
    PaintDock();
    PaintTray();
    Present(&g_dock);
    Present(&g_tray);
    InvalidateRect(g_deskWnd, NULL, FALSE);
}

static void NextWallpaper(void)
{
    char names[MAX_WALLS][64], ini[MAX_PATH];
    int n = ListWalls(names), i, next = 0;

    if (n == 0) return;
    for (i = 0; i < n; i++)
        if (!lstrcmpiA(names[i], g_wallName)) {
            next = (i + 1) % n;
            break;
        }
    SettingsPath(ini);
    WritePrivateProfileStringA("Shell", "Wallpaper", names[next], ini);
    LoadWallpaper(names[next]);
    RebuildBackgrounds();
}

static void RunSelected(void)
{
    int res[16], n = Results(res, 14);
    if (g_qlen == 0) return;
    if (g_sel >= 0 && g_sel < n) {
        ShowStart(0);
        LaunchApp(res[g_sel]);
    } else {
        char t[64];
        lstrcpynA(t, g_q, sizeof(t));
        ShowStart(0);
        RunText(t);
    }
}

static void Activate(Panel *p, int hit)
{
    Hit *h;
    if (hit < 0 || hit >= p->nh) return;
    h = &p->hits[hit];
    switch (h->kind) {
    case HK_START:
    case HK_SEARCH:
        if (g_startOpen) ShowStart(0);
        else if (GetTickCount() - g_startClosedAt > 350) ShowStart(1);
        break;
    case HK_APP:
        if (g_startOpen) ShowStart(0);
        LaunchApp(h->arg);
        break;
    case HK_WIN: {
        HWND t = g_tasks[h->arg].h;
        if (IsIconic(t)) {
            ShowWindow(t, SW_RESTORE);
            SetForegroundWindow(t);
        } else if (GetForegroundWindow() == t) {
            ShowWindow(t, SW_MINIMIZE);
        } else {
            SetForegroundWindow(t);
        }
        break;
    }
    case HK_REC:
        ShowStart(0);
        LaunchApp(g_rec[h->arg]);
        break;
    case HK_RESULT:
        g_sel = h->arg == 100 ? 99 : h->arg;
        RunSelected();
        break;
    case HK_POWER:
        g_powerOpen = !g_powerOpen;
        PaintStart();
        Present(&g_start);
        break;
    case HK_RESTART:
        DoPower(1);
        break;
    case HK_SHUTDOWN:
        DoPower(0);
        break;
    case HK_WALL:
        NextWallpaper();
        PaintStart();
        Present(&g_start);
        break;
    default:
        break;
    }
}

/* ======================================================================== */
/* automatic self test (only when config\autotest.ini exists)               */
/* ======================================================================== */

static char g_steps[MAX_STEPS][64];
static int g_nsteps, g_stepNow, g_stepDelay = 40, g_stepWait;

static void LoadAutotest(void)
{
    char ini[MAX_PATH], list[1500], *p, *q;
    wsprintfA(ini, "%s\\config\\autotest.ini", g_dir);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) return;
    g_stepDelay = (int)GetPrivateProfileIntA("Test", "StepDelay", 40, ini);
    g_stepWait = (int)GetPrivateProfileIntA("Test", "StartDelay", 60, ini);
    GetPrivateProfileStringA("Test", "Steps", "", list, sizeof(list), ini);
    for (p = list; *p && g_nsteps < MAX_STEPS;) {
        q = strchr(p, ',');
        if (q) *q = 0;
        lstrcpynA(g_steps[g_nsteps++], p, 64);
        if (!q) break;
        p = q + 1;
    }
    Log("autotest: %d steps, first after %ds, then every %ds", g_nsteps, g_stepWait, g_stepDelay);
}

static void AutotestTick(void)
{
    char *s;
    if (g_nsteps == 0 || g_stepNow >= g_nsteps) return;
    if (g_stepWait > 0) {
        g_stepWait--;
        return;
    }
    s = g_steps[g_stepNow++];
    g_stepWait = g_stepDelay;
    Log("autotest step: %s", s);
    if (!strcmp(s, "start")) ShowStart(1);
    else if (!strcmp(s, "closestart")) ShowStart(0);
    else if (!strcmp(s, "wall")) NextWallpaper();
    else if (!strncmp(s, "type:", 5)) {
        lstrcpynA(g_q, s + 5, sizeof(g_q));
        g_qlen = (int)strlen(g_q);
        g_sel = 0;
        PaintStart();
        Present(&g_start);
    } else if (!strncmp(s, "launch:", 7)) {
        int i = FindApp(s + 7);
        if (i >= 0) LaunchApp(i);
        else Log("autotest: no app named %s", s + 7);
    }
}

/* ======================================================================== */
/* window procedures                                                        */
/* ======================================================================== */

static void TrackLeave(Panel *p)
{
    TRACKMOUSEEVENT t;
    if (p->tracking) return;
    t.cbSize = sizeof(t);
    t.dwFlags = TME_LEAVE;
    t.hwndTrack = p->hwnd;
    t.dwHoverTime = 0;
    if (TrackMouseEvent(&t)) p->tracking = 1;
}

static void Repaint(Panel *p)
{
    if (p == &g_dock) PaintDock();
    else if (p == &g_tray) PaintTray();
    else PaintStart();
    Present(p);
}

static LRESULT CALLBACK PanelProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Panel *p = h == g_dock.hwnd ? &g_dock : (h == g_tray.hwnd ? &g_tray : &g_start);

    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (p->s.px) {
            GdiFlush();
            BitBlt(dc, 0, 0, p->s.w, p->s.h, p->s.dc, 0, 0, SRCCOPY);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return p == &g_start ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
        int hit = HitAt(p, (short)LOWORD(lp), (short)HIWORD(lp));
        TrackLeave(p);
        if (hit != p->hot) {
            p->hot = hit;
            Repaint(p);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        p->tracking = 0;
        if (p->hot != -1) {
            p->hot = -1;
            Repaint(p);
        }
        return 0;
    case WM_LBUTTONUP: {
        int hit = HitAt(p, (short)LOWORD(lp), (short)HIWORD(lp));
        if (hit >= 0) Activate(p, hit);
        return 0;
    }
    case WM_ACTIVATE:
        if (p == &g_start && LOWORD(wp) == WA_INACTIVE && g_startOpen) ShowStart(0);
        return 0;
    case WM_CHAR:
        if (p == &g_start) {
            if (wp >= 32 && wp < 127 && g_qlen < (int)sizeof(g_q) - 2) {
                g_q[g_qlen++] = (char)wp;
                g_q[g_qlen] = 0;
                g_sel = 0;
                g_powerOpen = 0;
                Repaint(p);
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (p == &g_start) {
            if (wp == VK_ESCAPE) {
                if (g_qlen) {
                    g_qlen = 0;
                    g_q[0] = 0;
                    Repaint(p);
                } else ShowStart(0);
            } else if (wp == VK_BACK && g_qlen > 0) {
                g_q[--g_qlen] = 0;
                g_sel = 0;
                Repaint(p);
            } else if (wp == VK_RETURN) {
                RunSelected();
            } else if (wp == VK_DOWN && g_qlen > 0) {
                int res[16], n = Results(res, 14);
                if (g_sel < n) g_sel++;
                Repaint(p);
            } else if (wp == VK_UP && g_sel > 0) {
                g_sel--;
                Repaint(p);
            }
            return 0;
        }
        break;
    case WM_HOTKEY:
        if (g_startOpen) ShowStart(0);
        else ShowStart(1);
        return 0;
    case WM_TIMER: {
        static int lastMin = -1;
        SYSTEMTIME st;
        int changed = RefreshTasks();
        AutotestTick();
        GetLocalTime(&st);
        if (changed) {
            LayoutDock();
            PaintDock();
            Present(&g_dock);
        } else {
            /* keep the active-window marker current */
            PaintDock();
            Present(&g_dock);
        }
        if (st.wMinute != lastMin) {
            lastMin = st.wMinute;
            PaintTray();
            Present(&g_tray);
        }
        return 0;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

static LRESULT CALLBACK DeskProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        if (g_desk.px) {
            GdiFlush();
            BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
                   ps.rcPaint.bottom - ps.rcPaint.top, g_desk.dc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (g_startOpen) ShowStart(0);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_WINDOWPOSCHANGING:
        ((WINDOWPOS *)lp)->hwndInsertAfter = HWND_BOTTOM;
        return 0;
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ======================================================================== */
/* entry point                                                              */
/* ======================================================================== */

static HWND MakePanel(HINSTANCE hi, const char *cls, DWORD exstyle, Panel *p, int x, int y, int w, int h, int visible)
{
    p->hwnd = CreateWindowExA(exstyle, cls, "avocados panel", WS_POPUP | (visible ? WS_VISIBLE : 0),
                              x, y, w, h, NULL, NULL, hi, NULL);
    p->hot = -1;
    return p->hwnd;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG msg;
    RECT wa;
    char *s;

    (void)hprev;
    (void)cmd;
    (void)show;

    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    s = strrchr(g_dir, '\\');
    if (s) *s = 0;

    g_sw = GetSystemMetrics(SM_CXSCREEN);
    g_sh = GetSystemMetrics(SM_CYSCREEN);
    Log("---- avocados shell starting, screen %dx%d, dir %s", g_sw, g_sh, g_dir);

    InitFonts();
    LoadConfig();

    wa.left = 0;
    wa.top = 0;
    wa.right = g_sw;
    wa.bottom = g_sh - WORK_RESERVE;
    SystemParametersInfoA(SPI_SETWORKAREA, 0, &wa, 0);

    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = hi;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpfnWndProc = DeskProc;
    wc.lpszClassName = "AvocadosDesk";
    RegisterClassA(&wc);
    wc.lpfnWndProc = PanelProc;
    wc.lpszClassName = "AvocadosPanel";
    RegisterClassA(&wc);

    SurfCreate(&g_desk, g_sw, g_sh);
    InitWallpaper();

    g_deskWnd = CreateWindowExA(WS_EX_TOOLWINDOW, "AvocadosDesk", "avocados", WS_POPUP | WS_VISIBLE,
                                0, 0, g_sw, g_sh, NULL, NULL, hi, NULL);
    SetWindowPos(g_deskWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    MakePanel(hi, "AvocadosPanel", WS_EX_TOOLWINDOW | WS_EX_TOPMOST, &g_dock, 0, 0, 10, DOCK_H, 0);
    MakePanel(hi, "AvocadosPanel", WS_EX_TOOLWINDOW | WS_EX_TOPMOST, &g_tray, 0, 0, TRAY_W, DOCK_H, 0);
    MakePanel(hi, "AvocadosPanel", WS_EX_TOOLWINDOW | WS_EX_TOPMOST, &g_start, 0, 0, START_W, 400, 0);

    RefreshTasks();
    LayoutDock();
    LayoutTray();
    PaintDock();
    PaintTray();
    ShowWindow(g_dock.hwnd, SW_SHOWNA);
    ShowWindow(g_tray.hwnd, SW_SHOWNA);
    SetWindowPos(g_dock.hwnd, HWND_TOPMOST, g_dock.x, g_dock.y, g_dock.w, g_dock.h, SWP_NOACTIVATE);
    SetWindowPos(g_tray.hwnd, HWND_TOPMOST, g_tray.x, g_tray.y, g_tray.w, g_tray.h, SWP_NOACTIVATE);
    InvalidateRect(g_deskWnd, NULL, FALSE);

    RegisterHotKey(g_dock.hwnd, 1, MOD_CONTROL, VK_ESCAPE);
    SetTimer(g_dock.hwnd, 1, 1000, NULL);
    LoadAutotest();
    Log("shell ready");

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
