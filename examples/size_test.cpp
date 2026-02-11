/**
 * @file size_test.c
 * @brief Full-screen terminal size test
 *
 * Detects terminal dimensions using the same mechanism as termina
 * (GetConsoleScreenBufferInfo on CONOUT$), then fills the entire
 * screen with a visible border to make it obvious whether the
 * detected size matches the actual console window.
 *
 * Also checks COLUMNS/LINES environment variables for comparison.
 *
 * Usage:
 *   size_test.exe            (run directly or through WinPatina)
 */

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Detect size the way termina does: open CONOUT$ directly */
static int detect_via_conout(int *cols, int *rows)
{
    HANDLE h = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;

    CONSOLE_SCREEN_BUFFER_INFO info;
    int ok = GetConsoleScreenBufferInfo(h, &info);
    CloseHandle(h);

    if (ok) {
        *cols = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
    return ok;
}

/* Detect via GetStdHandle(STD_OUTPUT_HANDLE) — fails when stdout is a pipe */
static int detect_via_stdout(int *cols, int *rows)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return 0;

    CONSOLE_SCREEN_BUFFER_INFO info;
    if (!GetConsoleScreenBufferInfo(h, &info)) return 0;

    *cols = info.srWindow.Right - info.srWindow.Left + 1;
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    return 1;
}

/* Detect from COLUMNS/LINES environment variables */
static int detect_via_env(int *cols, int *rows)
{
    const char *c = getenv("COLUMNS");
    const char *l = getenv("LINES");
    if (!c || !l) return 0;

    *cols = atoi(c);
    *rows = atoi(l);
    return (*cols > 0 && *rows > 0);
}

int main(void)
{
    /*
     * Draw using VT sequences so this works both natively and
     * through WinPatina's translation layer.
     */

    /* Switch to alternate screen first, then detect size.
     * The alternate buffer may have different dimensions to
     * the main buffer (e.g. main buffer has scrollback rows). */
    printf("\x1b[?1049h");
    printf("\x1b[?25l");
    fflush(stdout);

    int conout_cols = 0, conout_rows = 0;
    int stdout_cols = 0, stdout_rows = 0;
    int env_cols = 0, env_rows = 0;

    int have_conout = detect_via_conout(&conout_cols, &conout_rows);
    int have_stdout = detect_via_stdout(&stdout_cols, &stdout_rows);
    int have_env    = detect_via_env(&env_cols, &env_rows);

    /* Pick the best available size */
    int w, h;
    const char *source;
    if (have_conout) {
        w = conout_cols; h = conout_rows; source = "CONOUT$";
    } else if (have_stdout) {
        w = stdout_cols; h = stdout_rows; source = "stdout";
    } else if (have_env) {
        w = env_cols; h = env_rows; source = "env";
    } else {
        w = 80; h = 24; source = "fallback";
    }

    /* Clear screen */
    printf("\x1b[2J");

    /* Draw border: top row */
    printf("\x1b[1;1H");
    printf("\x1b[46;30m");  /* Cyan background, black text */
    putchar('+');
    for (int x = 1; x < w - 1; x++) putchar('-');
    if (w > 1) putchar('+');

    /* Side borders */
    for (int y = 1; y < h - 1; y++) {
        printf("\x1b[%d;1H|", y + 1);
        printf("\x1b[%d;%dH|", y + 1, w);
    }

    /* Bottom row */
    if (h > 1) {
        printf("\x1b[%d;1H", h);
        putchar('+');
        for (int x = 1; x < w - 1; x++) putchar('-');
        if (w > 1) putchar('+');
    }

    printf("\x1b[0m");  /* Reset colours */

    /* Centre the info block */
    int info_y = h / 2 - 4;
    if (info_y < 2) info_y = 2;

    printf("\x1b[%d;4H\x1b[1m--- Terminal Size Test ---\x1b[0m", info_y);

    printf("\x1b[%d;4HUsing: %s  =>  %d x %d", info_y + 2, source, w, h);

    printf("\x1b[%d;4HCONOUT$:  %s", info_y + 4,
           have_conout ? "" : "(failed)");
    if (have_conout) printf("%d x %d", conout_cols, conout_rows);

    printf("\x1b[%d;4Hstdout:   %s", info_y + 5,
           have_stdout ? "" : "(failed - pipe?)");
    if (have_stdout) printf("%d x %d", stdout_cols, stdout_rows);

    printf("\x1b[%d;4Henv:      %s", info_y + 6,
           have_env ? "" : "(not set)");
    if (have_env) printf("%d x %d (COLUMNS=%s LINES=%s)",
                         env_cols, env_rows,
                         getenv("COLUMNS"), getenv("LINES"));

    printf("\x1b[%d;4H\x1b[33mIf the border fills the whole window, "
           "the size is correct.\x1b[0m", info_y + 8);

    printf("\x1b[%d;4HPress any key to exit...", info_y + 10);

    fflush(stdout);

    /* Wait for a keypress (raw console input) */
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        /* Try to read a key via console input */
        HANDLE hConIn = CreateFileW(L"CONIN$", GENERIC_READ,
                                    FILE_SHARE_READ, NULL,
                                    OPEN_EXISTING, 0, NULL);
        if (hConIn != INVALID_HANDLE_VALUE) {
            DWORD old_mode;
            GetConsoleMode(hConIn, &old_mode);
            SetConsoleMode(hConIn, 0);

            INPUT_RECORD ir;
            DWORD read;
            while (ReadConsoleInput(hConIn, &ir, 1, &read)) {
                if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown)
                    break;
            }

            SetConsoleMode(hConIn, old_mode);
            CloseHandle(hConIn);
        }
    }

    /* Restore: show cursor, leave alternate screen */
    printf("\x1b[?25h");
    printf("\x1b[?1049l");
    fflush(stdout);

    return 0;
}
