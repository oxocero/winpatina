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
static __declspec(thread) char tls_error_buffer[WP_ERROR_BUFFER_SIZE];
static __declspec(thread) bool tls_error_set = false;

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

    return wp;
}

