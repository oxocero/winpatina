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

