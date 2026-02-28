#include <windows.h>
#include <wchar.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

/* ── Constants (Sabitler) ─────────────────────────────────────────────── */
#define MAX_ROWS   30
#define MAX_COLS   30
#define MIN_CELL   12     /* minimum cell pixel size (minimum hücre piksel boyutu) */
#define DEF_CELL   36     /* default cell size for initial window sizing (varsayılan hücre boyutu) */
#define BASE_ID    1000

#define IDM_NEW_GAME   2001
#define IDM_LANG_TR    2002
#define IDM_LANG_EN    2003
#define IDM_SETTINGS   2004

#define IDC_ROWS_EDIT   301
#define IDC_COLS_EDIT   302
#define IDC_MINES_EDIT  303
#define IDC_OK_BTN      304
#define IDC_CANCEL_BTN  305

/* ── Game state defaults (Oyun durumu varsayılanları) ────────────────── */
static int ROWS  = 9;
static int COLS  = 9;
static int MINES = 10;
static int lang  = 1;   /* 0 = TR, 1 = EN */

/* current dynamic cell size — updated by relayout() (dinamik hücre boyutu) */
static int cell_size = DEF_CELL;

static bool mines_grid[MAX_ROWS][MAX_COLS];
static bool revealed  [MAX_ROWS][MAX_COLS];
static bool flagged   [MAX_ROWS][MAX_COLS];
static int  neigh     [MAX_ROWS][MAX_COLS];
static HWND buttons   [MAX_ROWS][MAX_COLS];
static WNDPROC oldButtonProc[MAX_ROWS][MAX_COLS];
static int  revealedCount = 0;
static bool g_game_over   = false;
static HWND mainWindow;
static HINSTANCE hInst;

/* ── Localization (Yerelleştirme) ─────────────────────────────────────── */
typedef struct { const wchar_t *key, *tr, *en; } StrEntry;

static const StrEntry str_table[] = {
    {L"win_msg",       L"Tebrikler — kazandın!",                       L"Congratulations — you won!"},
    {L"win_title",     L"Kazandın",                                     L"You Won"},
    {L"lose_msg",      L"Mayına bastın! Oyun bitti.",                   L"You hit a mine! Game over."},
    {L"lose_title",    L"Oyun Bitti",                                   L"Game Over"},
    {L"menu_game",     L"Oyun",                                         L"Game"},
    {L"menu_new",      L"Yeni Oyun",                                    L"New Game"},
    {L"menu_options",  L"Seçenekler",                                   L"Options"},
    {L"menu_lang",     L"Dil",                                          L"Language"},
    {L"menu_settings", L"Oyun Ayarları",                                L"Game Settings"},
    {L"dlg_title",     L"Oyun Ayarları",                                L"Game Settings"},
    {L"lbl_rows",      L"Satır Sayısı (1-30):",                         L"Rows (1-30):"},
    {L"lbl_cols",      L"Sütun Sayısı (1-30):",                         L"Columns (1-30):"},
    {L"lbl_mines",     L"Mayın Sayısı:",                                L"Mines:"},
    {L"ok",            L"Tamam",                                        L"OK"},
    {L"cancel",        L"İptal",                                        L"Cancel"},
    {L"err_invalid",   L"Geçersiz değerler!\nSatır/Sütun: 1-30, Mayın: 1 ila (satır×sütun-1).",
                       L"Invalid values!\nRows/Cols: 1-30, Mines: 1 to (rows×cols-1)."},
    {NULL, NULL, NULL}
};

static const wchar_t *S(const wchar_t *key) {
    for (int i = 0; str_table[i].key; i++)
        if (wcscmp(str_table[i].key, key) == 0)
            return lang == 0 ? str_table[i].tr : str_table[i].en;
    return key;
}

/* ── Registry persistence (Kayıt defteri kalıcılığı) ─────────────────── */
#define REG_KEY L"Software\\MinesGame"

static void save_settings(void) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0,
                        KEY_WRITE, NULL, &hk, NULL) != ERROR_SUCCESS) return;
    DWORD v;
    v = (DWORD)lang;  RegSetValueExW(hk, L"Language", 0, REG_DWORD, (BYTE*)&v, sizeof v);
    v = (DWORD)ROWS;  RegSetValueExW(hk, L"Rows",     0, REG_DWORD, (BYTE*)&v, sizeof v);
    v = (DWORD)COLS;  RegSetValueExW(hk, L"Cols",     0, REG_DWORD, (BYTE*)&v, sizeof v);
    v = (DWORD)MINES; RegSetValueExW(hk, L"Mines",    0, REG_DWORD, (BYTE*)&v, sizeof v);
    RegCloseKey(hk);
}

static void load_settings(void) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hk) != ERROR_SUCCESS) return;
    DWORD v, sz = sizeof v;
    if (RegQueryValueExW(hk, L"Language", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS && v <= 1)
        lang = (int)v;
    sz = sizeof v;
    if (RegQueryValueExW(hk, L"Rows", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS
        && v >= 1 && v <= MAX_ROWS) ROWS = (int)v;
    sz = sizeof v;
    if (RegQueryValueExW(hk, L"Cols", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS
        && v >= 1 && v <= MAX_COLS) COLS = (int)v;
    sz = sizeof v;
    if (RegQueryValueExW(hk, L"Mines", NULL, NULL, (BYTE*)&v, &sz) == ERROR_SUCCESS
        && (int)v >= 1 && (int)v < ROWS * COLS) MINES = (int)v;
    RegCloseKey(hk);
}

/* ── Forward declarations (İleri bildirimler) ────────────────────────── */
LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK SettingsDlgProc(HWND, UINT, WPARAM, LPARAM);
void rebuild_buttons(void);
void relayout(int clientW, int clientH);
void update_menu(void);
void init_game(void);
void reveal_cell(int r, int c);
static HMENU create_menu(void);

/* ── Helper: grid index from control ID (kimlikten ızgara indeksi) ───── */
static void idx_from_id(int id, int *r, int *c) {
    int idx = id - BASE_ID;
    *r = idx / COLS;
    *c = idx % COLS;
}

/* ── Mine placement & neighbor count (Mayın yerleştirme ve komşu sayısı) */
static void place_mines_randomly(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            mines_grid[r][c] = false;
    int placed = 0;
    while (placed < MINES) {
        int r = rand() % ROWS, c = rand() % COLS;
        if (!mines_grid[r][c]) { mines_grid[r][c] = true; placed++; }
    }
}

static void compute_neighbors(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (mines_grid[r][c]) { neigh[r][c] = -1; continue; }
            int cnt = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (!dr && !dc) continue;
                    int rr = r+dr, cc = c+dc;
                    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && mines_grid[rr][cc]) cnt++;
                }
            neigh[r][c] = cnt;
        }
}

/* ── Game init (Oyun başlatma) ───────────────────────────────────────── */
void init_game(void) {
    revealedCount = 0;
    g_game_over   = false;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            revealed[r][c] = false;
            flagged [r][c] = false;
            if (buttons[r][c]) {
                SetWindowTextW(buttons[r][c], L"");
                EnableWindow(buttons[r][c], TRUE);
                InvalidateRect(buttons[r][c], NULL, TRUE);
            }
        }
    place_mines_randomly();
    compute_neighbors();
}

static void check_win(void) {
    if (revealedCount != ROWS * COLS - MINES) return;
    g_game_over = true;
    MessageBoxW(mainWindow, S(L"win_msg"), S(L"win_title"), MB_OK | MB_ICONINFORMATION);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            EnableWindow(buttons[r][c], FALSE);
}

static void reveal_all_mines(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (mines_grid[r][c]) {
                SetWindowTextW(buttons[r][c], L"*");
                EnableWindow(buttons[r][c], FALSE);
                InvalidateRect(buttons[r][c], NULL, TRUE);
            }
}

void reveal_cell(int r, int c) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    if (revealed[r][c] || flagged[r][c]) return;

    revealed[r][c] = true;
    revealedCount++;
    HWND btn = buttons[r][c];
    EnableWindow(btn, FALSE);
    InvalidateRect(btn, NULL, TRUE);

    if (mines_grid[r][c]) {
        SetWindowTextW(btn, L"*");
        reveal_all_mines();
        g_game_over = true;
        MessageBoxW(mainWindow, S(L"lose_msg"), S(L"lose_title"), MB_OK | MB_ICONERROR);
        for (int i = 0; i < ROWS; i++)
            for (int j = 0; j < COLS; j++)
                EnableWindow(buttons[i][j], FALSE);
        return;
    }

    int n = neigh[r][c];
    if (n > 0) {
        wchar_t buf[4];
        swprintf(buf, 4, L"%d", n);
        SetWindowTextW(btn, buf);
    }

    if (n == 0)
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++) {
                if (!dr && !dc) continue;
                int rr = r+dr, cc = c+dc;
                if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS
                    && !revealed[rr][cc] && !mines_grid[rr][cc] && !flagged[rr][cc])
                    reveal_cell(rr, cc);
            }

    check_win();
}

/* ── Layout engine (Düzen motoru) ────────────────────────────────────── */
/*
 * Called every time the client area changes size (WM_SIZE).
 * Picks the largest square cell that fits, centers the grid,
 * and batch-repositions all buttons via DeferWindowPos.
 *
 * Surplus space around the grid shows as the black background
 * (similar to Windows 7 Minesweeper behavior).
 *
 * (Her boyut değişiminde çağrılır. En büyük kare hücreyi seçer,
 *  ızgarayı ortalar ve tüm butonları toplu taşır. Fazla alan
 *  siyah arka plan olarak görünür — Win7 Minesweeper gibi.)
 */
void relayout(int clientW, int clientH) {
    if (!mainWindow) return;

    /* Largest square cell fitting both dimensions (her iki eksene sığan en büyük kare) */
    int cs = clientW / COLS;
    if (clientH / ROWS < cs) cs = clientH / ROWS;
    if (cs < MIN_CELL) cs = MIN_CELL;
    cell_size = cs;

    /* Center the grid (ızgarayı ortala) */
    int ox = (clientW  - COLS * cs) / 2;
    int oy = (clientH  - ROWS * cs) / 2;
    if (ox < 0) ox = 0;
    if (oy < 0) oy = 0;

    /* Batch-reposition (toplu yeniden konumlandır) */
    HDWP hdwp = BeginDeferWindowPos(ROWS * COLS);
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (buttons[r][c])
                hdwp = DeferWindowPos(hdwp, buttons[r][c], NULL,
                    ox + c * cs, oy + r * cs,
                    cs - 1, cs - 1,   /* 1-px gap between cells (hücreler arası 1 piksel boşluk) */
                    SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(hdwp);

    InvalidateRect(mainWindow, NULL, TRUE);
}

/* ── Menu (Menü) ─────────────────────────────────────────────────────── */
static HMENU create_menu(void) {
    HMENU hBar     = CreateMenu();
    HMENU hGame    = CreatePopupMenu();
    HMENU hOptions = CreatePopupMenu();
    HMENU hLang    = CreatePopupMenu();

    AppendMenuW(hGame, MF_STRING, IDM_NEW_GAME, S(L"menu_new"));
    AppendMenuW(hBar,  MF_POPUP,  (UINT_PTR)hGame,    S(L"menu_game"));

    AppendMenuW(hLang, MF_STRING | (lang == 0 ? MF_CHECKED : 0), IDM_LANG_TR, L"Türkçe");
    AppendMenuW(hLang, MF_STRING | (lang == 1 ? MF_CHECKED : 0), IDM_LANG_EN, L"English");
    AppendMenuW(hOptions, MF_POPUP,  (UINT_PTR)hLang,    S(L"menu_lang"));
    AppendMenuW(hOptions, MF_STRING, IDM_SETTINGS,        S(L"menu_settings"));
    AppendMenuW(hBar,  MF_POPUP,  (UINT_PTR)hOptions, S(L"menu_options"));

    return hBar;
}

void update_menu(void) {
    HMENU old = GetMenu(mainWindow);
    SetMenu(mainWindow, create_menu());
    if (old) DestroyMenu(old);
    DrawMenuBar(mainWindow);
}

/* ── Settings dialog (Ayarlar diyaloğu) ─────────────────────────────── */
static bool g_dlg_done = false, g_dlg_confirmed = false;
static int  g_new_rows, g_new_cols, g_new_mines;

LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        int y = 15, lw = 160, ew = 55, eh = 22, gap = 34;
        HWND h;

        h = CreateWindowW(L"STATIC", S(L"lbl_rows"), WS_CHILD|WS_VISIBLE, 10, y+2, lw, 20, hwnd, NULL, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        h = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,
            175, y, ew, eh, hwnd, (HMENU)(intptr_t)IDC_ROWS_EDIT, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        y += gap;

        h = CreateWindowW(L"STATIC", S(L"lbl_cols"), WS_CHILD|WS_VISIBLE, 10, y+2, lw, 20, hwnd, NULL, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        h = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,
            175, y, ew, eh, hwnd, (HMENU)(intptr_t)IDC_COLS_EDIT, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        y += gap;

        h = CreateWindowW(L"STATIC", S(L"lbl_mines"), WS_CHILD|WS_VISIBLE, 10, y+2, lw, 20, hwnd, NULL, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        h = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER,
            175, y, ew, eh, hwnd, (HMENU)(intptr_t)IDC_MINES_EDIT, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        y += gap + 10;

        h = CreateWindowW(L"BUTTON", S(L"ok"),     WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON,
            40,  y, 85, 28, hwnd, (HMENU)(intptr_t)IDC_OK_BTN, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);
        h = CreateWindowW(L"BUTTON", S(L"cancel"), WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
            140, y, 85, 28, hwnd, (HMENU)(intptr_t)IDC_CANCEL_BTN, hInst, NULL);
        SendMessageW(h, WM_SETFONT, (WPARAM)hf, FALSE);

        /* Fill current values (mevcut değerleri doldur) */
        wchar_t buf[8];
        swprintf(buf, 8, L"%d", ROWS);  SetDlgItemTextW(hwnd, IDC_ROWS_EDIT,  buf);
        swprintf(buf, 8, L"%d", COLS);  SetDlgItemTextW(hwnd, IDC_COLS_EDIT,  buf);
        swprintf(buf, 8, L"%d", MINES); SetDlgItemTextW(hwnd, IDC_MINES_EDIT, buf);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_OK_BTN) {
            wchar_t buf[8];
            GetDlgItemTextW(hwnd, IDC_ROWS_EDIT,  buf, 8); int nr = _wtoi(buf);
            GetDlgItemTextW(hwnd, IDC_COLS_EDIT,  buf, 8); int nc = _wtoi(buf);
            GetDlgItemTextW(hwnd, IDC_MINES_EDIT, buf, 8); int nm = _wtoi(buf);
            if (nr < 1 || nr > MAX_ROWS || nc < 1 || nc > MAX_COLS || nm < 1 || nm >= nr * nc) {
                MessageBoxW(hwnd, S(L"err_invalid"), L"!", MB_OK | MB_ICONWARNING);
            } else {
                g_new_rows = nr; g_new_cols = nc; g_new_mines = nm;
                g_dlg_confirmed = true;
                DestroyWindow(hwnd);
            }
        } else if (id == IDC_CANCEL_BTN) {
            DestroyWindow(hwnd);
        }
        break;
    }
    case WM_CLOSE:   DestroyWindow(hwnd); break;
    case WM_DESTROY: g_dlg_done = true;   break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void show_settings_dialog(void) {
    g_dlg_done = false; g_dlg_confirmed = false;

    RECT rc; GetWindowRect(mainWindow, &rc);
    int dlgW = 245, dlgH = 190;
    int x = rc.left + (rc.right  - rc.left - dlgW) / 2;
    int y = rc.top  + (rc.bottom - rc.top  - dlgH) / 2;

    EnableWindow(mainWindow, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"SettingsDlgClass", S(L"dlg_title"),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, dlgW, dlgH, mainWindow, NULL, hInst, NULL);
    ShowWindow(dlg, SW_SHOW); UpdateWindow(dlg);

    MSG m;
    while (!g_dlg_done && GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m); DispatchMessage(&m);
    }
    EnableWindow(mainWindow, TRUE);
    SetForegroundWindow(mainWindow);

    if (g_dlg_confirmed) {
        ROWS = g_new_rows; COLS = g_new_cols; MINES = g_new_mines;
        save_settings();
        rebuild_buttons();
        /* Re-run layout with current client size (mevcut boyutuyla düzenle) */
        RECT cr; GetClientRect(mainWindow, &cr);
        relayout(cr.right, cr.bottom);
    }
}

/* ── Button (re)creation (Buton oluşturma/yeniden oluşturma) ─────────── */
void rebuild_buttons(void) {
    /* Destroy old buttons (eski butonları yok et) */
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            if (buttons[r][c]) { DestroyWindow(buttons[r][c]); buttons[r][c] = NULL; }

    /* Create new grid — positions are set later by relayout() (konumlar relayout tarafından ayarlanır) */
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            int id = BASE_ID + r * COLS + c;
            buttons[r][c] = CreateWindowW(L"BUTTON", L"",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                0, 0, cell_size - 1, cell_size - 1,
                mainWindow, (HMENU)(intptr_t)id, hInst, NULL);
            oldButtonProc[r][c] = (WNDPROC)SetWindowLongPtrW(
                buttons[r][c], GWLP_WNDPROC, (LONG_PTR)ButtonProc);
            SendMessageW(buttons[r][c], WM_SETFONT,
                (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        }
    init_game();
}

/* ── Right-click subclass for flag toggling (Bayrak için sağ-tık alt sınıfı) */
LRESULT CALLBACK ButtonProc(HWND hwndBtn, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_RBUTTONUP) {
        int id = GetDlgCtrlID(hwndBtn);
        if (id >= BASE_ID && id < BASE_ID + ROWS * COLS) {
            int r, c; idx_from_id(id, &r, &c);
            if (!revealed[r][c]) {
                flagged[r][c] = !flagged[r][c];
                SetWindowTextW(buttons[r][c], flagged[r][c] ? L"F" : L"");
                InvalidateRect(buttons[r][c], NULL, TRUE);
            }
        }
        return 0;
    }
    int id = GetDlgCtrlID(hwndBtn);
    if (id >= BASE_ID && id < BASE_ID + ROWS * COLS) {
        int r, c; idx_from_id(id, &r, &c);
        return CallWindowProcW(oldButtonProc[r][c], hwndBtn, msg, wParam, lParam);
    }
    return DefWindowProcW(hwndBtn, msg, wParam, lParam);
}

/* ── Main window procedure (Ana pencere yordamı) ─────────────────────── */
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    /* Resize: recalculate cell size and reposition grid (yeniden boyutlandırma) */
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            relayout(LOWORD(lParam), HIWORD(lParam));
        break;

    /* Enforce a sensible minimum so cells never collapse (minimum pencere boyutu) */
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO*)lParam;
        RECT rc = {0, 0, COLS * MIN_CELL, ROWS * MIN_CELL};
        AdjustWindowRectEx(&rc, (DWORD)GetWindowLongW(hwnd, GWL_STYLE), TRUE, 0);
        mmi->ptMinTrackSize.x = rc.right  - rc.left;
        mmi->ptMinTrackSize.y = rc.bottom - rc.top;
        break;
    }

    /* Dark background outside the grid — Win7 Minesweeper style
       (ızgara dışında koyu arka plan, Win7 Minesweeper gibi) */
    case WM_ERASEBKGND: {
        RECT cr; GetClientRect(hwnd, &cr);
        FillRect((HDC)wParam, &cr, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDM_NEW_GAME) {
            init_game(); InvalidateRect(hwnd, NULL, TRUE);
        } else if (id == IDM_LANG_TR) {
            lang = 0; save_settings(); update_menu();
        } else if (id == IDM_LANG_EN) {
            lang = 1; save_settings(); update_menu();
        } else if (id == IDM_SETTINGS) {
            show_settings_dialog();
        } else if (id >= BASE_ID && id < BASE_ID + ROWS * COLS) {
            int r, c; idx_from_id(id, &r, &c);
            if (!flagged[r][c] && !revealed[r][c]) reveal_cell(r, c);
        }
        break;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis) break;
        int idx = (int)dis->CtlID - BASE_ID;
        if (idx < 0 || idx >= ROWS * COLS)
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        int r = idx / COLS, c = idx % COLS;

        /* Cell background (hücre arka planı) */
        HBRUSH br = CreateSolidBrush(revealed[r][c] ? RGB(192,192,192) : RGB(220,220,220));
        FillRect(dis->hDC, &dis->rcItem, br);
        DeleteObject(br);

        /* 3-D edge (3-D kenar) */
        DrawEdge(dis->hDC, &dis->rcItem,
            (dis->itemState & ODS_SELECTED) ? BDR_SUNKENINNER : BDR_RAISEDINNER, BF_RECT);

        /* Text (metin) — skip if empty (boşsa atla) */
        wchar_t buf[8] = L"";
        GetWindowTextW(dis->hwndItem, buf, 8);
        if (buf[0] == L'\0') return TRUE;

        SetBkMode(dis->hDC, TRANSPARENT);

        /* Classic Minesweeper digit colors (klasik sayı renkleri) */
        static const COLORREF numColors[] = {
            0,
            RGB(0,   0, 200),  /* 1 – blue (mavi) */
            RGB(0, 130,   0),  /* 2 – green (yeşil) */
            RGB(200,  0,   0), /* 3 – red (kırmızı) */
            RGB(0,   0, 130),  /* 4 – dark blue (koyu mavi) */
            RGB(130,  0,   0), /* 5 – dark red (koyu kırmızı) */
            RGB(0,  130, 130), /* 6 – teal (camgöbeği) */
            RGB(80,  80,  80), /* 7 – dark grey (koyu gri) */
            RGB(80,  80,  80), /* 8 – dark grey (koyu gri) */
        };

        COLORREF col;
        if (flagged[r][c] && !revealed[r][c]) {
            col = RGB(200, 0, 0);                   /* flag: red (bayrak: kırmızı) */
        } else if (revealed[r][c] && !mines_grid[r][c]) {
            int n = neigh[r][c];
            col = (n >= 1 && n <= 8) ? numColors[n] : RGB(0, 0, 0);
        } else {
            col = RGB(60, 60, 60);                  /* mine / disabled (mayın / devre dışı) */
        }

        HFONT hf  = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        HFONT old = (HFONT)SelectObject(dis->hDC, hf);
        SetTextColor(dis->hDC, col);
        DrawTextW(dis->hDC, buf, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dis->hDC, old);
        return TRUE;
    }

    case WM_CLOSE:   DestroyWindow(hwnd); break;
    case WM_DESTROY: PostQuitMessage(0);  break;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

/* ── Entry point (Giriş noktası) ─────────────────────────────────────── */
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR pCmd, int nCmdShow) {
    hInst = hInstance;
    srand((unsigned)time(NULL));
    load_settings();

    /* Register main window class (ana pencere sınıfını kaydet) */
    WNDCLASSW wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"MSimpleClass";
    wc.hbrBackground = NULL;   /* WM_ERASEBKGND handles background (arka planı biz çiziyoruz) */
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    /* Register settings dialog class (ayarlar diyaloğu sınıfını kaydet) */
    WNDCLASSW wcs = {0};
    wcs.lpfnWndProc   = SettingsDlgProc;
    wcs.hInstance     = hInstance;
    wcs.lpszClassName = L"SettingsDlgClass";
    wcs.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcs.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wcs);

    /*
     * Compute initial window size from DEF_CELL, then clamp to the OS work area
     * so large grids (e.g. 30x30) don't start off-screen, and center on screen.
     * (Başlangıç boyutunu hesapla, görev çubuğu hariç çalışma alanına sığdır, ortala.)
     */
    DWORD winStyle = WS_OVERLAPPEDWINDOW;   /* resizable + maximizable (boyutlandırılabilir + büyütülebilir) */

    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int wa_w = wa.right - wa.left;
    int wa_h = wa.bottom - wa.top;

    /* Desired window size for the default cell size (varsayılan hücre boyutu için istenen boyut) */
    RECT rc = {0, 0, COLS * DEF_CELL, ROWS * DEF_CELL};
    AdjustWindowRectEx(&rc, winStyle, TRUE, 0);
    int win_w = rc.right  - rc.left;
    int win_h = rc.bottom - rc.top;

    /* Clamp with a small margin so the window fits on screen (ekrana sığdır) */
    if (win_w > wa_w - 40) win_w = wa_w - 40;
    if (win_h > wa_h - 40) win_h = wa_h - 40;

    /* Center on work area (çalışma alanında ortala) */
    int win_x = wa.left + (wa_w - win_w) / 2;
    int win_y = wa.top  + (wa_h - win_h) / 2;

    mainWindow = CreateWindowW(L"MSimpleClass", L"Mines", winStyle,
        win_x, win_y, win_w, win_h, NULL, NULL, hInstance, NULL);
    if (!mainWindow) return 0;

    SetMenu(mainWindow, create_menu());

    /* Initialize button array (buton dizisini sıfırla) */
    for (int r = 0; r < MAX_ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++)
            buttons[r][c] = NULL;

    rebuild_buttons();

    /* Run first layout pass with the actual client size (gerçek istemci boyutuyla ilk düzeni çalıştır) */
    RECT cr; GetClientRect(mainWindow, &cr);
    relayout(cr.right, cr.bottom);

    ShowWindow(mainWindow, nCmdShow);
    UpdateWindow(mainWindow);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    return (int)m.wParam;
}
