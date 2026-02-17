#include <cstdio>
#include <cstring>
#include <windows.h>

static void write_text(const char* s)
{
    std::fputs(s, stdout);
    std::fflush(stdout);
}

static void print_banner(void)
{
    write_text("\x1b[2J\x1b[H");
    write_text("WinPatina Win2K Smoke Test\r\n");
    write_text("==========================\r\n\r\n");
    write_text("Expected:\r\n");
    write_text("1) Colored text below\r\n");
    write_text("2) Cursor movement and line overwrite\r\n");
    write_text("3) Program exits on Q/q\r\n\r\n");
}

static void print_colour_line(void)
{
    write_text("\x1b[31mRED\x1b[0m ");
    write_text("\x1b[32mGREEN\x1b[0m ");
    write_text("\x1b[33mYELLOW\x1b[0m ");
    write_text("\x1b[34mBLUE\x1b[0m ");
    write_text("\x1b[35mMAGENTA\x1b[0m ");
    write_text("\x1b[36mCYAN\x1b[0m\r\n");
}

static void run_progress(void)
{
    write_text("\r\nProgress demo:\r\n");
    write_text("[                    ] 0%\r\n");

    for (int i = 0; i <= 20; i++) {
        int pct = i * 5;
        char bar[64];
        char line[96];

        std::memset(bar, ' ', sizeof(bar));
        for (int j = 0; j < i; j++) {
            bar[j] = '#';
        }
        bar[20] = '\0';

        std::snprintf(line, sizeof(line),
            "\x1b[1A\r[%s] %d%%\r\n", bar, pct);
        write_text(line);
        Sleep(60);
    }
}

int main(void)
{
    print_banner();
    print_colour_line();
    run_progress();

    write_text("\r\nPress Q to quit...\r\n");

    while (true) {
        int ch = std::getchar();
        if (ch == EOF) {
            break;
        }
        if (ch == 'q' || ch == 'Q') {
            break;
        }
    }

    write_text("\x1b[0m\r\nDone.\r\n");
    return 0;
}
