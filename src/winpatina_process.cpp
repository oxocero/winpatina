/**
 * @file winpatina_process.cpp
 * @brief Child process management implementation
 *
 * Spawns a child process with stdout/stderr redirected through pipes
 * and stdin either piped or inherited from the console, providing
 * non-blocking read and blocking write primitives.
 *
 * Pipe architecture:
 *
 *   (piped stdin mode)
 *   [pipe_stdin_write] --write--> [pipe_stdin_read]  --> child stdin
 *   child stdout --> [pipe_stdout_write] --read--> [pipe_stdout_read]
 *   child stderr --> [pipe_stdout_write] (merged with stdout)
 *
 * The "read" and "write" in handle names refer to our side of the pipe.
 */

#include "winpatina_process.h"
#include "internal.h"
#include <cstdlib>  /* getenv, malloc, free */
#include <cstdio>   /* snprintf */
#include <cstring>  /* strlen, memset */

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/** Close a handle and set it to NULL. */
static void safe_close(HANDLE* h)
{
    if (h != NULL && *h != NULL && *h != INVALID_HANDLE_VALUE) {
        CloseHandle(*h);
        *h = NULL;
    }
}

/**
 * Create a pipe pair with the specified inheritance.
 *
 * @param read_handle   Receives the read end
 * @param write_handle  Receives the write end
 * @param inherit_read  If true, the read end is inheritable
 * @param inherit_write If true, the write end is inheritable
 * @return true on success
 */
static bool create_pipe_pair(HANDLE* read_handle, HANDLE* write_handle,
                              bool inherit_read, bool inherit_write)
{
    SECURITY_ATTRIBUTES sa;
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;  /* Pipes are created inheritable */

    HANDLE r = NULL, w = NULL;
    if (!CreatePipe(&r, &w, &sa, WP_PIPE_BUF_SIZE)) {
        return false;
    }

    /*
     * Remove inheritance from the handle we keep on our side.
     * The child should only inherit the other end.
     */
    if (!inherit_read) {
        SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
    }
    if (!inherit_write) {
        SetHandleInformation(w, HANDLE_FLAG_INHERIT, 0);
    }

    *read_handle = r;
    *write_handle = w;
    return true;
}

/**
 * Convert a narrow (UTF-8/ANSI) string to wide string for CreateProcessW.
 * Caller must free the returned pointer with free().
 * Returns NULL on failure.
 */
static WCHAR* to_wide(const char* str)
{
    if (str == NULL) return NULL;

    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    if (len <= 0) {
        /* Fall back to ANSI if UTF-8 fails */
        len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
        if (len <= 0) return NULL;

        WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
        if (wide == NULL) return NULL;
        MultiByteToWideChar(CP_ACP, 0, str, -1, wide, len);
        return wide;
    }

    WCHAR* wide = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (wide == NULL) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wide, len);
    return wide;
}

/**
 * Build a wide environment block with COLUMNS and LINES variables set.
 *
 * The Win32 environment block is a contiguous buffer of null-terminated
 * wide strings, terminated by an extra null (double-null at the end).
 * We copy the parent environment and upsert COLUMNS/LINES.
 *
 * Caller must free the returned pointer with free().
 * Returns NULL on failure (falls back to inheriting parent env).
 */
static WCHAR* build_env_block(int cols, int rows)
{
    if (cols <= 0 && rows <= 0) return NULL;

    /* Get the parent's environment block (wide) */
    WCHAR* parent_env = GetEnvironmentStringsW();
    if (parent_env == NULL) return NULL;

    /*
     * Measure the parent block and collect entries, skipping any
     * existing COLUMNS= or LINES= so we can replace them.
     */
    size_t parent_len = 0;   /* Total chars excluding replaced vars */
    const WCHAR* p = parent_env;
    while (*p != L'\0') {
        size_t entry_len = wcslen(p) + 1;  /* Including null terminator */

        bool skip = false;
        if (_wcsnicmp(p, L"COLUMNS=", 8) == 0 && cols > 0) skip = true;
        if (_wcsnicmp(p, L"LINES=", 6) == 0 && rows > 0)   skip = true;

        if (!skip) {
            parent_len += entry_len;
        }
        p += entry_len;
    }

    /* Format the new entries */
    WCHAR cols_entry[32] = {0};
    WCHAR rows_entry[32] = {0};
    size_t cols_len = 0;
    size_t rows_len = 0;

    if (cols > 0) {
        cols_len = (size_t)swprintf(cols_entry, 32, L"COLUMNS=%d", cols) + 1;
    }
    if (rows > 0) {
        rows_len = (size_t)swprintf(rows_entry, 32, L"LINES=%d", rows) + 1;
    }

    /* Allocate: parent entries + new entries + final null */
    size_t total = parent_len + cols_len + rows_len + 1;
    WCHAR* block = (WCHAR*)malloc(total * sizeof(WCHAR));
    if (block == NULL) {
        FreeEnvironmentStringsW(parent_env);
        return NULL;
    }

    /* Copy non-skipped parent entries */
    WCHAR* dst = block;
    p = parent_env;
    while (*p != L'\0') {
        size_t entry_len = wcslen(p) + 1;

        bool skip = false;
        if (_wcsnicmp(p, L"COLUMNS=", 8) == 0 && cols > 0) skip = true;
        if (_wcsnicmp(p, L"LINES=", 6) == 0 && rows > 0)   skip = true;

        if (!skip) {
            memcpy(dst, p, entry_len * sizeof(WCHAR));
            dst += entry_len;
        }
        p += entry_len;
    }

    /* Append our new entries */
    if (cols_len > 0) {
        memcpy(dst, cols_entry, cols_len * sizeof(WCHAR));
        dst += cols_len;
    }
    if (rows_len > 0) {
        memcpy(dst, rows_entry, rows_len * sizeof(WCHAR));
        dst += rows_len;
    }

    /* Double-null terminator */
    *dst = L'\0';

    FreeEnvironmentStringsW(parent_env);
    return block;
}

/*============================================================================
 * Public API - Lifecycle
 *============================================================================*/

void wp_process_init(WPProcess* proc)
{
    if (proc == NULL) return;
    memset(proc, 0, sizeof(*proc));
}

bool wp_process_spawn_ex(WPProcess* proc, const char* cmdline,
                         int cols, int rows, bool console_stderr,
                         bool use_console_stdin)
{
    if (proc == NULL || cmdline == NULL) {
        wp_set_error("Invalid parameters for process spawn");
        return false;
    }

    proc->pipe_stdin_write = NULL;
    proc->stdin_is_pipe = false;

    /*
     * Configure child stdin:
     *   - pipe mode: WinPatina writes translated VT bytes to child
     *   - console mode: child reads directly from the console input buffer
     */
    HANDLE child_stdin = NULL;
    if (use_console_stdin) {
        HANDLE parent_stdin = GetStdHandle(STD_INPUT_HANDLE);
        if (parent_stdin == NULL || parent_stdin == INVALID_HANDLE_VALUE) {
            wp_set_error("Failed to get console input handle for child stdin");
            return false;
        }

        DWORD console_mode = 0;
        if (!GetConsoleMode(parent_stdin, &console_mode)) {
            /* No console attached (e.g. redirected stdin) - fall back to pipe mode. */
            use_console_stdin = false;
        }
    }

    if (use_console_stdin) {
        HANDLE parent_stdin = GetStdHandle(STD_INPUT_HANDLE);

        if (!DuplicateHandle(GetCurrentProcess(), parent_stdin,
                             GetCurrentProcess(), &child_stdin,
                             0, TRUE, DUPLICATE_SAME_ACCESS)) {
            wp_set_error_win32("Failed to duplicate console stdin handle", GetLastError());
            return false;
        }
    } else {
        HANDLE pipe_stdin_read = NULL;
        if (!create_pipe_pair(&pipe_stdin_read, &proc->pipe_stdin_write,
                              true, false)) {
            wp_set_error_win32("Failed to create stdin pipe", GetLastError());
            return false;
        }
        child_stdin = pipe_stdin_read;
        proc->stdin_is_pipe = true;
    }

    /*
     * Create stdout pipe:
     *   child writes to pipe_stdout_write (inheritable)
     *   we read from pipe_stdout_read (not inheritable)
     */
    HANDLE pipe_stdout_write = NULL;
    if (!create_pipe_pair(&proc->pipe_stdout_read, &pipe_stdout_write,
                           false, true)) {
        wp_set_error_win32("Failed to create stdout pipe", GetLastError());
        safe_close(&child_stdin);
        safe_close(&proc->pipe_stdin_write);
        return false;
    }

    /*
     * Create a separate console screen buffer for the child's stderr.
     *
     * Modern TUI apps query the terminal size via
     * GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE)).
     * That fails on a pipe, so most frameworks fall back to stderr.
     * By making stderr a real console buffer, the query succeeds
     * and the app renders at the correct dimensions.
     *
     * We use a SEPARATE (non-active) screen buffer rather than CONOUT$
     * because sharing the active buffer with the child causes output
     * duplication under conpty — the child's writes shift the viewport
     * while our renderer paints at the new offset, producing multiple
     * copies of the same content.
     *
     * CreateConsoleScreenBuffer creates the buffer at the console's
     * current window size by default, which matches the dimensions we
     * pass as cols/rows. No explicit resize is needed at creation time.
     */
    HANDLE stderr_buf = NULL;
    if (console_stderr && cols > 0 && rows > 0) {
        SECURITY_ATTRIBUTES stderr_sa;
        memset(&stderr_sa, 0, sizeof(stderr_sa));
        stderr_sa.nLength = sizeof(stderr_sa);
        stderr_sa.bInheritHandle = TRUE;

        stderr_buf = CreateConsoleScreenBuffer(
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &stderr_sa,
            CONSOLE_TEXTMODE_BUFFER,
            NULL);

        if (stderr_buf == INVALID_HANDLE_VALUE) {
            stderr_buf = NULL;
        }
    }

    /*
     * Set up STARTUPINFOW with redirected handles.
     * stderr is a non-active console buffer (for CSBI queries), or
     * falls back to the stdout pipe if buffer creation failed.
     */
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = child_stdin;
    si.hStdOutput = pipe_stdout_write;
    si.hStdError  = (stderr_buf != NULL) ? stderr_buf : pipe_stdout_write;

    /*
     * Convert command line to wide string.
     * CreateProcessW requires a mutable buffer.
     */
    WCHAR* wide_cmdline = to_wide(cmdline);
    if (wide_cmdline == NULL) {
        wp_set_error("Failed to convert command line to wide string");
        safe_close(&child_stdin);
        safe_close(&pipe_stdout_write);
        safe_close(&proc->pipe_stdin_write);
        safe_close(&proc->pipe_stdout_read);
        return false;
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    /* Build environment block with COLUMNS/LINES if dimensions given */
    WCHAR* env_block = build_env_block(cols, rows);
    DWORD create_flags = CREATE_SUSPENDED;
    if (env_block) create_flags |= CREATE_UNICODE_ENVIRONMENT;

    BOOL ok = CreateProcessW(
        NULL,               /* Application name (NULL = parse from cmdline) */
        wide_cmdline,       /* Command line (mutable) */
        NULL,               /* Process security attributes */
        NULL,               /* Thread security attributes */
        TRUE,               /* Inherit handles */
        create_flags,       /* Creation flags */
        env_block,          /* Environment (with COLUMNS/LINES) */
        NULL,               /* Working directory (inherit parent) */
        &si,
        &pi
    );

    free(wide_cmdline);
    free(env_block);

    /*
     * Close the child's ends of the pipes — we don't need them.
     * The child holds its own copies of these handles.
     */
    safe_close(&child_stdin);
    safe_close(&pipe_stdout_write);

    if (!ok) {
        wp_set_error_win32("Failed to create process", GetLastError());
        safe_close(&stderr_buf);
        safe_close(&proc->pipe_stdin_write);
        safe_close(&proc->pipe_stdout_read);
        return false;
    }

    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->process_id = pi.dwProcessId;
    proc->hStderrBuffer = stderr_buf;
    proc->exited = false;
    proc->exit_code = 0;

    return true;
}

bool wp_process_spawn(WPProcess* proc, const char* cmdline,
                      int cols, int rows, bool console_stderr)
{
    return wp_process_spawn_ex(proc, cmdline, cols, rows, console_stderr, false);
}

bool wp_process_spawn_shell(WPProcess* proc, int cols, int rows)
{
    if (proc == NULL) {
        wp_set_error("Invalid parameters for shell spawn");
        return false;
    }

    const char* comspec = getenv("COMSPEC");
    if (comspec == NULL || comspec[0] == '\0') {
        comspec = "cmd.exe";
    }

    return wp_process_spawn(proc, comspec, cols, rows, false);
}

bool wp_process_resume(WPProcess* proc)
{
    if (proc == NULL || proc->hThread == NULL) return false;

    DWORD result = ResumeThread(proc->hThread);
    return (result != (DWORD)-1);
}

void wp_process_destroy(WPProcess* proc)
{
    if (proc == NULL) return;

    safe_close(&proc->pipe_stdin_write);
    safe_close(&proc->pipe_stdout_read);
    safe_close(&proc->hStderrBuffer);
    safe_close(&proc->hThread);
    safe_close(&proc->hProcess);

    proc->process_id = 0;
    proc->stdin_is_pipe = false;
}

/*============================================================================
 * Public API - I/O
 *============================================================================*/

int wp_process_read(WPProcess* proc, uint8_t* buf, int buf_size)
{
    if (proc == NULL || buf == NULL || buf_size <= 0) return 0;
    if (proc->pipe_stdout_read == NULL) return -1;

    /*
     * PeekNamedPipe to check for available data without blocking.
     * This is the standard pattern for non-blocking pipe reads on Win32.
     */
    DWORD available = 0;
    if (!PeekNamedPipe(proc->pipe_stdout_read, NULL, 0, NULL, &available, NULL)) {
        /* Pipe broken — child likely exited */
        return -1;
    }

    if (available == 0) {
        return 0;
    }

    /* Read up to buf_size bytes (or whatever is available) */
    DWORD to_read = (available < (DWORD)buf_size) ? available : (DWORD)buf_size;
    DWORD bytes_read = 0;

    if (!ReadFile(proc->pipe_stdout_read, buf, to_read, &bytes_read, NULL)) {
        return -1;
    }

    return (int)bytes_read;
}

int wp_process_write(WPProcess* proc, const uint8_t* data, int len)
{
    if (proc == NULL || data == NULL || len <= 0) return 0;
    if (proc->pipe_stdin_write == NULL) return -1;

    DWORD bytes_written = 0;
    if (!WriteFile(proc->pipe_stdin_write, data, (DWORD)len, &bytes_written, NULL)) {
        return -1;
    }

    return (int)bytes_written;
}

/*============================================================================
 * Public API - Stderr Buffer
 *============================================================================*/

bool wp_process_update_stderr_size(WPProcess* proc, int cols, int rows)
{
    if (proc == NULL) return false;
    if (proc->hStderrBuffer == NULL) return false;
    if (cols <= 0 || rows <= 0) return false;

    COORD buf_size;
    buf_size.X = (SHORT)cols;
    buf_size.Y = (SHORT)rows;

    /*
     * Shrink window first (buffer can't be smaller than the window),
     * then resize buffer, then expand window.
     */
    SMALL_RECT small_win = {0, 0, 0, 0};
    SetConsoleWindowInfo(proc->hStderrBuffer, TRUE, &small_win);

    if (!SetConsoleScreenBufferSize(proc->hStderrBuffer, buf_size)) {
        return false;
    }

    SMALL_RECT win_rect;
    win_rect.Left   = 0;
    win_rect.Top    = 0;
    win_rect.Right  = (SHORT)(cols - 1);
    win_rect.Bottom = (SHORT)(rows - 1);
    SetConsoleWindowInfo(proc->hStderrBuffer, TRUE, &win_rect);

    return true;
}

/*============================================================================
 * Public API - Status
 *============================================================================*/

bool wp_process_poll(WPProcess* proc)
{
    if (proc == NULL) return false;
    if (proc->hProcess == NULL) return false;
    if (proc->exited) return true;

    DWORD code = 0;
    if (GetExitCodeProcess(proc->hProcess, &code)) {
        if (code != STILL_ACTIVE) {
            proc->exited = true;
            proc->exit_code = code;
            return true;
        }
    }

    return false;
}

bool wp_process_is_running(const WPProcess* proc)
{
    if (proc == NULL) return false;
    if (proc->hProcess == NULL) return false;
    return !proc->exited;
}

int wp_process_get_exit_code(const WPProcess* proc)
{
    if (proc == NULL) return -1;
    if (!proc->exited) return -1;
    return (int)proc->exit_code;
}

bool wp_process_terminate(WPProcess* proc, UINT exit_code)
{
    if (proc == NULL) return false;
    if (proc->hProcess == NULL) return false;

    if (!TerminateProcess(proc->hProcess, exit_code)) {
        return false;
    }

    /* Wait briefly for the process to actually terminate */
    WaitForSingleObject(proc->hProcess, 1000);

    proc->exited = true;
    proc->exit_code = exit_code;
    return true;
}

void wp_process_close_stdin(WPProcess* proc)
{
    if (proc == NULL) return;
    safe_close(&proc->pipe_stdin_write);
}

HANDLE wp_process_get_stdout_handle(const WPProcess* proc)
{
    if (proc == NULL) return NULL;
    return proc->pipe_stdout_read;
}

bool wp_process_stdin_is_pipe(const WPProcess* proc)
{
    if (proc == NULL) return false;
    return proc->stdin_is_pipe;
}
