/**
 * @file winpatina_process.h
 * @brief Child process management - internal header
 *
 * Handles spawning a child process with its stdin/stdout redirected
 * through anonymous pipes. Provides non-blocking I/O primitives for
 * reading the child's output and writing to its input.
 *
 *   Console Input --> [pipe] --> Child stdin
 *   Child stdout  --> [pipe] --> VT Parser --> Screen --> Renderer
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_PROCESS_H
#define WINPATINA_PROCESS_H

#include <stdint.h>
#include <stdbool.h>

/* Windows headers */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Constants
 *============================================================================*/

/** Default pipe buffer size (64 KiB). */
#define WP_PIPE_BUF_SIZE (64 * 1024)

/*============================================================================
 * Process State
 *============================================================================*/

/**
 * @brief Child process state
 *
 * Manages the lifecycle of a spawned child process and its
 * redirected I/O pipes.
 */
typedef struct {
    /** Process handle (NULL if not spawned) */
    HANDLE hProcess;

    /** Primary thread handle */
    HANDLE hThread;

    /** Process ID */
    DWORD process_id;

    /**
     * Pipe: our write end --> child's stdin.
     * Write to this handle to send data to the child.
     */
    HANDLE pipe_stdin_write;

    /**
     * Pipe: child's stdout --> our read end.
     * Read from this handle to get the child's output.
     */
    HANDLE pipe_stdout_read;

    /**
     * Whether the child has exited.
     * Updated by wp_process_poll().
     */
    bool exited;

    /** Child's exit code (valid only when exited == true). */
    DWORD exit_code;
} WPProcess;

/*============================================================================
 * Lifecycle
 *============================================================================*/

/**
 * @brief Initialise the process state
 *
 * Zeroes all handles and state. Must be called before wp_process_spawn().
 *
 * @param proc Process state to initialise
 */
void wp_process_init(WPProcess* proc);

/**
 * @brief Spawn a child process with redirected I/O
 *
 * Creates anonymous pipes for stdin and stdout, then launches the
 * child process via CreateProcessW. The child's stderr is merged
 * with stdout (same pipe).
 *
 * @param proc     Process state (must be initialised)
 * @param cmdline  Command line to execute (will be passed to
 *                 CreateProcessW, which may modify it in-place)
 * @return true on success, false on failure
 */
bool wp_process_spawn(WPProcess* proc, const char* cmdline);

/**
 * @brief Spawn the default shell
 *
 * Launches %COMSPEC% (typically cmd.exe) with redirected I/O.
 *
 * @param proc  Process state (must be initialised)
 * @return true on success, false on failure
 */
bool wp_process_spawn_shell(WPProcess* proc);

/**
 * @brief Clean up process resources
 *
 * Closes all pipe handles and process/thread handles.
 * Safe to call multiple times or on an uninitialised (zeroed) state.
 * Does NOT terminate the child — call wp_process_terminate() first
 * if needed.
 *
 * @param proc Process state to clean up
 */
void wp_process_destroy(WPProcess* proc);

/*============================================================================
 * I/O
 *============================================================================*/

/**
 * @brief Read available data from the child's stdout (non-blocking)
 *
 * Uses PeekNamedPipe to check for available data, then reads up to
 * buf_size bytes. Returns immediately with 0 if no data is available.
 *
 * @param proc      Process state
 * @param buf       Buffer to read into
 * @param buf_size  Maximum bytes to read
 * @return Number of bytes read, 0 if no data available,
 *         or -1 on error (pipe broken = child exited)
 */
int wp_process_read(WPProcess* proc, uint8_t* buf, int buf_size);

/**
 * @brief Write data to the child's stdin
 *
 * Writes up to len bytes to the child's stdin pipe. This is a
 * blocking write (returns when all data is written or on error).
 *
 * @param proc  Process state
 * @param data  Data to write
 * @param len   Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
int wp_process_write(WPProcess* proc, const uint8_t* data, int len);

/*============================================================================
 * Status
 *============================================================================*/

/**
 * @brief Check if the child process has exited
 *
 * Non-blocking check using GetExitCodeProcess. Updates the
 * exited flag and exit_code in the process state.
 *
 * @param proc  Process state
 * @return true if the child has exited, false if still running
 */
bool wp_process_poll(WPProcess* proc);

/**
 * @brief Check if the child process is alive
 *
 * @param proc  Process state
 * @return true if spawned and not yet exited
 */
bool wp_process_is_running(const WPProcess* proc);

/**
 * @brief Get the child's exit code
 *
 * Only valid after wp_process_poll() returns true.
 *
 * @param proc  Process state
 * @return Exit code, or -1 if not yet exited
 */
int wp_process_get_exit_code(const WPProcess* proc);

/**
 * @brief Terminate the child process
 *
 * Forcefully terminates the child. Use as a last resort — prefer
 * sending Ctrl+C or closing the stdin pipe to allow graceful exit.
 *
 * @param proc       Process state
 * @param exit_code  Exit code to assign to the terminated process
 * @return true if terminated, false on error
 */
bool wp_process_terminate(WPProcess* proc, UINT exit_code);

/**
 * @brief Signal the child to exit by closing its stdin pipe
 *
 * Many console programs exit when they detect EOF on stdin.
 * This is gentler than wp_process_terminate().
 *
 * @param proc  Process state
 */
void wp_process_close_stdin(WPProcess* proc);

/**
 * @brief Get the stdout read handle for use with WaitForMultipleObjects
 *
 * This allows the event loop to wait on both console input and
 * child output simultaneously.
 *
 * @param proc  Process state
 * @return Pipe read handle, or NULL if not spawned
 */
HANDLE wp_process_get_stdout_handle(const WPProcess* proc);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_PROCESS_H */
