# TUI Framework Terminal Size Detection on Windows

How major TUI frameworks detect terminal dimensions on Windows, and whether
they work through WinPatina's pipe-based VT interception.

## The Problem

WinPatina redirects the child's stdout through an anonymous pipe so it can
intercept VT sequences. This means `GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE))`
fails in the child because the handle is a pipe, not a console buffer.

WinPatina provides three alternative size mechanisms:

| Mechanism                  | How it works                                      |
|----------------------------|---------------------------------------------------|
| `COLUMNS`/`LINES` env vars | Set in the child's environment at spawn time      |
| `CONOUT$`                  | Always opens the active console buffer directly   |
| `CSI 18 t` VT query        | WinPatina responds with `CSI 8;rows;cols t`       |

## Framework Comparison

| Framework | Language | Primary mechanism | Checks COLUMNS/LINES? | Opens CONOUT$? | VT query? | Fallback | Works through WinPatina? |
|-----------|----------|-------------------|------------------------|----------------|-----------|----------|--------------------------|
| **Ratatui** (crossterm) | Rust | `GetConsoleScreenBufferInfo` | No | **Yes** | No | None | **Yes** (CONOUT$ works) |
| **Textual** (Rich) | Python | `os.get_terminal_size(stdout)` | **Yes** (override) | No | No | Try stderr, then env, then 80x25 | **Yes** (env vars work) |
| **Ink** (terminal-size) | JS/Node | `process.stdout.columns` (libuv) | **Yes** (fallback) | No | No | Try stderr, then env, then 80x24 | **Yes** (env vars work) |
| **ncurses** (Unix) | C | `ioctl(TIOCGWINSZ)` | **Yes** (highest priority) | N/A | No | terminfo, then 24x80 | **Yes** (env vars work) |
| **Notcurses** | C | `GetConsoleScreenBufferInfo` | **Yes** (defaults) | No (GetStdHandle) | CSI 18 t | env vars, then 24x80 | **Yes** (env vars + CSI 18 t) |
| **PDCurses** (wincon) | C | `GetConsoleScreenBufferInfo` | No | No (GetStdHandle) | No | None | **No** |
| **Bubbletea** | Go | `GetConsoleScreenBufferInfo` | No | No (GetStdHandle) | No | None | **No** |
| **Termina** | C | `GetConsoleScreenBufferInfo` | No | No (GetStdHandle) | No | 80x24 | **No** |

## Details by Framework

### Ratatui / Crossterm (Rust) — WORKS

Crossterm opens `CONOUT$` via `CreateFileW("CONOUT$", ...)` rather than using
`GetStdHandle(STD_OUTPUT_HANDLE)`. This always refers to the active console
screen buffer, even when stdout is redirected to a pipe.

No env var fallback, no VT query — but it doesn't need them because `CONOUT$`
just works.

### Textual / Rich (Python) — WORKS

Rich's `Console.size` tries `os.get_terminal_size()` on stdout first (which
calls `GetConsoleScreenBufferInfo` via CPython's C implementation). If that
fails (pipe), it tries stderr. After the fd loop, it checks `COLUMNS`/`LINES`
env vars as an override. Falls back to 80x25.

Note: Rich checks env vars *after* the system call (as override), unlike
`shutil.get_terminal_size()` which checks them *first* (as primary).

### Ink / terminal-size (JS/Node) — WORKS

Node.js uses libuv's `uv_tty_get_winsize` which calls
`GetConsoleScreenBufferInfo`. When stdout is a pipe, `process.stdout.columns`
is undefined. Ink falls back to the `terminal-size` package which tries:

1. `process.stdout.columns` (fails on pipe)
2. `process.stderr.columns` (may work if stderr is still a TTY)
3. `COLUMNS`/`LINES` env vars
4. `tput` (Git Bash only on Windows)
5. Hardcoded 80x24

### ncurses (Unix) — WORKS

Checks `LINES`/`COLUMNS` env vars first (highest priority, unless
`use_env(FALSE)` is called). Then tries `ioctl(TIOCGWINSZ)`. Then terminfo
database. Then hardcoded 24x80.

### Notcurses (C) — WORKS

Uses `GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE))` as primary.
Falls back to `default_rows`/`default_cols` which are set from `LINES`/`COLUMNS`
env vars (then terminfo, then 24x80). Also sends `CSI 18 t` as part of initial
terminal queries.

### PDCurses wincon (C) — BROKEN

Uses `GetConsoleScreenBufferInfo` on `GetStdHandle(STD_OUTPUT_HANDLE)` as the
sole mechanism. No env var check, no `CONOUT$` fallback, no VT query.
If the call fails, there is no recovery.

**Possible fixes** (in PDCurses):
- Fall back to `CreateFileW("CONOUT$", ...)` when `GetStdHandle` returns a pipe
- Check `COLUMNS`/`LINES` env vars

### Bubbletea (Go) — BROKEN

Uses `GetConsoleScreenBufferInfo` on `os.Stdout`'s fd (= `STD_OUTPUT_HANDLE`).
No env var fallback, no `CONOUT$` fallback, no VT query. Also uses
`WINDOW_BUFFER_SIZE_EVENT` from console input for resize events.

**Possible fixes** (in Bubbletea / charmbracelet/x/term):
- Open `CONOUT$` instead of using `os.Stdout` for size detection
- Check `COLUMNS`/`LINES` env vars as fallback

### Termina (C) — BROKEN

Uses `GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE))` as the sole
mechanism. Falls back to hardcoded 80x24.

**Possible fixes** (in Termina):
- Fall back to `CreateFileW("CONOUT$", ...)` when stdout is a pipe
- Check `COLUMNS`/`LINES` env vars

## Key Insight

The gold standard approach on Windows is **opening `CONOUT$` directly** (as
crossterm does). This always works regardless of whether stdout has been
redirected. The next best approach is checking `COLUMNS`/`LINES` env vars as
a fallback.

Frameworks that exclusively rely on `GetStdHandle(STD_OUTPUT_HANDLE)` with no
fallback break through any pipe-based intermediary — not just WinPatina, but
also any multiplexer, logger, or wrapper that redirects stdout.

## What WinPatina Provides

All three mechanisms are implemented and working:

- `COLUMNS`/`LINES` set in child environment at spawn time
- Console buffer properly sized before child is resumed (CREATE_SUSPENDED)
  so `CONOUT$` queries return correct dimensions
- `CSI 18 t` terminal size query response (`CSI 8;rows;cols t`)
