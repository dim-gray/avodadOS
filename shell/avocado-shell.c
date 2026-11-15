/*
 * avocadOS shell: a full-screen tile launcher plus a taskbar.
 * Plain Win32 + GDI only (no GDI+, no theming APIs) so it stays within what ReactOS offers.
 * Build: i686-w64-mingw32-gcc -Os -s -mwindows -o avocado-shell.exe avocado-shell.c -luser32 -lgdi32 -lshell32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#define MAX_TILES 32
#define MAX_TASKS 20
#define MAX_WALLS 64
#define BAR_H 44
#define HOME_W 120
#define CLOCK_W 90
#define TILE_W 200
#define TILE_H 104
#define GAP 16
#define TOP_H 150
#define ID_TICK 1

typedef struct {
    char name[64];
    char cmd[300];
    char args[300];
} Tile;

typedef struct {
    HWND h;
    char title[80];
} Task;

static Tile g_tiles[MAX_TILES];
static int g_ntiles = 0;
static int g_hot = -1;
static Task g_tasks[MAX_TASKS];
static int g_ntasks = 0;
static HWND g_main, g_bar;
static HBITMAP g_wall = NULL;
static int g_w, g_h;
static char g_dir[MAX_PATH];
static HFONT g_fTitle, g_fTile, g_fGlyph, g_fSmall;

static const char *g_defaults[] = {
    "Firefox=%ProgramFiles%\\Mozilla Firefox\\firefox.exe|",
    "Notepad=notepad.exe|",
    "Terminal=cmd.exe|",
    "Wallpaper=@wallpaper",
    "Restart=@restart",
    "Shut down=@shutdown"
};

static const COLORREF g_palette[] = {
    RGB(74, 116, 28), RGB(96, 130, 40), RGB(58, 96, 44), RGB(120, 96, 52),
    RGB(84, 110, 64), RGB(104, 84, 44)
};

/* ---------- configuration ---------- */

static void AddTileLine(const char *line)
{
    const char *eq, *v, *bar;
    char raw[300];
    size_t nl, cl;
    Tile *t;

    if (g_ntiles >= MAX_TILES) return;
    eq = strchr(line, '=');
    if (!eq) return;
    t = &g_tiles[g_ntiles];
    nl = (size_t)(eq - line);
    if (nl >= sizeof(t->name)) nl = sizeof(t->name) - 1;
    memcpy(t->name, line, nl);
    t->name[nl] = 0;

    v = eq + 1;
    bar = strchr(v, '|');
    cl = bar ? (size_t)(bar - v) : strlen(v);
    if (cl >= sizeof(raw)) cl = sizeof(raw) - 1;
    memcpy(raw, v, cl);
    raw[cl] = 0;
    ExpandEnvironmentStringsA(raw, t->cmd, sizeof(t->cmd));
    t->args[0] = 0;
    if (bar) {
        char rawargs[300];
        lstrcpynA(rawargs, bar + 1, sizeof(rawargs));
        ExpandEnvironmentStringsA(rawargs, t->args, sizeof(t->args));
    }
    g_ntiles++;
}

static void LoadTiles(void)
{
    char ini[MAX_PATH];
    char buf[8192];
    DWORD n;
    char *p;
    int i;

    wsprintfA(ini, "%s\\config\\apps.ini", g_dir);
    n = GetPrivateProfileSectionA("Apps", buf, sizeof(buf), ini);
    if (n > 0) {
        for (p = buf; *p; p += strlen(p) + 1) AddTileLine(p);
    }
    if (g_ntiles == 0) {
        for (i = 0; i < (int)(sizeof(g_defaults) / sizeof(g_defaults[0])); i++)
            AddTileLine(g_defaults[i]);
    }
}

/* ---------- wallpapers ---------- */

static void SettingsPath(char *out)
{
    wsprintfA(out, "%s\\config\\settings.ini", g_dir);
}

static void LoadWall(const char *file)
{
    char p[MAX_PATH];
    if (g_wall) {
        DeleteObject(g_wall);
        g_wall = NULL;
    }
    wsprintfA(p, "%s\\wallpapers\\%s", g_dir, file);
    g_wall = (HBITMAP)LoadImageA(NULL, p, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
}

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

static void InitWallpaper(void)
{
    char names[MAX_WALLS][64];
    char ini[MAX_PATH], cur[64];
    int n = ListWalls(names);
    if (n == 0) return;
    SettingsPath(ini);
    GetPrivateProfileStringA("Shell", "Wallpaper", names[0], cur, sizeof(cur), ini);
    LoadWall(cur);
    if (!g_wall) LoadWall(names[0]);
}

static void NextWallpaper(void)
{
    char names[MAX_WALLS][64];
    char ini[MAX_PATH], cur[64];
    int n = ListWalls(names), i, next = 0;

    if (n == 0) return;
    SettingsPath(ini);
    GetPrivateProfileStringA("Shell", "Wallpaper", "", cur, sizeof(cur), ini);
    for (i = 0; i < n; i++) {
        if (!lstrcmpiA(names[i], cur)) {
            next = (i + 1) % n;
            break;
        }
    }
    WritePrivateProfileStringA("Shell", "Wallpaper", names[next], ini);
    LoadWall(names[next]);
    InvalidateRect(g_main, NULL, FALSE);
}

/* ---------- launching ---------- */

static void Launch(int i)
{
    Tile *t = &g_tiles[i];
    HINSTANCE r;

    if (!lstrcmpiA(t->cmd, "@wallpaper")) {
        NextWallpaper();
        return;
    }
    if (!lstrcmpiA(t->cmd, "@shutdown")) {
        ShellExecuteA(NULL, "open", "shutdown.exe", "-s -t 0", NULL, SW_HIDE);
        return;
    }
    if (!lstrcmpiA(t->cmd, "@restart")) {
        ShellExecuteA(NULL, "open", "shutdown.exe", "-r -t 0", NULL, SW_HIDE);
        return;
    }
    r = ShellExecuteA(NULL, "open", t->cmd, t->args[0] ? t->args : NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)r <= 32) {
        char m[500];
        wsprintfA(m, "%s could not be started.\n\nIt may not be installed yet. "
                     "Run avocados\\first-boot.cmd from the install disc.", t->name);
        MessageBoxA(g_main, m, "avocadOS", MB_OK | MB_ICONINFORMATION);
    }
}

/* If apps were never installed, find the install disc and run its first-boot script. */
static void MaybeFirstBoot(void)
{
    char sd[MAX_PATH], marker[MAX_PATH], drive[4], script[MAX_PATH], cl[MAX_PATH + 32];
    char c;

    if (!GetEnvironmentVariableA("SystemDrive", sd, sizeof(sd))) lstrcpyA(sd, "C:");
    wsprintfA(marker, "%s\\avocadOS\\firstboot.lock", sd);
    if (GetFileAttributesA(marker) != INVALID_FILE_ATTRIBUTES) return;

    drive[1] = ':';
    drive[2] = '\\';
    drive[3] = 0;
    for (c = 'A'; c <= 'Z'; c++) {
        drive[0] = c;
        if (GetDriveTypeA(drive) != DRIVE_CDROM) continue;
        wsprintfA(script, "%savocados\\first-boot.cmd", drive);
        if (GetFileAttributesA(script) != INVALID_FILE_ATTRIBUTES) {
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_SHOWMINNOACTIVE;
            wsprintfA(cl, "cmd.exe /c \"%s\"", script);
            if (CreateProcessA(NULL, cl, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            return;
        }
    }
}

/* ---------- launcher window ---------- */

static void TileRect(int i, RECT *r)
{
    int cols = (g_w - 80) / (TILE_W + GAP);
    int total, x0, col, row;
    if (cols < 1) cols = 1;
    if (cols > 6) cols = 6;
    total = cols * (TILE_W + GAP) - GAP;
    x0 = (g_w - total) / 2;
    col = i % cols;
    row = i / cols;
    r->left = x0 + col * (TILE_W + GAP);
    r->top = TOP_H + row * (TILE_H + GAP);
    r->right = r->left + TILE_W;
    r->bottom = r->top + TILE_H;
}

static COLORREF Lighten(COLORREF c)
{
    int r = GetRValue(c) + 34, g = GetGValue(c) + 34, b = GetBValue(c) + 34;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

static void DrawLogo(HDC dc, int x, int y, int s)
{
    HBRUSH skin = CreateSolidBrush(RGB(52, 84, 22));
    HBRUSH flesh = CreateSolidBrush(RGB(196, 220, 96));
    HBRUSH pit = CreateSolidBrush(RGB(122, 74, 34));
    HGDIOBJ oldB = SelectObject(dc, skin);
    HGDIOBJ oldP = SelectObject(dc, GetStockObject(NULL_PEN));

    Ellipse(dc, x, y, x + s, y + s + s / 4);
    SelectObject(dc, flesh);
    Ellipse(dc, x + s / 10, y + s / 10, x + s - s / 10, y + s + s / 4 - s / 10);
    SelectObject(dc, pit);
    Ellipse(dc, x + s / 3, y + s / 2, x + s * 2 / 3, y + s / 2 + s / 3);

    SelectObject(dc, oldP);
    SelectObject(dc, oldB);
    DeleteObject(skin);
    DeleteObject(flesh);
    DeleteObject(pit);
}

static void PaintMain(HDC dc, const RECT *clip)
{
    HDC m = CreateCompatibleDC(dc);
    HBITMAP bb = CreateCompatibleBitmap(dc, g_w, g_h);
    HGDIOBJ oldBmp = SelectObject(m, bb);
    HGDIOBJ oldFont;
    RECT full;
    SYSTEMTIME st;
    char buf[96], date[80];
    int i;

    full.left = 0; full.top = 0; full.right = g_w; full.bottom = g_h;

    if (g_wall) {
        HDC w = CreateCompatibleDC(dc);
        HGDIOBJ ow = SelectObject(w, g_wall);
        BITMAP bm;
        GetObject(g_wall, sizeof(bm), &bm);
        SetStretchBltMode(m, COLORONCOLOR);
        StretchBlt(m, 0, 0, g_w, g_h, w, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
        SelectObject(w, ow);
        DeleteDC(w);
    } else {
        HBRUSH bg = CreateSolidBrush(RGB(34, 58, 24));
        FillRect(m, &full, bg);
        DeleteObject(bg);
    }

    SetBkMode(m, TRANSPARENT);

    /* header */
    DrawLogo(m, 40, 34, 64);
    oldFont = SelectObject(m, g_fTitle);
    SetTextColor(m, RGB(255, 255, 255));
    TextOutA(m, 126, 30, "avocadOS", 8);
    SelectObject(m, g_fSmall);
    SetTextColor(m, RGB(222, 236, 188));
    TextOutA(m, 130, 84, "light, fast, 32-bit", 19);

    GetLocalTime(&st);
    wsprintfA(buf, "%02d:%02d", st.wHour, st.wMinute);
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL, date, sizeof(date));
    SelectObject(m, g_fTitle);
    SetTextColor(m, RGB(255, 255, 255));
    SetTextAlign(m, TA_RIGHT);
    TextOutA(m, g_w - 40, 30, buf, lstrlenA(buf));
    SelectObject(m, g_fSmall);
    SetTextColor(m, RGB(222, 236, 188));
    TextOutA(m, g_w - 40, 84, date, lstrlenA(date));
    SetTextAlign(m, TA_LEFT);

    /* tiles */
    for (i = 0; i < g_ntiles; i++) {
        RECT r;
        COLORREF c = g_palette[i % (sizeof(g_palette) / sizeof(g_palette[0]))];
        HBRUSH b;
        HGDIOBJ ob, op;
        char glyph[2];

        TileRect(i, &r);
        if (i == g_hot) c = Lighten(c);
        b = CreateSolidBrush(c);
        ob = SelectObject(m, b);
        op = SelectObject(m, GetStockObject(NULL_PEN));
        RoundRect(m, r.left, r.top, r.right, r.bottom, 18, 18);
        SelectObject(m, op);
        SelectObject(m, ob);
        DeleteObject(b);

        glyph[0] = g_tiles[i].name[0];
        glyph[1] = 0;
        SelectObject(m, g_fGlyph);
        SetTextColor(m, Lighten(Lighten(c)));
        TextOutA(m, r.left + 16, r.top + 6, glyph, 1);
        SelectObject(m, g_fTile);
        SetTextColor(m, RGB(255, 255, 255));
        TextOutA(m, r.left + 16, r.bottom - 30, g_tiles[i].name, lstrlenA(g_tiles[i].name));
    }

    SelectObject(m, oldFont);
    BitBlt(dc, clip->left, clip->top, clip->right - clip->left, clip->bottom - clip->top,
           m, clip->left, clip->top, SRCCOPY);
    SelectObject(m, oldBmp);
    DeleteObject(bb);
    DeleteDC(m);
}

static LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        SetTimer(h, ID_TICK, 1000, NULL);
        return 0;
    case WM_TIMER: {
        RECT r;
        r.left = 0; r.top = 0; r.right = g_w; r.bottom = TOP_H;
        InvalidateRect(h, &r, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintMain(dc, &ps.rcPaint);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p;
        int i, hot = -1;
        p.x = (short)LOWORD(lp);
        p.y = (short)HIWORD(lp);
        for (i = 0; i < g_ntiles; i++) {
            RECT r;
            TileRect(i, &r);
            if (PtInRect(&r, p)) { hot = i; break; }
        }
        if (hot != g_hot) {
            g_hot = hot;
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP:
        if (g_hot >= 0) Launch(g_hot);
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_WINDOWPOSCHANGING:
        ((WINDOWPOS *)lp)->hwndInsertAfter = HWND_BOTTOM;
        return 0;
    case WM_CLOSE:
        return 0; /* the shell must not exit */
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ---------- taskbar window ---------- */

static BOOL CALLBACK EnumProc(HWND h, LPARAM lp)
{
    char t[80];
    (void)lp;
    if (h == g_main || h == g_bar) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindow(h, GW_OWNER)) return TRUE;
    if (GetWindowLongA(h, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
    if (!GetWindowTextA(h, t, sizeof(t))) return TRUE;
    if (g_ntasks < MAX_TASKS) {
        g_tasks[g_ntasks].h = h;
        lstrcpynA(g_tasks[g_ntasks].title, t, sizeof(g_tasks[g_ntasks].title));
        g_ntasks++;
    }
    return TRUE;
}

static void RefreshTasks(void)
{
    g_ntasks = 0;
    EnumWindows(EnumProc, 0);
}

static int TaskButtonWidth(void)
{
    int avail = g_w - HOME_W - CLOCK_W - 24;
    int w;
    if (g_ntasks == 0) return 0;
    w = avail / g_ntasks;
    return w > 200 ? 200 : w;
}

static void PaintBar(HDC dc)
{
    RECT r;
    HBRUSH bg = CreateSolidBrush(RGB(26, 36, 20));
    HBRUSH btn = CreateSolidBrush(RGB(58, 84, 40));
    HBRUSH home = CreateSolidBrush(RGB(74, 116, 28));
    HGDIOBJ oldFont = SelectObject(dc, g_fSmall);
    SYSTEMTIME st;
    char tm[16];
    int i, bw = TaskButtonWidth(), x = HOME_W + 12;
    HWND fg = GetForegroundWindow();

    r.left = 0; r.top = 0; r.right = g_w; r.bottom = BAR_H;
    FillRect(dc, &r, bg);
    SetBkMode(dc, TRANSPARENT);

    r.left = 6; r.top = 6; r.right = HOME_W; r.bottom = BAR_H - 6;
    FillRect(dc, &r, home);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutA(dc, 20, 12, "Home", 4);

    for (i = 0; i < g_ntasks; i++) {
        r.left = x; r.top = 6; r.right = x + bw - 6; r.bottom = BAR_H - 6;
        if (g_tasks[i].h == fg) {
            HBRUSH on = CreateSolidBrush(RGB(110, 150, 60));
            FillRect(dc, &r, on);
            DeleteObject(on);
        } else {
            FillRect(dc, &r, btn);
        }
        SetTextColor(dc, RGB(255, 255, 255));
        DrawTextA(dc, g_tasks[i].title, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        x += bw;
    }

    GetLocalTime(&st);
    wsprintfA(tm, "%02d:%02d", st.wHour, st.wMinute);
    SetTextColor(dc, RGB(226, 238, 190));
    SetTextAlign(dc, TA_RIGHT);
    TextOutA(dc, g_w - 16, 12, tm, lstrlenA(tm));
    SetTextAlign(dc, TA_LEFT);

    SelectObject(dc, oldFont);
    DeleteObject(bg);
    DeleteObject(btn);
    DeleteObject(home);
}

static LRESULT CALLBACK BarProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        SetTimer(h, ID_TICK, 1000, NULL);
        return 0;
    case WM_TIMER:
        RefreshTasks();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        PaintBar(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_LBUTTONUP: {
        int x = (short)LOWORD(lp);
        int i, bw = TaskButtonWidth();
        if (x < HOME_W) {
            for (i = 0; i < g_ntasks; i++) ShowWindow(g_tasks[i].h, SW_MINIMIZE);
        } else if (bw > 0) {
            i = (x - (HOME_W + 12)) / bw;
            if (i >= 0 && i < g_ntasks) {
                HWND t = g_tasks[i].h;
                if (IsIconic(t)) {
                    ShowWindow(t, SW_RESTORE);
                    SetForegroundWindow(t);
                } else if (GetForegroundWindow() == t) {
                    ShowWindow(t, SW_MINIMIZE);
                } else {
                    SetForegroundWindow(t);
                }
            }
        }
        RefreshTasks();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_CLOSE:
        return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

/* ---------- entry point ---------- */

static HFONT MakeFont(int height, int weight)
{
    return CreateFontA(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, "Arial");
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hprev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    MSG msg;
    RECT wa;
    char *s;

    (void)hprev; (void)cmd; (void)show;

    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    s = strrchr(g_dir, '\\');
    if (s) *s = 0;

    g_w = GetSystemMetrics(SM_CXSCREEN);
    g_h = GetSystemMetrics(SM_CYSCREEN);

    g_fTitle = MakeFont(40, FW_BOLD);
    g_fTile = MakeFont(20, FW_BOLD);
    g_fGlyph = MakeFont(56, FW_BOLD);
    g_fSmall = MakeFont(16, FW_NORMAL);

    wa.left = 0; wa.top = 0; wa.right = g_w; wa.bottom = g_h - BAR_H;
    SystemParametersInfoA(SPI_SETWORKAREA, 0, &wa, 0);

    LoadTiles();
    InitWallpaper();
    MaybeFirstBoot();

    ZeroMemory(&wc, sizeof(wc));
    wc.hInstance = hi;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = "AvocadoMain";
    RegisterClassA(&wc);
    wc.lpfnWndProc = BarProc;
    wc.lpszClassName = "AvocadoBar";
    RegisterClassA(&wc);

    g_main = CreateWindowExA(WS_EX_TOOLWINDOW, "AvocadoMain", "avocadOS", WS_POPUP | WS_VISIBLE,
                             0, 0, g_w, g_h - BAR_H, NULL, NULL, hi, NULL);
    g_bar = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "AvocadoBar", "avocadOS bar",
                            WS_POPUP | WS_VISIBLE, 0, g_h - BAR_H, g_w, BAR_H, NULL, NULL, hi, NULL);
    SetWindowPos(g_main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RefreshTasks();

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
