#include <windows.h>
#include <wchar.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#define ROWS 9
#define COLS 9
#define MINES 10
#define CELL_SIZE 36
#define BASE_ID 1000

static bool mines[ROWS][COLS];
static bool revealed[ROWS][COLS];
static bool flagged[ROWS][COLS];
static int neigh[ROWS][COLS];
static HWND buttons[ROWS][COLS];
static WNDPROC oldButtonProc[ROWS][COLS]; // store original procs
static int revealedCount = 0;
static HWND mainWindow;

// forward
void init_game(void);
void reveal_cell(int r, int c);
void reveal_all_mines(void);
void check_win(void);

// helper to compute r,c from control id
static void idx_from_id(int id, int *out_r, int *out_c) {
    int idx = id - BASE_ID;
    *out_r = idx / COLS;
    *out_c = idx % COLS;
}

void place_mines_randomly(void) {
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            mines[r][c] = false;

    int placed = 0;
    while (placed < MINES) {
        int r = rand() % ROWS;
        int c = rand() % COLS;
        if (!mines[r][c]) {
            mines[r][c] = true;
            placed++;
        }
    }
}

void compute_neighbors(void) {
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            if (mines[r][c]) { neigh[r][c] = -1; continue; }
            int cnt = 0;
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    int rr = r + dr, cc = c + dc;
                    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && mines[rr][cc]) cnt++;
                }
            neigh[r][c] = cnt;
        }
    }
}

void init_game(void) {
    revealedCount = 0;
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c) {
            revealed[r][c] = false;
            flagged[r][c] = false;
            SetWindowTextW(buttons[r][c], L"");
            EnableWindow(buttons[r][c], TRUE);
            InvalidateRect(buttons[r][c], NULL, TRUE);
            UpdateWindow(buttons[r][c]);
            SendMessageW(buttons[r][c], WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        }

    place_mines_randomly();
    compute_neighbors();
}

void set_button_text_number(HWND btn, int n) {
    wchar_t buf[8];
    if (n > 0) {
        swprintf(buf, sizeof(buf)/sizeof(wchar_t), L"%d", n);
        SetWindowTextW(btn, buf);
    } else {
        SetWindowTextW(btn, L"");
    }
}

void check_win(void) {
    int totalCells = ROWS * COLS;
    if (revealedCount == totalCells - MINES) {
        MessageBoxW(mainWindow, L"Tebrikler — kazandın!", L"Win", MB_OK | MB_ICONINFORMATION);
        for (int i = 0; i < ROWS; ++i)
            for (int j = 0; j < COLS; ++j)
                EnableWindow(buttons[i][j], FALSE);
    }
}

void reveal_all_mines(void) {
    for (int r = 0; r < ROWS; ++r)
        for (int c = 0; c < COLS; ++c)
            if (mines[r][c]) {
                SetWindowTextW(buttons[r][c], L"*");
                EnableWindow(buttons[r][c], FALSE);
                InvalidateRect(buttons[r][c], NULL, TRUE);
                UpdateWindow(buttons[r][c]);
            }
}

void reveal_cell(int r, int c) {
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    if (revealed[r][c]) return;
    if (flagged[r][c]) return;

    revealed[r][c] = true;
    revealedCount++;
    HWND btn = buttons[r][c];

    EnableWindow(btn, FALSE);
    InvalidateRect(btn, NULL, TRUE);
    UpdateWindow(btn);

    if (mines[r][c]) {
        SetWindowTextW(btn, L"*");
        reveal_all_mines();
        MessageBoxW(mainWindow, L"Mayına bastın! Oyun bitti.", L"Game Over", MB_OK | MB_ICONERROR);
        for (int i = 0; i < ROWS; ++i)
            for (int j = 0; j < COLS; ++j)
                EnableWindow(buttons[i][j], FALSE);
        return;
    }

    int n = neigh[r][c];
    set_button_text_number(btn, n);

    if (n == 0) {
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc) {
                if (dr == 0 && dc == 0) continue;
                int rr = r + dr, cc = c + dc;
                if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS)
                    if (!revealed[rr][cc] && !mines[rr][cc] && !flagged[rr][cc])
                        reveal_cell(rr, cc);
            }
    }

    check_win();
}

// Subclassed button proc to catch right-clicks on the button itself
LRESULT CALLBACK ButtonProc(HWND hwndBtn, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_RBUTTONUP) {
        int id = GetDlgCtrlID(hwndBtn);
        if (id >= BASE_ID && id < BASE_ID + ROWS * COLS) {
            int r, c;
            idx_from_id(id, &r, &c);
            if (!revealed[r][c]) {
                flagged[r][c] = !flagged[r][c];
                if (flagged[r][c]) SetWindowTextW(buttons[r][c], L"F");
                else SetWindowTextW(buttons[r][c], L"");
                InvalidateRect(buttons[r][c], NULL, TRUE);
                UpdateWindow(buttons[r][c]);
            }
        }
        return 0; // handled
    }
    // For everything else, call original proc
    int id = GetDlgCtrlID(hwndBtn);
    if (id >= BASE_ID && id < BASE_ID + ROWS * COLS) {
        int r, c;
        idx_from_id(id, &r, &c);
        return CallWindowProcW(oldButtonProc[r][c], hwndBtn, msg, wParam, lParam);
    }
    return DefWindowProcW(hwndBtn, msg, wParam, lParam);
}

// main window proc
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= BASE_ID && id < BASE_ID + ROWS * COLS) {
            int r, c;
            idx_from_id(id, &r, &c);
            if (!flagged[r][c] && !revealed[r][c]) reveal_cell(r, c);
        }
        break;
    }
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (!dis) break;
        int id = (int)((intptr_t)dis->CtlID);
        int idx = id - BASE_ID;
        if (idx < 0 || idx >= ROWS*COLS) return DefWindowProcW(hwnd, msg, wParam, lParam);
        int r = idx / COLS;
        int c = idx % COLS;

        HBRUSH brush = CreateSolidBrush(revealed[r][c] ? RGB(180,180,180) : RGB(220,220,220));
        FillRect(dis->hDC, &dis->rcItem, brush);
        DeleteObject(brush);

        if (dis->itemState & ODS_SELECTED) {
            DrawEdge(dis->hDC, &dis->rcItem, BDR_SUNKENINNER, BF_RECT);
        } else {
            DrawEdge(dis->hDC, &dis->rcItem, BDR_RAISEDINNER, BF_RECT);
        }

        HFONT hFont = (HFONT)SendMessageW(dis->hwndItem, WM_GETFONT, 0, 0);
        HFONT oldFont = (HFONT)SelectObject(dis->hDC, hFont);

        wchar_t buf[16] = L"";
        GetWindowTextW(dis->hwndItem, buf, (int)(sizeof(buf)/sizeof(wchar_t)));
        SetBkMode(dis->hDC, TRANSPARENT);

        COLORREF txtColor;
        if (flagged[r][c] && !revealed[r][c]) txtColor = RGB(200,0,0);
        else txtColor = (dis->itemState & ODS_DISABLED) ? RGB(100,100,100) : RGB(0,0,0);

        COLORREF oldColor = SetTextColor(dis->hDC, txtColor);
        DrawTextW(dis->hDC, buf, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(dis->hDC, oldColor);

        SelectObject(dis->hDC, oldFont);
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, PWSTR pCmd, int nCmdShow) {
    srand((unsigned) time(NULL));

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MSimpleClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    int winWidth = COLS * CELL_SIZE + 16;
    int winHeight = ROWS * CELL_SIZE + 40;

    mainWindow = CreateWindowW(L"MSimpleClass", L"Mines",
                               WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
                               CW_USEDEFAULT, CW_USEDEFAULT, winWidth, winHeight,
                               NULL, NULL, hInstance, NULL);
    if (!mainWindow) return 0;

    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            int x = c * CELL_SIZE + 8;
            int y = r * CELL_SIZE + 8;
            int id = BASE_ID + r * COLS + c;
            buttons[r][c] = CreateWindowW(L"BUTTON", L"",
                                          WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
                                          x, y, CELL_SIZE - 4, CELL_SIZE - 4,
                                          mainWindow, (HMENU)(intptr_t)id, hInstance, NULL);
            // store original wndproc and subclass
            oldButtonProc[r][c] = (WNDPROC)SetWindowLongPtrW(buttons[r][c], GWLP_WNDPROC, (LONG_PTR)ButtonProc);
            // set default font
            SendMessageW(buttons[r][c], WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        }
    }

    init_game();
    ShowWindow(mainWindow, nCmdShow);
    UpdateWindow(mainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
