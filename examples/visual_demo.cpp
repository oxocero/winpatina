/**
 * @file visual_demo.cpp
 * @brief Visual demonstration of the WinPatina pipeline
 *
 * Feeds hardcoded VT escape sequences through the full pipeline:
 *   VT bytes -> Parser -> Dispatch -> Screen Buffer -> Renderer -> Console
 *
 * This is the first end-to-end visual test of all components working
 * together. Run it and you should see coloured, formatted text rendered
 * to the console via WriteConsoleOutputW.
 */

#include "../src/winpatina_vt_parser.h"
#include "../src/winpatina_screen.h"
#include "../src/winpatina_colour.h"
#include "../src/winpatina_dispatch.h"
#include "../src/winpatina_render.h"

#include <stdio.h>
#include <string.h>

/**
 * Feed a string through the VT pipeline.
 */
static void emit(WPVTParser* parser, const char* str)
{
    wp_vt_parser_feed(parser, (const uint8_t*)str, strlen(str));
}

int main(void)
{
    /* Get the console output handle */
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to get console handle\n");
        return 1;
    }

    /* Query current console size */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
        fprintf(stderr, "Failed to get console info\n");
        return 1;
    }

    int width  = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    WORD default_attrs = csbi.wAttributes;

    /* Detect LVB support (Vista+, NT 6.0) */
    OSVERSIONINFOW osvi;
    memset(&osvi, 0, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    GetVersionExW(&osvi);
    bool has_lvb = (osvi.dwMajorVersion >= 6);

    /* Create the pipeline components */
    WPScreenBuffer* screen = wp_screen_create(width, height, default_attrs);
    if (!screen) {
        fprintf(stderr, "Failed to create screen buffer\n");
        return 1;
    }

    WPVTParser parser;
    WPDispatchState dispatch;
    WPRenderer renderer;

    wp_dispatch_init(&dispatch, screen, default_attrs, has_lvb,
                     NULL, INVALID_HANDLE_VALUE, NULL, NULL, NULL);
    wp_vt_parser_init(&parser, NULL);
    wp_dispatch_attach(&dispatch, &parser);

    if (!wp_renderer_init(&renderer, hOut, screen)) {
        fprintf(stderr, "Failed to init renderer\n");
        wp_screen_destroy(screen);
        return 1;
    }

    /*====================================================================
     * Demo content - VT sequences fed through the full pipeline
     *====================================================================*/

    /* Clear screen and home cursor */
    emit(&parser, "\x1b[2J\x1b[H");

    /* Title banner with bright white on blue background */
    emit(&parser, "\x1b[1;37;44m");
    emit(&parser, "  WinPatina Visual Demo ");
    emit(&parser, "\x1b[0m");
    emit(&parser, "\r\n\r\n");

    /* Standard 8 foreground colours */
    emit(&parser, "  Standard colours:  ");
    emit(&parser, "\x1b[30m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Black blocks */
    emit(&parser, "\x1b[31m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Red */
    emit(&parser, "\x1b[32m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Green */
    emit(&parser, "\x1b[33m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Yellow */
    emit(&parser, "\x1b[34m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Blue */
    emit(&parser, "\x1b[35m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Magenta */
    emit(&parser, "\x1b[36m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");  /* Cyan */
    emit(&parser, "\x1b[37m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m");   /* White */
    emit(&parser, "\r\n");

    /* Bright 8 foreground colours */
    emit(&parser, "  Bright colours:    ");
    emit(&parser, "\x1b[90m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[91m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[92m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[93m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[94m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[95m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[96m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m ");
    emit(&parser, "\x1b[97m" "\xe2\x96\x88\xe2\x96\x88" "\x1b[0m");
    emit(&parser, "\r\n\r\n");

    /* Background colours */
    emit(&parser, "  Backgrounds:  ");
    emit(&parser, "\x1b[41m  \x1b[42m  \x1b[43m  \x1b[44m  ");
    emit(&parser, "\x1b[45m  \x1b[46m  \x1b[47m  \x1b[0m");
    emit(&parser, "\r\n\r\n");

    /* Text attributes */
    emit(&parser, "  Attributes:   ");
    emit(&parser, "\x1b[1m" "Bold" "\x1b[0m  ");
    if (has_lvb) {
        emit(&parser, "\x1b[4m" "Underline" "\x1b[0m  ");
    } else {
        emit(&parser, "(no underline)  ");
    }
    emit(&parser, "\x1b[7m" "Reverse" "\x1b[0m  ");
    emit(&parser, "\x1b[2m" "Dim" "\x1b[0m");
    emit(&parser, "\r\n\r\n");

    /* Combined attributes */
    emit(&parser, "  Combined:     ");
    emit(&parser, "\x1b[1;31m" "Bold Red" "\x1b[0m  ");
    emit(&parser, "\x1b[1;32;44m" "Bold Green on Blue" "\x1b[0m  ");
    emit(&parser, "\x1b[7;33m" "Reverse Yellow" "\x1b[0m");
    emit(&parser, "\r\n\r\n");

    /* 256-colour samples */
    emit(&parser, "  256-colour:   ");
    emit(&parser, "\x1b[38;5;196m" "Red-196" "\x1b[0m  ");
    emit(&parser, "\x1b[38;5;46m" "Green-46" "\x1b[0m  ");
    emit(&parser, "\x1b[38;5;21m" "Blue-21" "\x1b[0m  ");
    emit(&parser, "\x1b[38;5;208m" "Orange-208" "\x1b[0m  ");
    emit(&parser, "\x1b[38;5;13m" "Magenta-13" "\x1b[0m");
    emit(&parser, "\r\n\r\n");

    /* RGB colour samples */
    emit(&parser, "  RGB colour:   ");
    emit(&parser, "\x1b[38;2;255;0;0m" "Red" "\x1b[0m  ");
    emit(&parser, "\x1b[38;2;0;255;0m" "Green" "\x1b[0m  ");
    emit(&parser, "\x1b[38;2;0;0;255m" "Blue" "\x1b[0m  ");
    emit(&parser, "\x1b[38;2;255;165;0m" "Orange" "\x1b[0m  ");
    emit(&parser, "\x1b[38;2;128;0;128m" "Purple" "\x1b[0m");
    emit(&parser, "\r\n");
    emit(&parser, "  (quantised to nearest 16-colour match)");
    emit(&parser, "\r\n\r\n");

    /* Cursor movement demo */
    emit(&parser, "  Cursor test:  ");
    emit(&parser, "\x1b[s");           /* Save cursor */
    emit(&parser, ".....");
    emit(&parser, "\x1b[u");           /* Restore cursor */
    emit(&parser, "\x1b[32m");         /* Green */
    emit(&parser, "OVER");             /* Overwrite the dots */
    emit(&parser, "\x1b[0m");
    emit(&parser, ".");                /* Last dot stays */
    emit(&parser, "\r\n\r\n");

    /* Erase demo */
    emit(&parser, "  Erase test:   ");
    emit(&parser, "ABCDEFGHIJ");
    emit(&parser, "\x1b[5D");          /* Move left 5 */
    emit(&parser, "\x1b[K");           /* Erase to end of line */
    emit(&parser, " (FGHIJ erased)");
    emit(&parser, "\r\n\r\n");

    /* Box drawing (if console supports Unicode) */
    emit(&parser, "  Box drawing:  ");
    emit(&parser, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\r\n");
    emit(&parser, "                \xe2\x94\x82 ");
    emit(&parser, "\x1b[1;36m" "WinPat" "\x1b[0m");
    emit(&parser, " \xe2\x94\x82\r\n");
    emit(&parser, "                \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98");
    emit(&parser, "\r\n\r\n");

    /* Footer */
    emit(&parser, "  \x1b[90m" "Press any key to exit..." "\x1b[0m");

    /*====================================================================
     * Render to console
     *====================================================================*/

    wp_renderer_paint(&renderer);

    /* Wait for keypress */
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (hIn != INVALID_HANDLE_VALUE) {
        /* Flush input buffer */
        FlushConsoleInputBuffer(hIn);

        INPUT_RECORD ir;
        DWORD read;
        while (ReadConsoleInputW(hIn, &ir, 1, &read)) {
            if (ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown) {
                break;
            }
        }
    }

    /* Clean up */
    wp_renderer_destroy(&renderer);
    wp_screen_destroy(screen);

    return 0;
}
