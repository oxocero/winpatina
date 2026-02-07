#!/usr/bin/env python3
"""
WinPatina VT Stress Test

Exercises the full VT-to-Win32 translation pipeline:
  VT bytes -> parser -> dispatch -> screen buffer -> renderer

Run through WinPatina:
  winpatina-x64.exe --force-translate python.exe examples/stress_test.py

Tests cover:
  1. Basic text & UTF-8 / Unicode
  2. 16-colour ANSI SGR
  3. 256-colour palette
  4. 24-bit RGB true colour
  5. Text attributes (bold, dim, underline, reverse, etc.)
  6. Cursor movement (absolute & relative)
  7. Screen operations (erase, insert/delete lines)
  8. Scroll regions
  9. Rapid output (render throughput)
 10. Box-drawing & block elements
 11. Combined SGR stress
 12. Alternate screen buffer
"""

import sys
import time

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

ESC = "\x1b"
CSI = ESC + "["
OSC = ESC + "]"

def sgr(*codes):
    """Return an SGR (Select Graphic Rendition) sequence."""
    return CSI + ";".join(str(c) for c in codes) + "m"

def cup(row, col):
    """Cursor Position (1-based)."""
    return CSI + f"{row};{col}H"

def ed(n=2):
    """Erase in Display. 0=below, 1=above, 2=all."""
    return CSI + f"{n}J"

def el(n=0):
    """Erase in Line. 0=right, 1=left, 2=whole."""
    return CSI + f"{n}K"

def decstbm(top, bottom):
    """Set scroll region (1-based, inclusive)."""
    return CSI + f"{top};{bottom}r"

def fg256(n):
    return CSI + f"38;5;{n}m"

def bg256(n):
    return CSI + f"48;5;{n}m"

def fg_rgb(r, g, b):
    return CSI + f"38;2;{r};{g};{b}m"

def bg_rgb(r, g, b):
    return CSI + f"48;2;{r};{g};{b}m"

RESET = sgr(0)

def write(s):
    sys.stdout.write(s)
    sys.stdout.flush()

def pause(seconds=1.5):
    time.sleep(seconds)

def wait_key():
    """Prompt and wait for Enter."""
    write(sgr(90) + "  [press Enter to continue]" + RESET)
    sys.stdout.flush()
    try:
        input()
    except EOFError:
        time.sleep(2)

def header(title, section_num):
    """Display a section header."""
    write(ed(2) + cup(1, 1))
    write(sgr(1, 97) + f" {section_num}. {title} " + RESET)
    write(sgr(90) + "=" * (50 - len(title)) + RESET + "\n\n")

# ---------------------------------------------------------------------------
# Test sections
# ---------------------------------------------------------------------------

def test_basic_text():
    header("Basic Text & Unicode", 1)

    write("  Plain ASCII: The quick brown fox jumps over the lazy dog.\n")
    write("  Digits:      0123456789\n")
    write("  Symbols:     !@#$%^&*()_+-=[]{}|;':\",./<>?\n\n")

    write("  Latin ext:   cafe\u0301 na\u00efve r\u00e9sum\u00e9 \u00fc\u00f6\u00e4\u00df\n")
    write("  Cyrillic:    \u041f\u0440\u0438\u0432\u0435\u0442 \u043c\u0438\u0440!\n")
    write("  CJK:         \u4f60\u597d\u4e16\u754c \u3053\u3093\u306b\u3061\u306f\n")
    write("  Emoji:       (rendered as \ufffd in CHAR_INFO)\n")
    write("  Arrows:      \u2190 \u2191 \u2192 \u2193 \u2194 \u2195\n")
    write("  Math:        \u221a \u221e \u2248 \u2260 \u2264 \u2265 \u03c0 \u03a3\n\n")

    wait_key()


def test_16_colours():
    header("16-Colour ANSI SGR", 2)

    names = [
        "Black", "Red", "Green", "Yellow",
        "Blue", "Magenta", "Cyan", "White",
    ]

    # Normal foreground
    write("  Normal FG:  ")
    for i in range(8):
        write(sgr(30 + i) + f" {names[i][:3]} ")
    write(RESET + "\n")

    # Bright foreground
    write("  Bright FG:  ")
    for i in range(8):
        write(sgr(90 + i) + f" {names[i][:3]} ")
    write(RESET + "\n\n")

    # Normal background
    write("  Normal BG:  ")
    for i in range(8):
        write(sgr(40 + i, 97 if i < 4 else 30) + f" {names[i][:3]} ")
    write(RESET + "\n")

    # Bright background
    write("  Bright BG:  ")
    for i in range(8):
        write(sgr(100 + i, 30 if i > 3 else 97) + f" {names[i][:3]} ")
    write(RESET + "\n\n")

    # Colour matrix: FG x BG
    write("  FG\\BG ")
    for bg in range(8):
        write(f"  {bg}  ")
    write("\n")
    for fg in range(8):
        write(f"    {fg}   ")
        for bg in range(8):
            write(sgr(30 + fg, 40 + bg) + " Aa ")
        write(RESET + "\n")

    write("\n")
    wait_key()


def test_256_colours():
    header("256-Colour Palette", 3)

    # Standard 16
    write("  Standard (0-15):\n  ")
    for i in range(16):
        write(bg256(i) + "  ")
        if i == 7:
            write(RESET + "\n  ")
    write(RESET + "\n\n")

    # 6x6x6 colour cube (16-231)
    write("  Colour cube (16-231):\n")
    for row in range(6):
        write("  ")
        for g in range(6):
            for b in range(6):
                idx = 16 + row * 36 + g * 6 + b
                write(bg256(idx) + "  ")
            write(RESET + " ")
        write("\n")
    write("\n")

    # Greyscale ramp (232-255)
    write("  Greyscale (232-255):\n  ")
    for i in range(232, 256):
        write(bg256(i) + "  ")
    write(RESET + "\n\n")

    wait_key()


def test_rgb_colours():
    header("24-bit RGB True Colour", 4)

    # Horizontal gradient
    write("  Red gradient:\n  ")
    for i in range(0, 255, 4):
        write(bg_rgb(i, 0, 0) + " ")
    write(RESET + "\n")

    write("  Green gradient:\n  ")
    for i in range(0, 255, 4):
        write(bg_rgb(0, i, 0) + " ")
    write(RESET + "\n")

    write("  Blue gradient:\n  ")
    for i in range(0, 255, 4):
        write(bg_rgb(0, 0, i) + " ")
    write(RESET + "\n")

    write("  Rainbow:\n  ")
    for i in range(64):
        t = i / 64.0
        if t < 1/6:
            r, g, b = 255, int(t*6*255), 0
        elif t < 2/6:
            r, g, b = int((2/6-t)*6*255), 255, 0
        elif t < 3/6:
            r, g, b = 0, 255, int((t-2/6)*6*255)
        elif t < 4/6:
            r, g, b = 0, int((4/6-t)*6*255), 255
        elif t < 5/6:
            r, g, b = int((t-4/6)*6*255), 0, 255
        else:
            r, g, b = 255, 0, int((1-t)*6*255)
        write(bg_rgb(r, g, b) + " ")
    write(RESET + "\n\n")

    # Colour text
    write("  ")
    msg = "True colour text rendering"
    for i, ch in enumerate(msg):
        hue = i / len(msg)
        r = int(128 + 127 * __import__("math").sin(hue * 6.28))
        g = int(128 + 127 * __import__("math").sin(hue * 6.28 + 2.09))
        b = int(128 + 127 * __import__("math").sin(hue * 6.28 + 4.19))
        write(fg_rgb(r, g, b) + ch)
    write(RESET + "\n\n")

    wait_key()


def test_attributes():
    header("Text Attributes (SGR)", 5)

    attrs = [
        (1,  "Bold"),
        (2,  "Dim"),
        (3,  "Italic (may not render)"),
        (4,  "Underline"),
        (7,  "Reverse video"),
        (8,  "Hidden (invisible)"),
        (9,  "Strikethrough (may not render)"),
        (53, "Overline (may not render)"),
    ]

    for code, name in attrs:
        write(f"  {sgr(code)}{name}{RESET}  <-- SGR {code}\n")

    write("\n  Combinations:\n")
    write(f"  {sgr(1,4)}Bold + Underline{RESET}\n")
    write(f"  {sgr(1,7)}Bold + Reverse{RESET}\n")
    write(f"  {sgr(4,7)}Underline + Reverse{RESET}\n")
    write(f"  {sgr(1,4,7)}Bold + Underline + Reverse{RESET}\n")
    write(f"  {sgr(1,31)}Bold Red{RESET}  ")
    write(f"{sgr(1,32)}Bold Green{RESET}  ")
    write(f"{sgr(1,33)}Bold Yellow{RESET}\n")
    write(f"  {sgr(4,34)}Underline Blue{RESET}  ")
    write(f"{sgr(7,35)}Reverse Magenta{RESET}  ")
    write(f"{sgr(1,4,36)}Bold UL Cyan{RESET}\n\n")

    wait_key()


def test_cursor_movement():
    header("Cursor Movement", 6)

    # Absolute positioning
    write("  CUP (absolute positioning):\n")
    for i in range(5):
        write(cup(4 + i, 5 + i * 8) + sgr(31 + i) + f"({4+i},{5+i*8})" + RESET)
    write("\n\n\n\n\n\n")

    # Relative movement
    write("  Relative movement (CUU/CUD/CUF/CUB):\n")
    write("  Start")
    write(CSI + "3C")  # CUF 3 - forward 3
    write("-->Right3")
    write(CSI + "1B")  # CUD 1 - down 1
    write(CSI + "18D")  # CUB 18 - back 18
    write("Down1+Left")
    write(CSI + "1A")  # CUU 1 - up 1
    write(CSI + "15C")  # CUF 15 - forward 15
    write("Back up")
    write("\n\n")

    # Save and restore
    write("  Save/Restore cursor:\n")
    write("  Before save" + ESC + "7")  # DECSC
    write(cup(16, 30) + sgr(33) + "[jumped here]" + RESET)
    write(ESC + "8")  # DECRC
    write(" <-- restored\n\n")

    write("\n")
    wait_key()


def test_screen_ops():
    header("Screen Operations", 7)

    # Fill some lines then erase
    write("  Erase in Line (EL):\n")
    write("  XXXX This text survives XXXX")
    write(CSI + "29G")   # Move to column 29
    write(el(0))         # Erase from cursor to end
    write("\n")

    write("  XXXX" + CSI + "0K" + " <-- erased right of col 6\n")

    write("  Full line erase: XXXXXXXXXX")
    write("\r" + el(2) + "  Full line erase: (cleared and rewritten)\n\n")

    # Insert / delete lines
    write("  Insert/Delete Lines:\n")
    write("  Line A\n")
    write("  Line B\n")
    write("  Line C\n")
    write(CSI + "2A")   # Move up 2
    write(CSI + "1L")   # Insert 1 line
    write("  >> INSERTED LINE <<\n")
    write(CSI + "2B")   # Move down 2
    write("\n")

    # Insert / delete characters
    write("  Insert/Delete Characters:\n")
    write("  HelloWorld")
    write(CSI + "5D")     # Back 5
    write(CSI + "1@")     # ICH - insert 1 char
    write(" ")            # The space that was inserted
    write(CSI + "6C")     # Forward to end
    write(" <-- space inserted\n")

    write("  Hello World")
    write(CSI + "6D")     # Back 6
    write(CSI + "1P")     # DCH - delete 1 char
    write(CSI + "5C")     # Forward to end
    write(" <-- space deleted\n\n")

    wait_key()


def test_scroll_regions():
    header("Scroll Regions (DECSTBM)", 8)

    write("  Setting scroll region to rows 4-10, then scrolling:\n\n")

    # Draw a frame
    for row in range(4, 11):
        write(cup(row, 3) + sgr(90) + "|" + RESET)
        write(cup(row, 60) + sgr(90) + "|" + RESET)
    write(cup(3, 3) + sgr(90) + "+" + "-" * 56 + "+" + RESET)
    write(cup(11, 3) + sgr(90) + "+" + "-" * 56 + "+" + RESET)

    # Set scroll region
    write(decstbm(4, 10))

    # Write lines that cause scrolling within the region
    for i in range(12):
        write(cup(10, 5))
        colour = 31 + (i % 6)
        write(sgr(colour) + f"  Scrolling line {i+1:2d} -- content stays in region  " + RESET)
        if i < 11:
            write("\n")
        time.sleep(0.15)

    # Reset scroll region
    write(decstbm(1, 999))

    write(cup(13, 1))
    write("  Text outside the region should be unaffected.\n\n")

    wait_key()


def test_rapid_output():
    header("Rapid Output (Throughput Stress)", 9)

    write("  Filling screen with coloured text as fast as possible...\n\n")
    time.sleep(0.5)

    start = time.time()
    total_bytes = 0

    for iteration in range(3):
        for row in range(3, 24):
            line = ""
            for col in range(0, 78, 6):
                colour = 31 + ((row + col + iteration) % 6)
                line += f"\x1b[{colour}m"
                line += f"{row:02d},{col:02d} "
            line += RESET
            write(cup(row, 1) + line)
            total_bytes += len(line)

    elapsed = time.time() - start
    rate = total_bytes / elapsed / 1024 if elapsed > 0 else 0

    write(cup(24, 1) + RESET)
    write(f"  {total_bytes:,} bytes in {elapsed:.2f}s = {rate:.0f} KB/s\n\n")

    wait_key()


def test_box_drawing():
    header("Box Drawing & Block Elements", 10)

    # Single-line box
    write("  Single-line box:\n")
    write("  \u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n")
    write("  \u2502  Box drawing OK!  \u2502\n")
    write("  \u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n\n")

    # Double-line box
    write("  Double-line box:\n")
    write("  \u2554\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2557\n")
    write("  \u2551  Double borders!  \u2551\n")
    write("  \u255a\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u255d\n\n")

    # Block elements
    write("  Block elements:\n")
    write("  Shades: \u2591\u2591\u2592\u2592\u2593\u2593\u2588\u2588  ")
    write("Half: \u2580\u2584\u258c\u2590  ")
    write("Quad: \u2596\u2597\u2598\u259d\n\n")

    # Table
    write("  Table:\n")
    write("  \u250c\u2500\u2500\u2500\u2500\u2500\u2500\u252c\u2500\u2500\u2500\u2500\u2500\u2500\u252c\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n")
    write("  \u2502 " + sgr(1) + "Col1" + RESET + " \u2502 " + sgr(1) + "Col2" + RESET + " \u2502 " + sgr(1) + "Col3" + RESET + " \u2502\n")
    write("  \u251c\u2500\u2500\u2500\u2500\u2500\u2500\u253c\u2500\u2500\u2500\u2500\u2500\u2500\u253c\u2500\u2500\u2500\u2500\u2500\u2500\u2524\n")
    write("  \u2502  A1  \u2502  B1  \u2502  C1  \u2502\n")
    write("  \u2502  A2  \u2502  B2  \u2502  C2  \u2502\n")
    write("  \u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2534\u2500\u2500\u2500\u2500\u2500\u2500\u2534\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n\n")

    wait_key()


def test_sgr_combos():
    header("Combined SGR Stress", 11)

    write("  Every FG colour with bold:\n  ")
    for i in range(8):
        write(sgr(1, 30 + i) + f" C{i} ")
    write(RESET + "\n  ")
    for i in range(8):
        write(sgr(1, 90 + i) + f" B{i} ")
    write(RESET + "\n\n")

    write("  Every FG colour with underline:\n  ")
    for i in range(8):
        write(sgr(4, 30 + i) + f" C{i} ")
    write(RESET + "\n  ")
    for i in range(8):
        write(sgr(4, 90 + i) + f" B{i} ")
    write(RESET + "\n\n")

    write("  Every FG colour with reverse:\n  ")
    for i in range(8):
        write(sgr(7, 30 + i) + f" C{i} ")
    write(RESET + "\n  ")
    for i in range(8):
        write(sgr(7, 90 + i) + f" B{i} ")
    write(RESET + "\n\n")

    write("  256-col FG + bold + underline:\n  ")
    for i in range(0, 255, 16):
        write(sgr(1, 4) + fg256(i) + "\u2588\u2588")
    write(RESET + "\n\n")

    write("  RGB FG + reverse:\n  ")
    import math
    for i in range(40):
        t = i / 40.0
        r = int(128 + 127 * math.sin(t * 6.28))
        g = int(128 + 127 * math.sin(t * 6.28 + 2.09))
        b = int(128 + 127 * math.sin(t * 6.28 + 4.19))
        write(sgr(7) + fg_rgb(r, g, b) + "\u2588\u2588")
    write(RESET + "\n\n")

    # Rapid SGR changes
    write("  Rapid attribute toggling (500 changes):\n  ")
    start = time.time()
    for i in range(500):
        attr = 1 + (i % 7)
        col = 31 + (i % 6)
        write(sgr(attr, col) + "X")
    elapsed = time.time() - start
    write(RESET + f"\n  500 SGR changes in {elapsed*1000:.0f}ms\n\n")

    wait_key()


def test_alt_screen():
    header("Alternate Screen Buffer", 12)

    write("  This text is on the MAIN screen.\n")
    write("  Switching to alternate buffer in 2 seconds...\n")
    time.sleep(2)

    # Enter alternate screen
    write(CSI + "?1049h")
    write(ed(2) + cup(1, 1))

    write(sgr(1, 92) + "  === ALTERNATE SCREEN BUFFER ===" + RESET + "\n\n")
    write("  You are now on the alternate screen.\n")
    write("  The main screen content is preserved.\n\n")

    # Draw something distinctive
    for row in range(6, 18):
        write(cup(row, 10))
        for col in range(50):
            r = int(128 + 127 * __import__("math").sin((row + col) * 0.2))
            g = int(128 + 127 * __import__("math").sin((row + col) * 0.2 + 2))
            b = int(128 + 127 * __import__("math").sin((row + col) * 0.2 + 4))
            write(bg_rgb(r, g, b) + " ")
        write(RESET)

    write(cup(20, 1))
    write("  Returning to main screen in 3 seconds...\n")
    time.sleep(3)

    # Leave alternate screen
    write(CSI + "?1049l")

    write("\n  Back on the main screen. Previous content should be intact.\n\n")

    wait_key()


def test_window_title():
    header("Window Title (OSC 2)", 13)

    original = "WinPatina Stress Test"
    write(f"  Setting window title to various strings...\n\n")

    titles = [
        "WinPatina Test - Hello!",
        "WinPatina Test - Unicode: \u00e9\u00e8\u00ea\u00eb",
        "WinPatina Test - 12345",
        original,
    ]

    for title in titles:
        write(OSC + "2;" + title + "\x07")  # BEL terminates OSC
        write(f"  Title set to: {title}\n")
        time.sleep(1)

    write("\n")
    wait_key()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Ensure output is unbuffered
    sys.stdout.reconfigure(write_through=True) if hasattr(sys.stdout, 'reconfigure') else None

    write(ed(2) + cup(1, 1))
    write(sgr(1, 96) + "  WinPatina VT Stress Test" + RESET + "\n")
    write(sgr(90) + "  ========================" + RESET + "\n\n")
    write("  This programme exercises the full VT translation pipeline.\n")
    write("  Each section tests a different category of VT sequences.\n\n")

    write("  Note: 256-colour and RGB tests show quantised results\n")
    write("  (mapped to 16 colours) when running through WinPatina's\n")
    write("  Win32 translation layer. This is expected behaviour.\n\n")

    wait_key()

    tests = [
        test_basic_text,
        test_16_colours,
        test_256_colours,
        test_rgb_colours,
        test_attributes,
        test_cursor_movement,
        test_screen_ops,
        test_scroll_regions,
        test_rapid_output,
        test_box_drawing,
        test_sgr_combos,
        test_alt_screen,
        test_window_title,
    ]

    for test_fn in tests:
        try:
            test_fn()
        except Exception as e:
            write(RESET + f"\n  ERROR in {test_fn.__name__}: {e}\n\n")
            wait_key()

    # Final summary
    write(ed(2) + cup(1, 1))
    write(sgr(1, 92) + "  All tests complete!" + RESET + "\n\n")
    write(f"  Ran {len(tests)} test sections covering:\n")
    write("    - Basic text & Unicode\n")
    write("    - 16 / 256 / RGB colours\n")
    write("    - Text attributes & SGR combinations\n")
    write("    - Cursor movement & save/restore\n")
    write("    - Screen erase, insert/delete lines & chars\n")
    write("    - Scroll regions\n")
    write("    - Rapid output throughput\n")
    write("    - Box drawing & block elements\n")
    write("    - Alternate screen buffer\n")
    write("    - Window title (OSC 2)\n\n")

    write(sgr(90) + "  Press Enter to exit." + RESET + "\n")
    try:
        input()
    except EOFError:
        pass

if __name__ == "__main__":
    main()
