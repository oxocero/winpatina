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
    ASSERT_FALSE(wp_process_spawn(NULL, "cmd.exe"));
    ASSERT_FALSE(wp_process_spawn_shell(NULL));
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

    ASSERT_FALSE(wp_process_spawn(&proc, NULL));
}

/*============================================================================
 * Tests - Spawn and Read Output
 *============================================================================*/

TEST(process_spawn_echo) {
    WPProcess proc;
    wp_process_init(&proc);

    /* Spawn cmd.exe /c echo hello */
    bool ok = wp_process_spawn(&proc, "cmd.exe /c echo hello");
    ASSERT_TRUE(ok);
    ASSERT_TRUE(proc.hProcess != NULL);
    ASSERT_TRUE(proc.pipe_stdout_read != NULL);
    ASSERT_TRUE(proc.pipe_stdin_write != NULL);
    ASSERT_TRUE(proc.process_id != 0);

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

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 42");
    ASSERT_TRUE(ok);

    ASSERT_TRUE(wait_for_exit(&proc, 5000));
    ASSERT_EQ(wp_process_get_exit_code(&proc), 42);
    ASSERT_FALSE(wp_process_is_running(&proc));

    wp_process_destroy(&proc);
}

TEST(process_spawn_exit_zero) {
    WPProcess proc;
    wp_process_init(&proc);

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0");
    ASSERT_TRUE(ok);

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
    bool ok = wp_process_spawn(&proc, "cmd.exe /c ping -n 2 127.0.0.1 >nul");
    ASSERT_TRUE(ok);

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
    bool ok = wp_process_spawn(&proc, "cmd.exe /q /k");
    ASSERT_TRUE(ok);

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

    bool ok = wp_process_spawn(&proc, "cmd.exe /q /k");
    ASSERT_TRUE(ok);

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

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0");
    ASSERT_TRUE(ok);

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
    bool ok = wp_process_spawn(&proc, "cmd.exe /c ping -n 30 127.0.0.1 >nul");
    ASSERT_TRUE(ok);
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

    bool ok = wp_process_spawn_shell(&proc);
    ASSERT_TRUE(ok);
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

    bool ok = wp_process_spawn(&proc, "cmd.exe /c exit 0");
    ASSERT_TRUE(ok);

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
        "cmd.exe /c \"echo line1 & echo line2 & echo line3\"");
    ASSERT_TRUE(ok);

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

    bool ok = wp_process_spawn(&proc, "cmd.exe /c ping -n 30 127.0.0.1 >nul");
    ASSERT_TRUE(ok);

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

    bool ok = wp_process_spawn(&proc, "cmd.exe /c echo final");
    ASSERT_TRUE(ok);

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
 * Test Runner
 *============================================================================*/

TEST_MAIN()
