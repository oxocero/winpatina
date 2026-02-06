/**
 * @file winpatina_process.cpp
 * @brief Child process management implementation
 *
 * Spawns a child process with its stdin/stdout/stderr redirected
 * through anonymous pipes, providing non-blocking read and
 * blocking write primitives.
 *
 * Pipe architecture:
 *
 *   [pipe_stdin_write] --write--> [pipe_stdin_read]  --> child stdin
 *   child stdout --> [pipe_stdout_write] --read--> [pipe_stdout_read]
 *   child stderr --> [pipe_stdout_write] (merged with stdout)
 *
 * The "read" and "write" in handle names refer to our side of the pipe.
 */

#include "winpatina_process.h"
#include "internal.h"
#include <cstdlib>  /* getenv, malloc, free */
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

/*============================================================================
 * Public API - Lifecycle
 *============================================================================*/

void wp_process_init(WPProcess* proc)
{
    if (proc == NULL) return;
    memset(proc, 0, sizeof(*proc));
}

bool wp_process_spawn(WPProcess* proc, const char* cmdline)
{
    if (proc == NULL || cmdline == NULL) {
        wp_set_error("Invalid parameters for process spawn");
        return false;
    }

    /*
     * Create stdin pipe:
     *   child reads from pipe_stdin_read (inheritable)
     *   we write to pipe_stdin_write (not inheritable)
     */
    HANDLE pipe_stdin_read = NULL;
    if (!create_pipe_pair(&pipe_stdin_read, &proc->pipe_stdin_write,
                           true, false)) {
        wp_set_error_win32("Failed to create stdin pipe", GetLastError());
        return false;
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
        safe_close(&pipe_stdin_read);
        safe_close(&proc->pipe_stdin_write);
        return false;
    }

    /*
     * Set up STARTUPINFOW with redirected handles.
     * stderr is merged with stdout (same pipe).
     */
    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = pipe_stdin_read;
    si.hStdOutput = pipe_stdout_write;
    si.hStdError  = pipe_stdout_write;

    /*
     * Convert command line to wide string.
     * CreateProcessW requires a mutable buffer.
     */
    WCHAR* wide_cmdline = to_wide(cmdline);
    if (wide_cmdline == NULL) {
        wp_set_error("Failed to convert command line to wide string");
        safe_close(&pipe_stdin_read);
        safe_close(&pipe_stdout_write);
        safe_close(&proc->pipe_stdin_write);
        safe_close(&proc->pipe_stdout_read);
        return false;
    }

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    BOOL ok = CreateProcessW(
        NULL,               /* Application name (NULL = parse from cmdline) */
        wide_cmdline,       /* Command line (mutable) */
        NULL,               /* Process security attributes */
        NULL,               /* Thread security attributes */
        TRUE,               /* Inherit handles */
        0,                  /* Creation flags */
        NULL,               /* Environment (inherit parent) */
        NULL,               /* Working directory (inherit parent) */
        &si,
        &pi
    );

    free(wide_cmdline);

    /*
     * Close the child's ends of the pipes — we don't need them.
     * The child holds its own copies of these handles.
     */
    safe_close(&pipe_stdin_read);
    safe_close(&pipe_stdout_write);

    if (!ok) {
        wp_set_error_win32("Failed to create process", GetLastError());
        safe_close(&proc->pipe_stdin_write);
        safe_close(&proc->pipe_stdout_read);
        return false;
    }

    proc->hProcess = pi.hProcess;
    proc->hThread = pi.hThread;
    proc->process_id = pi.dwProcessId;
    proc->exited = false;
    proc->exit_code = 0;

    return true;
}

bool wp_process_spawn_shell(WPProcess* proc)
{
    if (proc == NULL) {
        wp_set_error("Invalid parameters for shell spawn");
        return false;
    }

    const char* comspec = getenv("COMSPEC");
    if (comspec == NULL || comspec[0] == '\0') {
        comspec = "cmd.exe";
    }

    return wp_process_spawn(proc, comspec);
}

void wp_process_destroy(WPProcess* proc)
{
    if (proc == NULL) return;

    safe_close(&proc->pipe_stdin_write);
    safe_close(&proc->pipe_stdout_read);
    safe_close(&proc->hThread);
    safe_close(&proc->hProcess);

    proc->process_id = 0;
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
