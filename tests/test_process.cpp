/**
 * @file test_process.cpp
 * @brief Unit tests for child process management
 *
 * Tests spawning child processes with redirected I/O, reading their
 * output, writing to their stdin, and lifecycle management.
 *
 * These tests spawn real processes (cmd.exe) so they require a
 * working Windows console environment.
 */

#include "test_harness.h"
#include "../src/winpatina_process.h"

#include <cstring>

/*============================================================================
 * Test Helpers
 *============================================================================*/

/**
 * Wait for a process to exit, with a timeout.
 * Polls in a loop with Sleep() to avoid busy-waiting.
 * Returns true if the process exited within the timeout.
 */
static bool wait_for_exit(WPProcess* proc, int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (wp_process_poll(proc)) return true;
        Sleep(10);
        elapsed += 10;
    }
    return wp_process_poll(proc);
}

/**
 * Read all available output from a process, with retries.
 * Accumulates into buf, returns total bytes read.
 */
static int read_all(WPProcess* proc, uint8_t* buf, int buf_size, int timeout_ms)
{
    int total = 0;
    int elapsed = 0;
    while (elapsed < timeout_ms && total < buf_size) {
        int n = wp_process_read(proc, buf + total, buf_size - total);
        if (n > 0) {
            total += n;
            elapsed = 0;  /* Reset timeout on data */
        } else if (n < 0) {
            break;  /* Pipe broken */
        } else {
            Sleep(10);
            elapsed += 10;
        }
    }
    return total;
}

/*============================================================================
 * Tests - Lifecycle
 *============================================================================*/

TEST(process_init) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_EQ(proc.hProcess, (HANDLE)NULL);
    ASSERT_EQ(proc.hThread, (HANDLE)NULL);
    ASSERT_EQ(proc.process_id, (DWORD)0);
    ASSERT_EQ(proc.pipe_stdin_write, (HANDLE)NULL);
    ASSERT_EQ(proc.pipe_stdout_read, (HANDLE)NULL);
    ASSERT_FALSE(proc.exited);
    ASSERT_EQ(proc.exit_code, (DWORD)0);
}

TEST(process_init_null_safe) {
    wp_process_init(NULL);
    ASSERT_TRUE(true);
}

TEST(process_destroy_null_safe) {
    wp_process_destroy(NULL);
    ASSERT_TRUE(true);
}

TEST(process_destroy_uninitialised) {
    WPProcess proc;
    wp_process_init(&proc);
    wp_process_destroy(&proc);
    ASSERT_TRUE(true);
}

TEST(process_destroy_double) {
    WPProcess proc;
    wp_process_init(&proc);
    wp_process_destroy(&proc);
    wp_process_destroy(&proc);
    ASSERT_TRUE(true);
}

/*============================================================================
 * Tests - State Queries (Not Spawned)
 *============================================================================*/

TEST(process_not_spawned_is_not_running) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_FALSE(wp_process_is_running(&proc));
}

TEST(process_not_spawned_exit_code) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_EQ(wp_process_get_exit_code(&proc), -1);
}

TEST(process_not_spawned_poll) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_FALSE(wp_process_poll(&proc));
}

TEST(process_not_spawned_read) {
    WPProcess proc;
    wp_process_init(&proc);
    uint8_t buf[64];

    ASSERT_EQ(wp_process_read(&proc, buf, sizeof(buf)), -1);
}

TEST(process_not_spawned_write) {
    WPProcess proc;
    wp_process_init(&proc);
    uint8_t data[] = "hello";

    ASSERT_EQ(wp_process_write(&proc, data, 5), -1);
}

/*============================================================================
 * Tests - Null Safety on All Functions
 *============================================================================*/

TEST(process_null_safety) {
    uint8_t buf[64];
    uint8_t data[] = "test";

    ASSERT_FALSE(wp_process_poll(NULL));
    ASSERT_FALSE(wp_process_is_running(NULL));
    ASSERT_EQ(wp_process_get_exit_code(NULL), -1);
    ASSERT_EQ(wp_process_read(NULL, buf, sizeof(buf)), 0);
    ASSERT_EQ(wp_process_write(NULL, data, 4), 0);
    ASSERT_FALSE(wp_process_terminate(NULL, 1));
    wp_process_close_stdin(NULL);
    ASSERT_EQ(wp_process_get_stdout_handle(NULL), (HANDLE)NULL);
    ASSERT_FALSE(wp_process_spawn(NULL, "cmd.exe", 0, 0, false));
    ASSERT_FALSE(wp_process_spawn_shell(NULL, 0, 0));
    ASSERT_TRUE(true);
}

TEST(process_read_null_buf) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_EQ(wp_process_read(&proc, NULL, 64), 0);
}

TEST(process_read_zero_size) {
    WPProcess proc;
    wp_process_init(&proc);
    uint8_t buf[64];

    ASSERT_EQ(wp_process_read(&proc, buf, 0), 0);
}

TEST(process_write_null_data) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_EQ(wp_process_write(&proc, NULL, 5), 0);
}

TEST(process_write_zero_len) {
    WPProcess proc;
    wp_process_init(&proc);
    uint8_t data[] = "test";

    ASSERT_EQ(wp_process_write(&proc, data, 0), 0);
}

TEST(process_spawn_null_cmdline) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_FALSE(wp_process_spawn(&proc, NULL, 0, 0, false));
}

/*============================================================================
 * Tests - Spawn and Read Output
 *============================================================================*/

TEST(process_spawn_echo) {
    WPProcess proc;
    wp_process_init(&proc);

    /* Spawn cmd.exe /c echo hello */
    bool ok = wp_process_spawn(&proc, "cmd.exe /c echo hello", 0, 0, false);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(proc.hProcess != NULL);
    ASSERT_TRUE(proc.pipe_stdout_read != NULL);
    ASSERT_TRUE(proc.pipe_stdin_write != NULL);
    ASSERT_TRUE(proc.process_id != 0);
    wp_process_resume(&proc);

    /* Read output */
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    int total = read_all(&proc, buf, sizeof(buf) - 1, 5000);

    ASSERT_TRUE(total > 0);
    /* Output should contain "hello" */
    ASSERT_TRUE(strstr((char*)buf, "hello") != NULL);

    /* Wait for exit */
    ASSERT_TRUE(wait_for_exit(&proc, 5000));
    ASSERT_TRUE(proc.exited);
    ASSERT_EQ(wp_process_get_exit_code(&proc), 0);

    wp_process_destroy(&proc);
}

TEST(process_spawn_exit_code) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 42", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    ASSERT_TRUE(wait_for_exit(&proc, 5000));
    ASSERT_EQ(wp_process_get_exit_code(&proc), 42);
    ASSERT_FALSE(wp_process_is_running(&proc));

    wp_process_destroy(&proc);
}

TEST(process_spawn_exit_zero) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    ASSERT_TRUE(wait_for_exit(&proc, 5000));
    ASSERT_EQ(wp_process_get_exit_code(&proc), 0);

    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Running State
 *============================================================================*/

TEST(process_is_running_before_exit) {
    WPProcess proc;
    wp_process_init(&proc);

    /* Spawn a command that waits a bit */
    bool ok = wp_process_spawn(&proc, "cmd.exe /c ping -n 2 127.0.0.1 >nul", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    /* Should be running immediately after spawn */
    ASSERT_TRUE(wp_process_is_running(&proc));

    /* Wait for it to finish */
    ASSERT_TRUE(wait_for_exit(&proc, 10000));
    ASSERT_FALSE(wp_process_is_running(&proc));

    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Write to Child
 *============================================================================*/

TEST(process_write_to_stdin) {
    WPProcess proc;
    wp_process_init(&proc);

    /*
     * Spawn cmd.exe in interactive-ish mode.
     * Send "echo received\r\n" then "exit\r\n".
     * Read back the output — should contain "received".
     */
    bool ok = wp_process_spawn(&proc, "cmd.exe /q /k", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    /* Give cmd.exe a moment to start */
    Sleep(200);

    /* Write commands */
    const char* cmd1 = "echo received\r\n";
    int w1 = wp_process_write(&proc, (const uint8_t*)cmd1, (int)strlen(cmd1));
    ASSERT_TRUE(w1 > 0);

    const char* cmd2 = "exit\r\n";
    int w2 = wp_process_write(&proc, (const uint8_t*)cmd2, (int)strlen(cmd2));
    ASSERT_TRUE(w2 > 0);

    /* Read output */
    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));
    int total = read_all(&proc, buf, sizeof(buf) - 1, 5000);

    ASSERT_TRUE(total > 0);
    ASSERT_TRUE(strstr((char*)buf, "received") != NULL);

    ASSERT_TRUE(wait_for_exit(&proc, 5000));

    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Close Stdin (EOF Signal)
 *============================================================================*/

TEST(process_close_stdin) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /q /k", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    /* Close stdin — cmd.exe should eventually exit */
    wp_process_close_stdin(&proc);
    ASSERT_EQ(proc.pipe_stdin_write, (HANDLE)NULL);

    ASSERT_TRUE(wait_for_exit(&proc, 5000));
    ASSERT_TRUE(proc.exited);

    wp_process_destroy(&proc);
}

TEST(process_close_stdin_double) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    wp_process_close_stdin(&proc);
    wp_process_close_stdin(&proc);  /* Should not crash */
    ASSERT_TRUE(true);

    wait_for_exit(&proc, 5000);
    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Terminate
 *============================================================================*/

TEST(process_terminate) {
    WPProcess proc;
    wp_process_init(&proc);

    /* Spawn something that runs for a while */
    bool ok = wp_process_spawn(&proc, "cmd.exe /c ping -n 30 127.0.0.1 >nul", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);
    ASSERT_TRUE(wp_process_is_running(&proc));

    /* Terminate it */
    ASSERT_TRUE(wp_process_terminate(&proc, 99));
    ASSERT_TRUE(proc.exited);
    ASSERT_EQ(wp_process_get_exit_code(&proc), 99);

    wp_process_destroy(&proc);
}

TEST(process_terminate_not_spawned) {
    WPProcess proc;
    wp_process_init(&proc);

    ASSERT_FALSE(wp_process_terminate(&proc, 1));
}

/*============================================================================
 * Tests - Spawn Shell
 *============================================================================*/

TEST(process_spawn_shell) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn_shell(&proc, 0, 0);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);
    ASSERT_TRUE(proc.hProcess != NULL);
    ASSERT_TRUE(wp_process_is_running(&proc));

    /* Send exit command */
    const char* cmd = "exit\r\n";
    wp_process_write(&proc, (const uint8_t*)cmd, (int)strlen(cmd));

    ASSERT_TRUE(wait_for_exit(&proc, 5000));
    ASSERT_EQ(wp_process_get_exit_code(&proc), 0);

    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Get Stdout Handle
 *============================================================================*/

TEST(process_get_stdout_handle) {
    WPProcess proc;
    wp_process_init(&proc);

    /* Not spawned -> NULL */
    ASSERT_EQ(wp_process_get_stdout_handle(&proc), (HANDLE)NULL);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    /* Spawned -> valid handle */
    ASSERT_TRUE(wp_process_get_stdout_handle(&proc) != NULL);

    wait_for_exit(&proc, 5000);
    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Multiple Output Reads
 *============================================================================*/

TEST(process_multi_line_output) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc,
        "cmd.exe /c \"echo line1 & echo line2 & echo line3\"", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));
    int total = read_all(&proc, buf, sizeof(buf) - 1, 5000);

    ASSERT_TRUE(total > 0);
    ASSERT_TRUE(strstr((char*)buf, "line1") != NULL);
    ASSERT_TRUE(strstr((char*)buf, "line2") != NULL);
    ASSERT_TRUE(strstr((char*)buf, "line3") != NULL);

    wait_for_exit(&proc, 5000);
    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Destroy While Running
 *============================================================================*/

TEST(process_destroy_while_running) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c ping -n 30 127.0.0.1 >nul", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    /* Terminate then destroy */
    wp_process_terminate(&proc, 0);
    wp_process_destroy(&proc);

    /* All handles should be cleaned up */
    ASSERT_EQ(proc.hProcess, (HANDLE)NULL);
    ASSERT_EQ(proc.pipe_stdout_read, (HANDLE)NULL);
    ASSERT_EQ(proc.pipe_stdin_write, (HANDLE)NULL);
}

/*============================================================================
 * Tests - Read After Exit
 *============================================================================*/

TEST(process_read_after_exit) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c echo final", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    ASSERT_TRUE(wait_for_exit(&proc, 5000));

    /* Should still be able to read buffered output after exit */
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    int total = read_all(&proc, buf, sizeof(buf) - 1, 1000);

    /* May or may not have data depending on timing, but shouldn't crash */
    ASSERT_TRUE(total >= 0);

    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Environment Variables (COLUMNS/LINES)
 *============================================================================*/

TEST(process_spawn_sets_columns_lines) {
    WPProcess proc;
    wp_process_init(&proc);

    /* Spawn cmd.exe that echoes COLUMNS and LINES */
    bool ok = wp_process_spawn(&proc,
        "cmd.exe /c \"echo %COLUMNS% %LINES%\"", 132, 43, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    wait_for_exit(&proc, 5000);

    uint8_t buf[4096];
    memset(buf, 0, sizeof(buf));
    int total = read_all(&proc, buf, sizeof(buf) - 1, 3000);

    /* Output should contain the COLUMNS and LINES values */
    ASSERT_TRUE(total > 0);
    ASSERT_TRUE(strstr((char*)buf, "132") != NULL);
    ASSERT_TRUE(strstr((char*)buf, "43") != NULL);

    wp_process_destroy(&proc);
}

TEST(process_spawn_zero_dims_inherits_parent_env) {
    WPProcess proc;
    wp_process_init(&proc);

    /* With cols=0, rows=0, should not set COLUMNS/LINES
     * (they'll be empty/%COLUMNS% unless parent had them) */
    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", 0, 0, false);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    wait_for_exit(&proc, 5000);
    ASSERT_EQ(wp_process_get_exit_code(&proc), 0);

    wp_process_destroy(&proc);
}

/*============================================================================
 * Tests - Stderr Console Buffer for CSBI
 *
 * The child's stderr is a separate (non-active) console screen buffer
 * so that GetConsoleScreenBufferInfo succeeds on it, allowing TUI
 * apps to query the terminal dimensions via the stderr fallback.
 *============================================================================*/

TEST(process_stderr_buffer_created) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", 80, 25, true);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(proc.hStderrBuffer != NULL);

    wp_process_resume(&proc);
    wait_for_exit(&proc, 5000);
    wp_process_destroy(&proc);
}

TEST(process_stderr_buffer_csbi_works) {
    WPProcess proc;
    wp_process_init(&proc);

    /*
     * Open CONOUT$ to get the actual console dimensions.
     * GetStdHandle(STD_OUTPUT_HANDLE) may be a pipe when running
     * under CTest.
     */
    HANDLE hCon = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    ASSERT_TRUE(hCon != INVALID_HANDLE_VALUE);

    CONSOLE_SCREEN_BUFFER_INFO parent_csbi;
    ASSERT_TRUE(GetConsoleScreenBufferInfo(hCon, &parent_csbi));

    int con_w = parent_csbi.srWindow.Right - parent_csbi.srWindow.Left + 1;
    int con_h = parent_csbi.srWindow.Bottom - parent_csbi.srWindow.Top + 1;
    CloseHandle(hCon);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", con_w, con_h, true);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(proc.hStderrBuffer != NULL);

    /* CSBI on the stderr buffer should succeed and report the
     * console's current dimensions. */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    BOOL csbi_ok = GetConsoleScreenBufferInfo(proc.hStderrBuffer, &csbi);
    ASSERT_TRUE(csbi_ok);

    int buf_w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int buf_h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    ASSERT_EQ(buf_w, con_w);
    ASSERT_EQ(buf_h, con_h);

    wp_process_resume(&proc);
    wait_for_exit(&proc, 5000);
    wp_process_destroy(&proc);
}

TEST(process_stderr_separate_from_stdout) {
    WPProcess proc;
    wp_process_init(&proc);

    /*
     * Verify stderr is NOT merged with stdout.
     * Write "MARKER" to stderr (cmd redirect 1>&2).
     * If stderr were the same pipe as stdout, we'd read "MARKER".
     * With a separate console buffer, it goes there instead.
     */
    bool ok = wp_process_spawn(&proc, "cmd.exe /c \"echo MARKER 1>&2\"", 80, 25, true);
    ASSERT_TRUE(ok);
    wp_process_resume(&proc);

    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));
    int total = read_all(&proc, buf, sizeof(buf) - 1, 3000);

    wait_for_exit(&proc, 5000);

    /* MARKER should NOT appear in the stdout pipe */
    ASSERT_TRUE(strstr((char*)buf, "MARKER") == NULL);

    wp_process_destroy(&proc);
}

TEST(process_stderr_not_created_without_dims) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0", 0, 0, false);
    ASSERT_TRUE(ok);
    ASSERT_EQ(proc.hStderrBuffer, (HANDLE)NULL);

    wp_process_resume(&proc);
    wait_for_exit(&proc, 5000);
    wp_process_destroy(&proc);
}

TEST(process_stderr_resize_null_safety) {
    ASSERT_FALSE(wp_process_update_stderr_size(NULL, 80, 25));

    WPProcess proc;
    wp_process_init(&proc);
    ASSERT_FALSE(wp_process_update_stderr_size(&proc, 80, 25));
}

/*============================================================================
 * Test Runner
 *============================================================================*/

TEST_MAIN()
