/**
 * @file winpatina_main.cpp
 * @brief Core WinPatina implementation
 *
 * Implements the main public API: initialisation, destruction, and queries.
 */

#include "internal.h"
#include <cstdio>    /* snprintf */
#include <cstdlib>   /* malloc, free */
#include <cstring>   /* strncpy, strlen */

/*============================================================================
 * Error Handling
 *
 * Thread-local storage for error messages.
 *============================================================================*/

#define WP_ERROR_BUFFER_SIZE 512

/* Thread-local error buffer */
/* Use thread_local (C++11) which works on both MSVC and GCC/MinGW */
static thread_local char tls_error_buffer[WP_ERROR_BUFFER_SIZE];
static thread_local bool tls_error_set = false;

void wp_set_error(const char* message)
{
    if (message == NULL) {
        tls_error_buffer[0] = '\0';
        tls_error_set = false;
        return;
    }

    strncpy(tls_error_buffer, message, WP_ERROR_BUFFER_SIZE - 1);
    tls_error_buffer[WP_ERROR_BUFFER_SIZE - 1] = '\0';
    tls_error_set = true;
}

void wp_set_error_win32(const char* message, DWORD error_code)
{
    char win32_message[256] = {0};

    /* Get the Windows error message */
    FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        win32_message,
        sizeof(win32_message) - 1,
        NULL
    );

    /* Remove trailing newline if present */
    size_t len = strlen(win32_message);
    while (len > 0 && (win32_message[len - 1] == '\n' || win32_message[len - 1] == '\r')) {
        win32_message[--len] = '\0';
    }

    /* Format: "message: win32_message (error N)" */
    snprintf(tls_error_buffer, WP_ERROR_BUFFER_SIZE,
             "%s: %s (error %lu)",
             message ? message : "Error",
             win32_message,
             (unsigned long)error_code);

    tls_error_set = true;
}

const char* wp_get_error(void)
{
    if (!tls_error_set) {
        return NULL;
    }
    return tls_error_buffer;
}

/*============================================================================
 * Version Functions
 *============================================================================*/

int wp_version(void)
{
    return (WINPATINA_VERSION_MAJOR * 10000) +
           (WINPATINA_VERSION_MINOR * 100) +
           WINPATINA_VERSION_PATCH;
}

const char* wp_version_string(void)
{
    /* Static buffer - constructed once on first call */
    static char version_str[32] = {0};

    if (version_str[0] == '\0') {
        snprintf(version_str, sizeof(version_str), "%d.%d.%d",
                 WINPATINA_VERSION_MAJOR,
                 WINPATINA_VERSION_MINOR,
                 WINPATINA_VERSION_PATCH);
    }

    return version_str;
}

/*============================================================================
 * Lifecycle Functions
 *============================================================================*/

WinPatina* wp_init(const WinPatinaConfig* config)
{
    /* Clear any previous error */
    wp_set_error(NULL);

    /* Allocate the main structure */
    WinPatina* wp = (WinPatina*)malloc(sizeof(WinPatina));
    if (wp == NULL) {
        wp_set_error("Failed to allocate memory for WinPatina");
        return NULL;
    }

    /* Zero-initialise */
    memset(wp, 0, sizeof(*wp));

    /* Copy configuration (or use defaults if NULL) */
    if (config != NULL) {
        wp->config = *config;
    } else {
        /* Default configuration */
        memset(&wp->config, 0, sizeof(wp->config));
        wp->config.enable_utf8 = true;
    }

    /* Get console handles */
    wp->hConsoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    wp->hConsoleInput = GetStdHandle(STD_INPUT_HANDLE);

    if (wp->hConsoleOutput == INVALID_HANDLE_VALUE) {
        wp_set_error("Failed to get console output handle");
        free(wp);
        return NULL;
    }

    /* Save original console modes (to restore on cleanup) */
    if (!GetConsoleMode(wp->hConsoleOutput, &wp->original_output_mode)) {
        wp_set_error_win32("Failed to get console output mode", GetLastError());
        free(wp);
        return NULL;
    }

    if (wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        GetConsoleMode(wp->hConsoleInput, &wp->original_input_mode);
    }

    /* Save original codepages */
    wp->original_output_cp = GetConsoleOutputCP();
    wp->original_input_cp = GetConsoleCP();

    /* Run capability detection */
    if (!wp_detect_capabilities(&wp->caps, &wp->config)) {
        /* Error already set by wp_detect_capabilities */
        free(wp);
        return NULL;
    }

    /* Set UTF-8 codepage if requested and available */
    if (wp->config.enable_utf8 && (wp->caps.flags & WP_CAP_UTF8_CODEPAGE)) {
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }

    /*
     * Set up the translation pipeline if we're in Win32 translation mode.
     * In passthrough mode the pipeline is not needed — VT sequences
     * go straight to the terminal.
     */
    if (wp->caps.mode == WP_MODE_WIN32_TRANSLATION ||
        wp->caps.mode == WP_MODE_HYBRID) {

        WORD default_attrs = 0x07;  /* White on black fallback */

        /* Use the console's actual default attributes if available */
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(wp->hConsoleOutput, &csbi)) {
            default_attrs = csbi.wAttributes;
        }

        bool has_lvb = (wp->caps.flags & WP_CAP_UNDERSCORE) != 0;

        /* Screen buffer */
        wp->screen = wp_screen_create(
            wp->caps.screen_width, wp->caps.screen_height, default_attrs);
        if (wp->screen == NULL) {
            wp_set_error("Failed to create screen buffer");
            free(wp);
            return NULL;
        }

        /* Dispatch (parser callbacks -> screen operations) */
        wp_dispatch_init(&wp->dispatch, wp->screen, default_attrs, has_lvb,
                         &wp->input, wp->hConsoleInput,
                         &wp->local_echo, &wp->line_len, &wp->line_cols);

        /* VT parser */
        wp_vt_parser_init(&wp->parser, NULL);
        wp_dispatch_attach(&wp->dispatch, &wp->parser);

        /* Renderer */
        if (!wp_renderer_init(&wp->renderer, wp->hConsoleOutput, wp->screen)) {
            wp_set_error("Failed to initialise renderer");
            wp_screen_destroy(wp->screen);
            wp->screen = NULL;
            free(wp);
            return NULL;
        }

        /* Input handler */
        wp_input_init(&wp->input);

        /* Process manager (initialised but not spawned yet) */
        wp_process_init(&wp->process);

        wp->pipeline_ready = true;
    }

    return wp;
}

void wp_destroy(WinPatina* wp)
{
    if (wp == NULL) {
        return;
    }

    /* Tear down the pipeline */
    if (wp->pipeline_ready) {
        /* Terminate child if still running */
        if (wp_process_is_running(&wp->process)) {
            wp_process_terminate(&wp->process, 1);
        }
        wp_process_destroy(&wp->process);

        wp_renderer_destroy(&wp->renderer);

        if (wp->screen != NULL) {
            wp_screen_destroy(wp->screen);
            wp->screen = NULL;
        }

        wp->pipeline_ready = false;
    }

    /* Free command history */
    for (int i = 0; i < WP_HISTORY_MAX; i++) {
        if (wp->history[i] != NULL) {
            free(wp->history[i]);
            wp->history[i] = NULL;
        }
    }

    /* Restore the original buffer dimensions (saved in post_spawn_setup) */
    if (wp->saved_buffer_size.X > 0 && wp->saved_buffer_size.Y > 0) {
        HANDLE hOut = wp->hConsoleOutput;

        /* Shrink window → resize buffer → expand window */
        SMALL_RECT small_win = {0, 0, 0, 0};
        SetConsoleWindowInfo(hOut, TRUE, &small_win);
        SetConsoleScreenBufferSize(hOut, wp->saved_buffer_size);
        SetConsoleWindowInfo(hOut, TRUE, &wp->saved_window);
    }

    /* Restore original console output mode */
    if (wp->hConsoleOutput != NULL && wp->hConsoleOutput != INVALID_HANDLE_VALUE) {
        SetConsoleMode(wp->hConsoleOutput, wp->original_output_mode);
    }

    /* Restore original console input mode */
    if (wp->hConsoleInput != NULL && wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        SetConsoleMode(wp->hConsoleInput, wp->original_input_mode);
    }

    /* Restore original codepages */
    if (wp->original_output_cp != 0) {
        SetConsoleOutputCP(wp->original_output_cp);
    }
    if (wp->original_input_cp != 0) {
        SetConsoleCP(wp->original_input_cp);
    }

    /* Re-enable default Ctrl+C handling for the parent process */
    SetConsoleCtrlHandler(NULL, FALSE);

    /* Free the structure */
    free(wp);
}

/*============================================================================
 * Query Functions
 *============================================================================*/

WinPatinaMode wp_get_mode(WinPatina* wp)
{
    if (wp == NULL) {
        return WP_MODE_WIN32_TRANSLATION;  /* Safe default */
    }
    return wp->caps.mode;
}

WinPatinaWindowsVersion wp_get_windows_version(WinPatina* wp)
{
    if (wp == NULL) {
        return WP_WIN_2000;  /* Safe default */
    }
    return wp->caps.os_version;
}

WinPatinaTerminalType wp_get_terminal_type(WinPatina* wp)
{
    if (wp == NULL) {
        return WP_TERM_UNKNOWN;
    }
    return wp->caps.terminal;
}

uint32_t wp_get_capabilities(WinPatina* wp)
{
    if (wp == NULL) {
        return 0;
    }
    return wp->caps.flags;
}

void wp_get_screen_size(WinPatina* wp, int* width, int* height)
{
    if (wp == NULL) {
        if (width != NULL) *width = 80;
        if (height != NULL) *height = 25;
        return;
    }

    if (width != NULL) {
        *width = wp->caps.screen_width;
    }
    if (height != NULL) {
        *height = wp->caps.screen_height;
    }
}

/*============================================================================
 * Write-Back Callback
 *
 * Used by the dispatch handler for DSR responses — writes bytes back
 * to the child's stdin pipe.
 *============================================================================*/

static void write_back_to_child(void* wb_data, const uint8_t* data, size_t len)
{
    WPProcess* proc = (WPProcess*)wb_data;
    if (proc == NULL || data == NULL || len == 0) return;
    wp_process_write(proc, data, (int)len);
}

/**
 * Write keyboard input bytes to the child with CR→CRLF translation.
 *
 * Windows console programs reading from pipes expect CRLF line endings,
 * but the VT convention is to send just CR for the Enter key.  This
 * helper inserts an LF after every CR so that cmd.exe (and similar)
 * properly recognise line endings from piped input.
 */
static void write_to_child(WPProcess* process, const uint8_t* buf, int len)
{
    int start = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == 0x0D) {
            /* Write bytes up to and including CR */
            wp_process_write(process, buf + start, i + 1 - start);
            /* Append LF */
            uint8_t lf = 0x0A;
            wp_process_write(process, &lf, 1);
            start = i + 1;
        }
    }
    if (start < len) {
        wp_process_write(process, buf + start, len - start);
    }
}

/*============================================================================
 * Local Echo / Line Discipline
 *
 * When local_echo is true, typed characters are buffered and echoed
 * to the screen in real time.  On Enter the complete line is sent to
 * the child.  If echo_suppress is also true, an '@' prefix is prepended
 * so that cmd.exe does not re-echo the command.
 *============================================================================*/

/** Encode a Unicode codepoint as UTF-8.  Returns bytes written (1-4). */
static int encode_utf8_echo(uint8_t* buf, uint32_t cp)
{
    if (cp < 0x80) {
        buf[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        buf[0] = (uint8_t)(0xC0 | (cp >> 6));
        buf[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        buf[0] = (uint8_t)(0xE0 | (cp >> 12));
        buf[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        buf[0] = (uint8_t)(0xF0 | (cp >> 18));
        buf[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/**
 * Replace the current line buffer and its on-screen representation.
 *
 * Erases the visible text by emitting backspace-space-backspace for
 * each display column, then writes the new text.
 */
static void replace_line(WinPatina* wp, const char* text, int len)
{
    /* Erase current visible text */
    for (int i = 0; i < wp->line_cols; i++) {
        uint8_t bs_seq[3] = {0x08, 0x20, 0x08};
        wp_vt_parser_feed(&wp->parser, bs_seq, 3);
    }

    /* Load new text into line buffer */
    if (len > (int)sizeof(wp->line_buf) - 1)
        len = (int)sizeof(wp->line_buf) - 1;
    memcpy(wp->line_buf, text, len);
    wp->line_len = len;

    /* Count display columns (one per codepoint — good enough for ASCII) */
    wp->line_cols = 0;
    for (int i = 0; i < len; ) {
        uint8_t b = (uint8_t)text[i];
        if (b < 0x80)      i += 1;
        else if (b < 0xE0) i += 2;
        else if (b < 0xF0) i += 3;
        else                i += 4;
        wp->line_cols++;
    }

    /* Echo new text to screen */
    if (len > 0) {
        wp_vt_parser_feed(&wp->parser, (const uint8_t*)text, len);
    }
}

/**
 * Save a command to the history ring buffer.
 */
static void history_push(WinPatina* wp, const uint8_t* buf, int len)
{
    if (len <= 0) return;

    int slot = wp->history_count % WP_HISTORY_MAX;

    /* Free old entry if the ring has wrapped */
    if (wp->history[slot] != NULL) {
        free(wp->history[slot]);
    }

    wp->history[slot] = (char*)malloc(len + 1);
    if (wp->history[slot] != NULL) {
        memcpy(wp->history[slot], buf, len);
        wp->history[slot][len] = '\0';
    }

    wp->history_count++;
    wp->history_pos = wp->history_count;
}

/**
 * Check if the line buffer (trimmed, case-insensitive) matches a command.
 */
static bool line_matches_command(const uint8_t* buf, int len, const char* cmd)
{
    /* Trim leading spaces */
    int start = 0;
    while (start < len && buf[start] == ' ') start++;

    /* Trim trailing spaces */
    int end = len;
    while (end > start && buf[end - 1] == ' ') end--;

    int trimmed_len = end - start;
    int cmd_len = (int)strlen(cmd);
    if (trimmed_len != cmd_len) return false;

    for (int i = 0; i < cmd_len; i++) {
        char c = (char)buf[start + i];
        /* Lowercase for case-insensitive compare */
        if (c >= 'A' && c <= 'Z') c += 32;
        if (c != cmd[i]) return false;
    }
    return true;
}

/**
 * Handle a key event in local-echo / line-buffered mode.
 *
 * Printable characters are added to the line buffer and echoed to the
 * screen.  Backspace removes the last character.  Enter sends the
 * buffered line to the child and echoes a newline; the child's echo
 * of the command is then silently consumed.
 *
 * Up/Down arrows navigate command history.
 */
static void handle_local_echo_key(WinPatina* wp,
                                   const KEY_EVENT_RECORD* event)
{
    if (!event->bKeyDown) return;

    WCHAR uc = event->uChar.UnicodeChar;

    /* Printable character */
    if (uc >= 0x20 && uc != 0x7F) {
        uint8_t utf8[4];
        int n = encode_utf8_echo(utf8, (uint32_t)uc);
        if (n > 0 && wp->line_len + n < (int)sizeof(wp->line_buf)) {
            memcpy(wp->line_buf + wp->line_len, utf8, n);
            wp->line_len += n;
            wp->line_cols++;
            wp_vt_parser_feed(&wp->parser, utf8, n);
        }
        return;
    }

    /* Enter — send the buffered line to the child */
    if (uc == 0x0D) {
        bool had_typed_input = (wp->line_len > 0);

        /* Save to history (before clearing line_buf) */
        history_push(wp, wp->line_buf, wp->line_len);

        /*
         * Intercept "cls" — cmd.exe's cls uses direct console APIs
         * that bypass our pipes, so it would do nothing useful.
         * Clear our screen buffer instead, send just a bare CRLF
         * to trigger a fresh prompt, and skip the echo of that
         * empty line.
         */
        /*
         * Intercept "exit" — close the child's stdin pipe so it
         * exits cleanly, same as Ctrl+D on an empty line.
         */
        if (line_matches_command(wp->line_buf, wp->line_len, "exit")) {
            wp->line_len = 0;
            wp->line_cols = 0;
            wp_process_close_stdin(&wp->process);
            return;
        }

        if (line_matches_command(wp->line_buf, wp->line_len, "cls")) {
            const uint8_t clear_seq[] = {
                0x1b, '[', '2', 'J',   /* ESC[2J — erase entire display */
                0x1b, '[', 'H'         /* ESC[H  — cursor home */
            };
            wp_vt_parser_feed(&wp->parser, clear_seq, sizeof(clear_seq));

            /* Send bare CRLF so cmd.exe outputs a fresh prompt */
            uint8_t crlf[2] = {0x0D, 0x0A};
            wp_process_write(&wp->process, crlf, 2);

            wp->line_len = 0;
            wp->line_cols = 0;
            wp->skipping_echo = true;
            return;
        }

        if (wp->line_len > 0) {
            wp_process_write(&wp->process,
                             wp->line_buf, wp->line_len);
        }
        uint8_t crlf[2] = {0x0D, 0x0A};
        wp_process_write(&wp->process, crlf, 2);
        wp->line_len = 0;
        wp->line_cols = 0;

        /* Echo newline to screen */
        wp_vt_parser_feed(&wp->parser, crlf, 2);

        /*
         * cmd.exe will echo the prompt + command back through the
         * pipe.  We already showed the text via local echo, so skip
         * the next line of child output to avoid duplication.
         */
        wp->skipping_echo = had_typed_input;

        /*
         * Switch to raw input mode while the command runs.
         * The child process (especially TUI apps like vim, python,
         * etc.) needs raw keystrokes rather than line-buffered input.
         * Local echo is restored automatically when the command
         * finishes and the shell prompt returns (detected by an
         * idle timeout in wp_poll).
         */
        if (had_typed_input) {
            wp->local_echo = false;
            wp->command_running = true;
            wp->last_child_output_tick = GetTickCount();
        }
        return;
    }

    /* Backspace — erase the last UTF-8 character from the buffer */
    if (uc == 0x08 || uc == 0x7F) {
        if (wp->line_len > 0) {
            /* Walk back past UTF-8 continuation bytes (10xxxxxx) */
            int pos = wp->line_len - 1;
            while (pos > 0 && (wp->line_buf[pos] & 0xC0) == 0x80) {
                pos--;
            }
            wp->line_len = pos;
            if (wp->line_cols > 0) wp->line_cols--;

            /* Erase on screen: BS  Space  BS */
            uint8_t bs_seq[3] = {0x08, 0x20, 0x08};
            wp_vt_parser_feed(&wp->parser, bs_seq, 3);
        }
        return;
    }

    /* Tab */
    if (uc == 0x09) {
        if (wp->line_len + 1 < (int)sizeof(wp->line_buf)) {
            wp->line_buf[wp->line_len++] = 0x09;
            uint8_t tab = 0x09;
            wp_vt_parser_feed(&wp->parser, &tab, 1);
        }
        return;
    }

    /* Ctrl+C — deliver a real console control event to the child */
    if (uc == 0x03) {
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        wp->line_len = 0;
        wp->line_cols = 0;
        return;
    }

    /* Ctrl+D — close child stdin (EOF), causing it to exit */
    if (uc == 0x04) {
        if (wp->line_len == 0) {
            wp_process_close_stdin(&wp->process);
        }
        return;
    }

    /* Up arrow — recall previous command from history */
    WORD vk = event->wVirtualKeyCode;
    if (vk == VK_UP) {
        if (wp->history_count == 0 || wp->history_pos <= 0) return;
        wp->history_pos--;
        int slot = wp->history_pos % WP_HISTORY_MAX;
        if (wp->history[slot] != NULL) {
            replace_line(wp, wp->history[slot],
                         (int)strlen(wp->history[slot]));
        }
        return;
    }

    /* Down arrow — recall next command or clear line */
    if (vk == VK_DOWN) {
        if (wp->history_pos >= wp->history_count) return;
        wp->history_pos++;
        if (wp->history_pos >= wp->history_count) {
            /* Past newest entry — show empty line */
            replace_line(wp, "", 0);
        } else {
            int slot = wp->history_pos % WP_HISTORY_MAX;
            if (wp->history[slot] != NULL) {
                replace_line(wp, wp->history[slot],
                             (int)strlen(wp->history[slot]));
            }
        }
        return;
    }

    /* Everything else (F-keys, modifier-only) — ignored */
}

/*============================================================================
 * Process Management
 *============================================================================*/

/**
 * Common setup after a child process has been spawned.
 *
 * Creates a dedicated console screen buffer for rendering (like the VT
 * "alternate screen"), wires up DSR write-back, and puts the console
 * input into raw event mode.
 *
 * The fresh screen buffer is essential because the existing console
 * buffer may have been scrolled down by previous shell activity.
 * WriteConsoleOutputW writes at buffer coordinates (row 0, 1, 2 ...)
 * which would be off-screen in a scrolled buffer.  A new buffer starts
 * at the top, so our output is always visible.
 */
static int post_spawn_setup(WinPatina* wp)
{
    /* DSR write-back requires a writable stdin pipe. */
    if (wp_process_stdin_is_pipe(&wp->process)) {
        wp_dispatch_set_write_back(&wp->dispatch, write_back_to_child,
                                   &wp->process);
    } else {
        wp_dispatch_set_write_back(&wp->dispatch, NULL, NULL);
    }

    /*
     * Prepare the console buffer for rendering.
     *
     * Previous approach: create a dedicated buffer via
     * CreateConsoleScreenBuffer.  This doesn't work with conpty
     * (Windows Terminal) because conpty only monitors the original
     * buffer — changes to a secondary buffer are silently lost.
     *
     * New approach: resize the original buffer to match our screen
     * dimensions (eliminating scrollback), scroll to the top, and
     * clear.  This keeps conpty monitoring the same buffer.
     */
    {
        HANDLE hOut = wp->hConsoleOutput;

        /* Save the original buffer size so we can restore it on exit */
        WORD clear_attrs = 0x07;  /* White on black fallback */
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
            wp->saved_buffer_size = csbi.dwSize;
            wp->saved_window = csbi.srWindow;
            clear_attrs = csbi.wAttributes;
        }

        COORD buf_size;
        buf_size.X = (SHORT)wp->caps.screen_width;
        buf_size.Y = (SHORT)wp->caps.screen_height;

        /*
         * Resize: shrink window → set buffer size → expand window.
         * The buffer must always be >= the window.
         */
        SMALL_RECT small_win = {0, 0, 0, 0};
        SetConsoleWindowInfo(hOut, TRUE, &small_win);
        SetConsoleScreenBufferSize(hOut, buf_size);

        SMALL_RECT full_win;
        full_win.Left   = 0;
        full_win.Top    = 0;
        full_win.Right  = buf_size.X - 1;
        full_win.Bottom = buf_size.Y - 1;
        SetConsoleWindowInfo(hOut, TRUE, &full_win);

        /* Clear the buffer so we start fresh */
        COORD origin = {0, 0};
        DWORD total = (DWORD)buf_size.X * (DWORD)buf_size.Y;
        DWORD written = 0;
        FillConsoleOutputCharacterW(hOut, L' ', total, origin, &written);
        FillConsoleOutputAttribute(hOut, clear_attrs,
                                   total, origin, &written);
        SetConsoleCursorPosition(hOut, origin);

        /*
         * Verify actual viewport dimensions and sync the screen model.
         *
         * We render the visible window, not the full scrollback buffer.
         * Using dwSize here can explode the model height on terminals with
         * large scrollback, causing sluggish or apparently frozen output.
         */
        CONSOLE_SCREEN_BUFFER_INFO post_csbi;
        if (GetConsoleScreenBufferInfo(hOut, &post_csbi)) {
            int actual_w = post_csbi.srWindow.Right - post_csbi.srWindow.Left + 1;
            int actual_h = post_csbi.srWindow.Bottom - post_csbi.srWindow.Top + 1;

            if (actual_w <= 0 || actual_h <= 0) {
                actual_w = post_csbi.dwSize.X;
                actual_h = post_csbi.dwSize.Y;
            }

            if (actual_w != wp->caps.screen_width ||
                actual_h != wp->caps.screen_height) {
                wp->caps.screen_width = actual_w;
                wp->caps.screen_height = actual_h;

                if (wp->screen != NULL) {
                    wp_screen_resize(wp->screen, actual_w, actual_h);
                    wp_renderer_resize(&wp->renderer);
                }
            }
        }

        /*
         * Enable console output modes:
         *  - LVB attributes (underline, overline, grid lines)
         *  - VT processing (so we can write VT clear sequences directly
         *    for full_repaint — conpty's diff algorithm doesn't reliably
         *    translate WriteConsoleOutputW space-fills into visual clears)
         *  - Disable newline auto-return (prevents \n → \r\n doubling)
         */
        {
            DWORD out_mode = 0;
            if (GetConsoleMode(hOut, &out_mode)) {
                DWORD applied_mode = out_mode;
                bool vt_enabled = false;

                if (wp->caps.flags & WP_CAP_VT_PROCESSING) {
                    /*
                     * Try VT + DNA-RETURN first, then VT-only fallback.
                     * Some consoles support VT but reject newer mode bits.
                     */
                    DWORD vt_mode = applied_mode
                                  | ENABLE_VIRTUAL_TERMINAL_PROCESSING
                                  | DISABLE_NEWLINE_AUTO_RETURN;
                    if (SetConsoleMode(hOut, vt_mode)) {
                        applied_mode = vt_mode;
                        vt_enabled = true;
                    } else {
                        vt_mode = applied_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                        if (SetConsoleMode(hOut, vt_mode)) {
                            applied_mode = vt_mode;
                            vt_enabled = true;
                        }
                    }
                }

                if (wp->caps.flags & WP_CAP_UNDERSCORE) {
                    DWORD lvb_mode = applied_mode | ENABLE_LVB_GRID_WORLDWIDE;
                    if (SetConsoleMode(hOut, lvb_mode)) {
                        applied_mode = lvb_mode;
                    } else {
                        /* Keep the best mode we already managed to apply. */
                        SetConsoleMode(hOut, applied_mode);
                    }
                }

                wp->renderer.vt_output_enabled = vt_enabled;
            }
        }

        /* Renderer already points at hConsoleOutput from wp_init */

        /* Force a full repaint so the clean buffer gets our content */
        wp_renderer_paint_all(&wp->renderer);
    }

    /*
     * Input mode:
     * - piped stdin: we consume ReadConsoleInput events and translate to VT
     * - console stdin: child reads input events directly; keep normal mode
     */
    if (wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        if (wp_process_stdin_is_pipe(&wp->process)) {
            /*
             * ENABLE_EXTENDED_FLAGS with QUICK_EDIT cleared ensures mouse
             * events are delivered to ReadConsoleInput instead of being
             * captured by selection mode.
             */
            DWORD mode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
            if (wp->config.enable_mouse) {
                mode |= ENABLE_MOUSE_INPUT;
            }
            SetConsoleMode(wp->hConsoleInput, mode);
        } else {
            /*
             * Child reads input directly. Preserve normal console behavior
             * but force Quick Edit off so mouse events aren't eaten by
             * selection mode in TUI applications.
             */
            DWORD mode = wp->original_input_mode | ENABLE_EXTENDED_FLAGS;
            mode &= ~(DWORD)ENABLE_QUICK_EDIT_MODE;
            SetConsoleMode(wp->hConsoleInput, mode);
        }
    }

    /*
     * Ctrl+C handling:
     * - piped stdin: ignore in parent, deliver manually to child
     * - console stdin: keep default handling so child receives Ctrl+C
     */
    if (wp_process_stdin_is_pipe(&wp->process)) {
        SetConsoleCtrlHandler(NULL, TRUE);
    } else {
        SetConsoleCtrlHandler(NULL, FALSE);
    }

    /*
     * Resume the child process. It was created suspended so the console
     * buffer could be resized before the child queries its dimensions.
     */
    wp_process_resume(&wp->process);

    return 0;
}

int wp_spawn(WinPatina* wp, const char* command, char* const argv[])
{
    if (wp == NULL || command == NULL) {
        wp_set_error("Invalid parameters for wp_spawn");
        return -1;
    }

    if (!wp->pipeline_ready) {
        wp_set_error("Pipeline not ready (passthrough mode?)");
        return -1;
    }

    /*
     * Build a command line from command + argv.
     * Windows expects a single command line string, not separate args.
     */
    char cmdline[4096];
    int pos = 0;

    /* Start with the command itself */
    pos += snprintf(cmdline + pos, sizeof(cmdline) - pos, "%s", command);

    /* Append arguments if provided */
    if (argv != NULL) {
        for (int i = 0; argv[i] != NULL; i++) {
            if (pos < (int)sizeof(cmdline) - 1) {
                pos += snprintf(cmdline + pos, sizeof(cmdline) - pos,
                                " %s", argv[i]);
            }
        }
    }

    if (!wp_process_spawn(&wp->process, cmdline,
                           wp->caps.screen_width,
                           wp->caps.screen_height, true)) {
        return -1;
    }

    return post_spawn_setup(wp);
}

int wp_spawn_shell(WinPatina* wp)
{
    if (wp == NULL) {
        wp_set_error("Invalid parameters for wp_spawn_shell");
        return -1;
    }

    if (!wp->pipeline_ready) {
        wp_set_error("Pipeline not ready (passthrough mode?)");
        return -1;
    }

    const char* comspec = getenv("COMSPEC");
    if (comspec == NULL || comspec[0] == '\0') {
        comspec = "cmd.exe";
    }

    if (!wp_process_spawn_ex(&wp->process, comspec,
                             wp->caps.screen_width,
                             wp->caps.screen_height,
                             false,  /* console stderr */
                             true)) { /* console stdin */
        return -1;
    }

    /*
     * With console stdin passthrough, cmd.exe performs its own line
     * editing/echo. Disable local line discipline in WinPatina.
     */
    wp->local_echo = false;
    wp->line_len = 0;
    wp->line_cols = 0;
    wp->skipping_echo = false;
    wp->command_running = false;

    int rc = post_spawn_setup(wp);
    if (rc != 0) return rc;

    /*
     * Paint a welcome bar: blue background, white text, padded to
     * the full screen width so the colour fills the entire row.
     */
    {
        char bar[512];
        int content_len = snprintf(bar, sizeof(bar),
            "\x1b" "[44;97m"   /* SGR: blue bg (44), bright white fg (97) */
            " WinPatina v%s - type exit to close this shell",
            wp_version_string());

        /* Pad with spaces to fill the row */
        int pad = wp->caps.screen_width - content_len
                  + 8;  /* +8 to account for \x1b[44;97m (8 non-visible bytes) */
        if (pad < 0) pad = 0;
        for (int i = 0; i < pad && content_len + i < (int)sizeof(bar) - 10; i++) {
            bar[content_len + i] = ' ';
        }
        content_len += pad;

        /* Reset attributes and newline */
        content_len += snprintf(bar + content_len,
                                sizeof(bar) - content_len,
                                "\x1b" "[0m\r\n");

        wp_vt_parser_feed(&wp->parser,
                          (const uint8_t*)bar, content_len);
        wp_renderer_paint(&wp->renderer);
    }

    return 0;
}

bool wp_is_running(WinPatina* wp)
{
    if (wp == NULL) return false;
    return wp_process_is_running(&wp->process);
}

int wp_get_exit_code(WinPatina* wp)
{
    if (wp == NULL) return -1;
    return wp_process_get_exit_code(&wp->process);
}

/*============================================================================
 * Main Loop
 *============================================================================*/

/** Read buffer for child output */
#define WP_READ_BUF_SIZE 4096

static void apply_console_resize_if_changed(WinPatina* wp, SHORT new_w, SHORT new_h)
{
    if (new_w <= 0 || new_h <= 0) return;

    /* Ignore stale resize notifications. */
    if (new_w == wp->caps.screen_width &&
        new_h == wp->caps.screen_height) {
        return;
    }

    wp->caps.screen_width = new_w;
    wp->caps.screen_height = new_h;

    if (wp->screen != NULL) {
        wp_screen_resize(wp->screen, new_w, new_h);
        wp_renderer_resize(&wp->renderer);
        wp_renderer_paint_all(&wp->renderer);
    }

    /* Keep child's stderr CSBI path in sync with viewport size. */
    wp_process_update_stderr_size(&wp->process, new_w, new_h);
}

int wp_poll(WinPatina* wp, int timeout_ms)
{
    if (wp == NULL) return -1;
    if (!wp->pipeline_ready) return -1;

    const bool capture_input = wp_process_stdin_is_pipe(&wp->process);
    DWORD effective_wait;
    if (timeout_ms < 0) {
        effective_wait = 16;
    } else if ((DWORD)timeout_ms > 16) {
        effective_wait = 16;
    } else {
        effective_wait = (DWORD)timeout_ms;
    }

    /*
     * Wait for console input events.
     *
     * We only wait on the console input handle.  Anonymous pipe handles
     * are NOT valid synchronisation objects for WaitForMultipleObjects
     * (the Win32 docs list console input, events, mutexes, semaphores,
     * processes, threads, and waitable timers — but not pipes).
     * Using a pipe handle causes undefined behaviour on many Windows
     * versions.  Instead we poll the child's stdout pipe separately
     * via PeekNamedPipe (inside wp_process_read) on each iteration.
     *
     * The wait is capped at 16 ms (~60 Hz) so that child output is
     * picked up promptly even when no console events arrive.
     */
    if (capture_input && wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        WaitForSingleObject(wp->hConsoleInput, effective_wait);
    } else if (!wp_process_is_running(&wp->process)) {
        return 1;  /* Child not running */
    } else if (effective_wait > 0) {
        Sleep(effective_wait);
    }

    /*
     * Process console input events (keyboard, mouse, resize).
     */
    if (capture_input && wp->hConsoleInput != INVALID_HANDLE_VALUE) {
        DWORD events_available = 0;
        GetNumberOfConsoleInputEvents(wp->hConsoleInput, &events_available);

        while (events_available > 0) {
            INPUT_RECORD ir;
            DWORD read_count = 0;
            if (!ReadConsoleInputW(wp->hConsoleInput, &ir, 1, &read_count))
                break;
            if (read_count == 0) break;
            events_available--;

            uint8_t vt_buf[WP_INPUT_BUF_MAX];
            int vt_len = 0;

            switch (ir.EventType) {
                case KEY_EVENT:
                    /*
                     * Ctrl+C: deliver a real CTRL_C_EVENT rather than
                     * writing 0x03 to the pipe.  This triggers signal
                     * handlers (e.g. Python's KeyboardInterrupt).
                     */
                    if (ir.Event.KeyEvent.bKeyDown &&
                        ir.Event.KeyEvent.uChar.UnicodeChar == 0x03) {
                        GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
                        break;
                    }

                    if (wp->local_echo) {
                        handle_local_echo_key(wp, &ir.Event.KeyEvent);
                    } else {
                        vt_len = wp_input_translate_key(&wp->input,
                                                         &ir.Event.KeyEvent,
                                                         vt_buf);
                    }
                    break;

                case MOUSE_EVENT:
                    vt_len = wp_input_translate_mouse(&wp->input,
                                                       &ir.Event.MouseEvent,
                                                       vt_buf);
                    break;

                case WINDOW_BUFFER_SIZE_EVENT: {
                    SHORT new_w = 0;
                    SHORT new_h = 0;

                    CONSOLE_SCREEN_BUFFER_INFO csbi;
                    if (GetConsoleScreenBufferInfo(wp->hConsoleOutput, &csbi)) {
                        new_w = (SHORT)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
                        new_h = (SHORT)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
                    } else {
                        new_w = ir.Event.WindowBufferSizeEvent.dwSize.X;
                        new_h = ir.Event.WindowBufferSizeEvent.dwSize.Y;
                    }

                    apply_console_resize_if_changed(wp, new_w, new_h);
                    break;
                }

                default:
                    break;
            }

            /* Send translated input to child (with CR→CRLF) */
            if (vt_len > 0) {
                write_to_child(&wp->process, vt_buf, vt_len);
            }
        }
    } else if (wp->hConsoleOutput != INVALID_HANDLE_VALUE) {
        /*
         * Child owns console input; poll viewport size directly so
         * renderer dimensions still track window resizes.
         */
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        if (GetConsoleScreenBufferInfo(wp->hConsoleOutput, &csbi)) {
            SHORT new_w = (SHORT)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
            SHORT new_h = (SHORT)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
            apply_console_resize_if_changed(wp, new_w, new_h);
        }
    }

    /*
     * Read child output and feed through the VT pipeline.
     *
     * When skipping_echo is set, we consume bytes until the end of
     * the line (\n).  This discards cmd.exe's echo of the command we
     * already displayed via local echo.
     */
    HANDLE child_stdout = wp_process_get_stdout_handle(&wp->process);
    if (child_stdout != NULL) {
        uint8_t read_buf[WP_READ_BUF_SIZE];
        for (;;) {
            int n = wp_process_read(&wp->process, read_buf, sizeof(read_buf));
            if (n <= 0) break;

            int offset = 0;

            /* Skip the echoed command line if needed */
            if (wp->skipping_echo) {
                while (offset < n) {
                    if (read_buf[offset] == '\n') {
                        offset++;   /* consume the LF */
                        wp->skipping_echo = false;
                        break;
                    }
                    offset++;
                }
            }

            /* Feed remaining bytes through parser */
            if (offset < n) {
                wp->last_child_output_tick = GetTickCount();
                wp_vt_parser_feed(&wp->parser,
                                  read_buf + offset, n - offset);
            }
        }
    }

    /*
     * Restore local echo when a command has finished.
     *
     * After Enter sends a command, local_echo is disabled so that
     * raw keystrokes reach the child (essential for TUI apps).
     * We restore local echo when ALL of:
     *   - A command was running (command_running is true)
     *   - The echo skip has completed
     *   - No TUI modes are active (not in alternate screen,
     *     no mouse tracking, cursor keys in normal mode)
     *   - The child has been idle for at least 200 ms
     *
     * This detects the return to a cmd.exe prompt after a command
     * finishes, even if the child (e.g., a TUI app using direct
     * console APIs) never sent DECSET sequences through the pipe.
     */
    if (wp->command_running && !wp->skipping_echo && !wp->local_echo) {
        bool tui_active = false;
        if (wp->screen != NULL && wp->screen->using_alternate)
            tui_active = true;
        if (wp->input.mouse_mode != WP_MOUSE_OFF)
            tui_active = true;
        if (wp->input.cursor_key_mode != WP_CURSOR_KEY_NORMAL)
            tui_active = true;

        if (!tui_active) {
            DWORD elapsed = GetTickCount() - wp->last_child_output_tick;
            if (elapsed >= 200) {
                wp->local_echo = true;
                wp->command_running = false;
            }
        }
    }

    /*
     * Render any changes to the console.
     */
    wp_renderer_paint(&wp->renderer);

    /*
     * Check if child has exited.
     */
    if (wp_process_poll(&wp->process)) {
        /* Drain any remaining output */
        if (child_stdout != NULL) {
            uint8_t read_buf[WP_READ_BUF_SIZE];
            for (;;) {
                int n = wp_process_read(&wp->process, read_buf, sizeof(read_buf));
                if (n <= 0) break;
                wp_vt_parser_feed(&wp->parser, read_buf, n);
            }
            wp_renderer_paint(&wp->renderer);
        }
        return 1;  /* Child exited */
    }

    return 0;  /* Still running */
}

int wp_run(WinPatina* wp)
{
    if (wp == NULL) return -1;

    int status;
    do {
        status = wp_poll(wp, 100);
    } while (status == 0);

    if (status < 0) return -1;
    return wp_get_exit_code(wp);
}

/*============================================================================
 * Direct I/O
 *============================================================================*/

int wp_write_child(WinPatina* wp, const char* data, int len)
{
    if (wp == NULL || data == NULL || len <= 0) return -1;
    return wp_process_write(&wp->process, (const uint8_t*)data, len);
}

void wp_refresh(WinPatina* wp)
{
    if (wp == NULL) return;
    if (!wp->pipeline_ready) return;
    wp_renderer_paint_all(&wp->renderer);
}

/*============================================================================
 * Terminal Control
 *============================================================================*/

void wp_set_title(WinPatina* wp, const char* title)
{
    if (wp == NULL || title == NULL) return;

    /* Convert UTF-8 title to wide string */
    int len = MultiByteToWideChar(CP_UTF8, 0, title, -1, NULL, 0);
    if (len <= 0) return;

    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide == NULL) return;

    MultiByteToWideChar(CP_UTF8, 0, title, -1, wide, len);
    SetConsoleTitleW(wide);
    free(wide);
}

void wp_set_mouse_enabled(WinPatina* wp, bool enabled)
{
    if (wp == NULL) return;

    if (enabled) {
        wp->input.mouse_mode = WP_MOUSE_NORMAL;
    } else {
        wp->input.mouse_mode = WP_MOUSE_OFF;
    }

    /* Update console input mode only when WinPatina is translating input. */
    if (wp->hConsoleInput != INVALID_HANDLE_VALUE &&
        (!wp_process_is_running(&wp->process) ||
         wp_process_stdin_is_pipe(&wp->process))) {
        DWORD mode = ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
        if (enabled) {
            mode |= ENABLE_MOUSE_INPUT;
        }
        SetConsoleMode(wp->hConsoleInput, mode);
    }
}
