#!/usr/bin/env python3
"""
WinPatina VT Stress Test — Comprehensive Edition

Exercises the full VT-to-Win32 translation pipeline:
  VT bytes -> parser -> dispatch -> screen buffer -> renderer

Run through WinPatina:
  winpatina-x64.exe --force-translate python.exe examples/stress_test.py

Tests cover:
   1. Basic text & ASCII
   2. UTF-8 multi-byte & edge cases
   3. Wide characters (CJK)
   4. 16-colour ANSI SGR
   5. 256-colour palette
   6. 24-bit RGB true colour
   7. Colour quantisation boundaries
   8. Text attributes (bold, dim, underline, reverse, etc.)
   9. SGR selective disable codes
  10. SGR bold vs dim conflict
  11. SGR extended colour edge cases
  12. Combined SGR stress
  13. Attribute persistence across operations
  14. Cursor movement (absolute & relative)
  15. Cursor boundary conditions
  16. Cursor save/restore edge cases
  17. Tab stops (HT)
  18. C0 control characters
  19. CSI parameter edge cases
  20. Malformed & invalid sequences
  21. Line wrap & pending wrap
  22. Screen erase operations (ED/EL/ECH)
  23. Insert/delete characters (ICH/DCH)
  24. Insert/delete lines (IL/DL)
  25. Scroll regions (DECSTBM)
  26. Scroll region advanced edge cases
  27. Reverse index (RI)
  28. Alternate screen buffer
  29. Alternate buffer advanced edge cases
  30. Private modes (DECSET/DECRST)
  31. Device status report (DSR)
  32. Repeat character (REP)
  33. Window title (OSC 2)
  34. OSC edge cases
  35. DCS/SOS/PM/APC sequences
  36. Box drawing & block elements
  37. Full-screen application simulation
  38. Rapid output (throughput stress)
  39. Mixed sequence interleaving
  40. Burst & fragmentation stress
"""

import sys
import time
import math
import os

# Force UTF-8 output — Python defaults to the ANSI codepage when stdout
# is a pipe (which is the case under WinPatina's translation layer).
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

ESC = "\x1b"
CSI = ESC + "["
OSC = ESC + "]"
DCS = ESC + "P"
SOS = ESC + "X"
PM  = ESC + "^"
APC = ESC + "_"
ST  = ESC + "\\"   # String Terminator
SS3 = ESC + "O"
BEL = "\x07"
BS  = "\x08"
HT  = "\x09"
LF  = "\x0a"
VT  = "\x0b"
FF  = "\x0c"
CR  = "\x0d"
CAN = "\x18"
SUB = "\x1a"

def sgr(*codes):
    """Return an SGR (Select Graphic Rendition) sequence."""
    return CSI + ";".join(str(c) for c in codes) + "m"

def cup(row, col):
    """Cursor Position (1-based)."""
    return CSI + f"{row};{col}H"

def ed(n=2):
    """Erase in Display. 0=below, 1=above, 2=all, 3=scrollback."""
    return CSI + f"{n}J"

def el(n=0):
    """Erase in Line. 0=right, 1=left, 2=whole."""
    return CSI + f"{n}K"

def ech(n=1):
    """Erase Characters."""
    return CSI + f"{n}X"

def decstbm(top, bottom):
    """Set scroll region (1-based, inclusive)."""
    return CSI + f"{top};{bottom}r"

def cuu(n=1):
    """Cursor Up."""
    return CSI + f"{n}A"

def cud(n=1):
    """Cursor Down."""
    return CSI + f"{n}B"

def cuf(n=1):
    """Cursor Forward."""
    return CSI + f"{n}C"

def cub(n=1):
    """Cursor Back."""
    return CSI + f"{n}D"

def cnl(n=1):
    """Cursor Next Line."""
    return CSI + f"{n}E"

def cpl(n=1):
    """Cursor Previous Line."""
    return CSI + f"{n}F"

def cha(n=1):
    """Cursor Horizontal Absolute (1-based column)."""
    return CSI + f"{n}G"

def vpa(n=1):
    """Vertical Position Absolute (1-based row)."""
    return CSI + f"{n}d"

def ich(n=1):
    """Insert Characters."""
    return CSI + f"{n}@"

def dch(n=1):
    """Delete Characters."""
    return CSI + f"{n}P"

def il(n=1):
    """Insert Lines."""
    return CSI + f"{n}L"

def dl(n=1):
    """Delete Lines."""
    return CSI + f"{n}M"

def su(n=1):
    """Scroll Up."""
    return CSI + f"{n}S"

def sd(n=1):
    """Scroll Down."""
    return CSI + f"{n}T"

def rep(n=1):
    """Repeat preceding graphic character."""
    return CSI + f"{n}b"

def fg256(n):
    return CSI + f"38;5;{n}m"

def bg256(n):
    return CSI + f"48;5;{n}m"

def fg_rgb(r, g, b):
    return CSI + f"38;2;{r};{g};{b}m"

def bg_rgb(r, g, b):
    return CSI + f"48;2;{r};{g};{b}m"

def decset(mode):
    """Enable private mode."""
    return CSI + f"?{mode}h"

def decrst(mode):
    """Disable private mode."""
    return CSI + f"?{mode}l"

RESET = sgr(0)

DECSC = ESC + "7"   # Save cursor
DECRC = ESC + "8"   # Restore cursor
RI    = ESC + "M"   # Reverse Index

def write(s):
    sys.stdout.write(s)
    sys.stdout.flush()

def write_raw(b):
    """Write raw bytes to stdout."""
    sys.stdout.buffer.write(b)
    sys.stdout.buffer.flush()

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
    pad = max(1, 50 - len(title))
    write(sgr(1, 97) + f" {section_num}. {title} " + RESET)
    write(sgr(90) + "=" * pad + RESET + "\n\n")

def ok(msg):
    write(sgr(32) + "  \u2713 " + RESET + msg + "\n")

def info(msg):
    write("  " + msg + "\n")


# ---------------------------------------------------------------------------
# 1. Basic Text & ASCII
# ---------------------------------------------------------------------------

def test_basic_text():
    header("Basic Text & ASCII", 1)

    info("Plain ASCII: The quick brown fox jumps over the lazy dog.")
    info("Digits:      0123456789")
    info("Symbols:     !@#$%^&*()_+-=[]{}|;':\",./<>?")
    write("\n")

    # Every printable ASCII character (0x20-0x7E)
    info("Full printable ASCII range (0x20-0x7E):")
    write("  ")
    for i in range(0x20, 0x7F):
        write(chr(i))
    write("\n\n")

    # Empty line, single character
    info("Single character: X")
    info("Empty line above this one:")
    write("\n")
    info("Line after empty line.")
    write("\n")

    wait_key()


# ---------------------------------------------------------------------------
# 2. UTF-8 Multi-byte & Edge Cases
# ---------------------------------------------------------------------------

def test_utf8_edge_cases():
    header("UTF-8 Multi-byte & Edge Cases", 2)

    # 2-byte sequences (U+0080 - U+07FF)
    info("2-byte UTF-8 (Latin Extended, Cyrillic, etc.):")
    info("  Latin ext:   caf\u00e9 na\u00efve r\u00e9sum\u00e9 \u00fc\u00f6\u00e4\u00df")
    info("  Cyrillic:    \u041f\u0440\u0438\u0432\u0435\u0442 \u043c\u0438\u0440!")
    info("  Greek:       \u0391\u03b1 \u0392\u03b2 \u0393\u03b3 \u0394\u03b4 \u03c0 \u03a3")
    info("  Hebrew:      \u05e9\u05dc\u05d5\u05dd")
    info("  Arabic:      \u0645\u0631\u062d\u0628\u0627")
    write("\n")

    # 3-byte sequences (U+0800 - U+FFFF)
    info("3-byte UTF-8 (CJK, symbols, BMP edge):")
    info("  CJK:         \u4f60\u597d\u4e16\u754c \u3053\u3093\u306b\u3061\u306f")
    info("  Korean:      \ud55c\uad6d\uc5b4")
    info("  Arrows:      \u2190 \u2191 \u2192 \u2193 \u2194 \u2195 \u21d0 \u21d2")
    info("  Math:        \u221a \u221e \u2248 \u2260 \u2264 \u2265 \u222b \u2202")
    info("  Dingbats:    \u2714 \u2718 \u2764 \u2605 \u2606 \u266a \u266b")
    info("  Currency:    \u20ac \u00a3 \u00a5 \u20a3 \u20b9 \u20bf")
    write("\n")

    # BMP boundary: U+FFFD (replacement character), U+FFFE, U+FFFF
    info("BMP boundary codepoints:")
    info("  U+FFFD (replacement): \ufffd")
    info("  U+FEFF (BOM/ZWNBSP): [\ufeff] (zero-width, invisible)")
    write("\n")

    # 4-byte sequences (U+10000+) — rendered as U+FFFD on Win32 CHAR_INFO
    info("4-byte UTF-8 (supplementary plane, shown as \ufffd):")
    info("  Emoji:       \U0001f600 \U0001f60d \U0001f4a9 \U0001f680 \U0001f3b5")
    info("  Math:        \U0001d400 \U0001d401 \U0001d402 (math bold)")
    write("\n")

    # Combining characters
    info("Combining characters:")
    info("  e\u0301 (e + combining acute) = \u00e9")
    info("  a\u0308 (a + combining diaeresis) = \u00e4")
    info("  o\u0302\u0323 (o + circumflex + dot below)")
    info("  n\u0303 (n + combining tilde) = \u00f1")
    write("\n")

    # Zero-width characters
    info("Zero-width characters:")
    info("  ZWJ: A\u200dB (should appear as AB)")
    info("  ZWNJ: A\u200cB (should appear as AB)")
    info("  ZWSP: A\u200bB (should appear as AB)")
    write("\n")

    # Write raw UTF-8 bytes to test the parser's accumulator
    info("Raw UTF-8 byte sequences (testing parser accumulator):")
    write("  Euro sign (\u20ac): ")
    write_raw(b"\xe2\x82\xac")      # U+20AC in UTF-8
    write("\n")
    write("  CJK char (\u4e16): ")
    write_raw(b"\xe4\xb8\x96")      # U+4E16 in UTF-8
    write("\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 3. Wide Characters (CJK)
# ---------------------------------------------------------------------------

def test_wide_characters():
    header("Wide Characters (CJK)", 3)

    info("CJK characters occupy 2 cells each:")
    write("\n")

    # Grid alignment test — each CJK character should be 2 cells wide
    info("Alignment test (numbers mark cell positions):")
    write("  1234567890123456789012345678901234567890\n")
    write("  ")
    for ch in "\u4e00\u4e8c\u4e09\u56db\u4e94\u516d\u4e03\u516b\u4e5d\u5341":
        write(ch)
    write("\n")
    info("  Each character above should align to 2 columns.")
    write("\n")

    # Mixed ASCII and CJK
    info("Mixed ASCII and CJK:")
    write("  Hello\u4f60\u597dWorld\u4e16\u754c!\n\n")

    # CJK at right edge — should wrap if not enough space
    info("CJK near right edge (wrapping test):")
    write("  " + "X" * 37 + "\u4f60\n")
    write("  " + "X" * 38 + "\u4f60\n")
    write("\n")

    # Full-width punctuation
    info("Full-width forms:")
    info("  \uff01\uff1f\uff08\uff09\uff1a\uff1b\u3001\u3002\u300c\u300d\u3010\u3011")
    write("\n")

    # Katakana and Hiragana
    info("Japanese kana:")
    info("  Hiragana: \u3042\u3044\u3046\u3048\u304a\u304b\u304d\u304f\u3051\u3053")
    info("  Katakana: \u30a2\u30a4\u30a6\u30a8\u30aa\u30ab\u30ad\u30af\u30b1\u30b3")
    write("\n")

    wait_key()


# ---------------------------------------------------------------------------
# 4. 16-Colour ANSI SGR
# ---------------------------------------------------------------------------

def test_16_colours():
    header("16-Colour ANSI SGR", 4)

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

    # Full colour matrix: FG x BG (all 64 combinations)
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

    # Default colour codes
    info("Default colour reset:")
    write("  " + sgr(31) + "Red" + sgr(39) + " <-- SGR 39 (default FG)\n")
    write("  " + sgr(42) + "GrnBG" + sgr(49) + " <-- SGR 49 (default BG)\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 5. 256-Colour Palette
# ---------------------------------------------------------------------------

def test_256_colours():
    header("256-Colour Palette", 5)

    # Standard 16
    info("Standard (0-15):")
    write("  ")
    for i in range(16):
        write(bg256(i) + "  ")
        if i == 7:
            write(RESET + "\n  ")
    write(RESET + "\n\n")

    # 6x6x6 colour cube (16-231)
    info("Colour cube (16-231):")
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
    info("Greyscale (232-255):")
    write("  ")
    for i in range(232, 256):
        write(bg256(i) + "  ")
    write(RESET + "\n\n")

    # Boundary indices
    info("Boundary indices:")
    write("  Index 0:   " + bg256(0) + "  " + RESET + "  ")
    write("Index 15:  " + bg256(15) + "  " + RESET + "  ")
    write("Index 16:  " + bg256(16) + "  " + RESET + "\n")
    write("  Index 231: " + bg256(231) + "  " + RESET + "  ")
    write("Index 232: " + bg256(232) + "  " + RESET + "  ")
    write("Index 255: " + bg256(255) + "  " + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 6. 24-bit RGB True Colour
# ---------------------------------------------------------------------------

def test_rgb_colours():
    header("24-bit RGB True Colour", 6)

    # Horizontal gradients
    info("Red gradient:")
    write("  ")
    for i in range(0, 255, 4):
        write(bg_rgb(i, 0, 0) + " ")
    write(RESET + "\n")

    info("Green gradient:")
    write("  ")
    for i in range(0, 255, 4):
        write(bg_rgb(0, i, 0) + " ")
    write(RESET + "\n")

    info("Blue gradient:")
    write("  ")
    for i in range(0, 255, 4):
        write(bg_rgb(0, 0, i) + " ")
    write(RESET + "\n")

    info("Rainbow:")
    write("  ")
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
        r = int(128 + 127 * math.sin(hue * 6.28))
        g = int(128 + 127 * math.sin(hue * 6.28 + 2.09))
        b = int(128 + 127 * math.sin(hue * 6.28 + 4.19))
        write(fg_rgb(r, g, b) + ch)
    write(RESET + "\n\n")

    # RGB boundary values
    info("RGB boundary values:")
    write("  (0,0,0):     " + bg_rgb(0, 0, 0) + "  " + RESET + "  ")
    write("(255,255,255): " + bg_rgb(255, 255, 255) + "  " + RESET + "\n")
    write("  (255,0,0):   " + bg_rgb(255, 0, 0) + "  " + RESET + "  ")
    write("(0,255,0):     " + bg_rgb(0, 255, 0) + "  " + RESET + "  ")
    write("(0,0,255):     " + bg_rgb(0, 0, 255) + "  " + RESET + "\n")
    write("  (128,128,128):" + bg_rgb(128, 128, 128) + "  " + RESET + "  ")
    write("(1,1,1):       " + bg_rgb(1, 1, 1) + "  " + RESET + "  ")
    write("(254,254,254): " + bg_rgb(254, 254, 254) + "  " + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 7. Colour Quantisation Boundaries
# ---------------------------------------------------------------------------

def test_colour_quantisation():
    header("Colour Quantisation Boundaries", 7)

    info("When translated to Win32, colours are quantised to 16.")
    info("These tests exercise boundary colours in the mapping.\n")

    # All 256 colours as foreground text
    info("All 256 palette colours as FG (quantised to 16):")
    for row in range(16):
        write("  ")
        for col in range(16):
            idx = row * 16 + col
            write(fg256(idx) + f"{idx:3d} ")
        write(RESET + "\n")
    write("\n")

    # Greyscale ramp — each should map to nearest of ANSI 0,8,7,15
    info("Greyscale quantisation (232-255 mapped to 16):")
    write("  ")
    for i in range(232, 256):
        grey = 8 + (i - 232) * 10
        write(bg256(i) + fg256(0 if grey > 128 else 15) + f"{grey:3d}")
    write(RESET + "\n\n")

    # RGB corner colours — should map to nearest ANSI primary
    info("RGB cube corners (should map to ANSI primaries):")
    corners = [
        ((0,0,0), "Black"), ((255,0,0), "Red"), ((0,255,0), "Green"),
        ((255,255,0), "Yellow"), ((0,0,255), "Blue"), ((255,0,255), "Magenta"),
        ((0,255,255), "Cyan"), ((255,255,255), "White"),
    ]
    for (r, g, b), name in corners:
        write(f"  ({r:3d},{g:3d},{b:3d}) {name:8s}: ")
        write(bg_rgb(r, g, b) + "    " + RESET + "\n")
    write("\n")

    # Mid-range off-palette colours
    info("Off-palette mid-range colours:")
    mid_colours = [
        (128, 64, 32), (64, 128, 64), (32, 64, 128),
        (200, 100, 50), (100, 200, 100), (50, 100, 200),
        (192, 192, 0), (0, 192, 192), (192, 0, 192),
    ]
    write("  ")
    for r, g, b in mid_colours:
        write(bg_rgb(r, g, b) + f" {r},{g},{b} ")
    write(RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 8. Text Attributes (SGR)
# ---------------------------------------------------------------------------

def test_attributes():
    header("Text Attributes (SGR)", 8)

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
    write(f"  {sgr(2,4)}Dim + Underline{RESET}\n")
    write(f"  {sgr(2,7)}Dim + Reverse{RESET}\n")
    write(f"  {sgr(1,4,7,31)}Bold+UL+Rev+Red{RESET}  ")
    write(f"{sgr(2,4,32)}Dim+UL+Green{RESET}  ")
    write(f"{sgr(7,4,33)}Rev+UL+Yellow{RESET}\n")
    write(f"  {sgr(1,31)}Bold Red{RESET}  ")
    write(f"{sgr(1,32)}Bold Green{RESET}  ")
    write(f"{sgr(1,33)}Bold Yellow{RESET}\n")
    write(f"  {sgr(4,34)}Underline Blue{RESET}  ")
    write(f"{sgr(7,35)}Reverse Magenta{RESET}  ")
    write(f"{sgr(1,4,36)}Bold UL Cyan{RESET}\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 9. SGR Selective Disable Codes
# ---------------------------------------------------------------------------

def test_sgr_selective_disable():
    header("SGR Selective Disable Codes", 9)

    info("Testing individual attribute disable codes:\n")

    # SGR 22: normal intensity (clear bold AND dim)
    write("  " + sgr(1) + "Bold text" + sgr(22) + " <-- SGR 22 clears bold" + RESET + "\n")
    write("  " + sgr(2) + "Dim text" + sgr(22) + " <-- SGR 22 clears dim" + RESET + "\n")

    # SGR 23: not italic
    write("  " + sgr(3) + "Italic text" + sgr(23) + " <-- SGR 23 clears italic" + RESET + "\n")

    # SGR 24: not underline
    write("  " + sgr(4) + "Underline text" + sgr(24) + " <-- SGR 24 clears underline" + RESET + "\n")

    # SGR 27: not reversed
    write("  " + sgr(7) + "Reverse text" + sgr(27) + " <-- SGR 27 clears reverse" + RESET + "\n")

    # SGR 28: not hidden
    write("  " + sgr(8) + "Hidden text" + sgr(28) + " <-- SGR 28 reveals hidden" + RESET + "\n")

    # SGR 55: not overline
    write("  " + sgr(53) + "Overline text" + sgr(55) + " <-- SGR 55 clears overline" + RESET + "\n")
    write("\n")

    # Selective disable preserving other attributes
    info("Selective disable preserves other attributes:")
    write("  " + sgr(1, 4, 31) + "Bold+UL+Red")
    write(sgr(24) + " <-- UL off, bold+red remain")
    write(sgr(22) + " <-- bold off, red remains" + RESET + "\n")
    write("  " + sgr(1, 7, 32) + "Bold+Rev+Grn")
    write(sgr(27) + " <-- rev off, bold+green remain" + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 10. SGR Bold vs Dim Conflict
# ---------------------------------------------------------------------------

def test_sgr_bold_dim_conflict():
    header("SGR Bold vs Dim Conflict", 10)

    info("Bold (SGR 1) and dim (SGR 2) are mutually exclusive.\n")

    write("  " + sgr(1) + "Bold" + RESET + " -> ")
    write(sgr(1, 2) + "Bold then Dim (dim wins)" + RESET + "\n")

    write("  " + sgr(2) + "Dim" + RESET + " -> ")
    write(sgr(2, 1) + "Dim then Bold (bold wins)" + RESET + "\n")

    write("  " + sgr(1) + "Start bold" + sgr(2) + " -> dim replaces" + RESET + "\n")
    write("  " + sgr(2) + "Start dim" + sgr(1) + " -> bold replaces" + RESET + "\n\n")

    # SGR 22 clears both
    write("  " + sgr(1) + "Bold" + sgr(22) + " -> normal (22 clears)" + RESET + "\n")
    write("  " + sgr(2) + "Dim" + sgr(22) + " -> normal (22 clears)" + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 11. SGR Extended Colour Edge Cases
# ---------------------------------------------------------------------------

def test_sgr_extended_colours():
    header("SGR Extended Colour Edge Cases", 11)

    # Standard semicolon form
    info("Standard form (semicolons):")
    write("  FG 256: " + CSI + "38;5;196m" + "Red (196)" + RESET + "  ")
    write("BG 256: " + CSI + "48;5;21m" + "Blue BG (21)" + RESET + "\n")
    write("  FG RGB: " + CSI + "38;2;255;128;0m" + "Orange" + RESET + "  ")
    write("BG RGB: " + CSI + "48;2;0;128;255m" + "Sky BG" + RESET + "\n\n")

    # Colon sub-parameter form (ISO 8613-6)
    info("Colon sub-parameter form (ISO 8613-6):")
    write("  FG 256: " + CSI + "38:5:196m" + "Red (196)" + RESET + "  ")
    write("BG 256: " + CSI + "48:5:21m" + "Blue BG (21)" + RESET + "\n")
    write("  FG RGB: " + CSI + "38:2:255:128:0m" + "Orange" + RESET + "  ")
    write("BG RGB: " + CSI + "48:2:0:128:255m" + "Sky BG" + RESET + "\n\n")

    # Boundary colour indices
    info("256-colour boundary indices:")
    write("  Index 0:   " + fg256(0) + bg256(15) + "BLK" + RESET + "  ")
    write("Index 15:  " + fg256(15) + bg256(0) + "WHT" + RESET + "  ")
    write("Index 16:  " + fg256(16) + bg256(15) + "Cube start" + RESET + "\n")
    write("  Index 231: " + fg256(231) + bg256(0) + "Cube end" + RESET + "  ")
    write("Index 232: " + fg256(232) + bg256(15) + "Grey start" + RESET + "  ")
    write("Index 255: " + fg256(255) + bg256(0) + "Grey end" + RESET + "\n\n")

    # Mixed SGR with extended colours in one sequence
    info("Mixed SGR with extended colours:")
    write("  " + CSI + "1;38;5;196;4m" + "Bold+Red256+UL" + RESET + "\n")
    write("  " + CSI + "7;48;2;0;100;200m" + "Rev+RGB BG" + RESET + "\n")
    write("  " + CSI + "1;38;2;255;0;0;48;2;0;0;255m" + "Bold+RedFG+BlueBG" + RESET + "\n\n")

    # SGR 38/48 with missing parameters
    info("Extended colour with edge-case params:")
    write("  SGR 38;5 (no index): " + CSI + "38;5m" + "default?" + RESET + "\n")
    write("  SGR 38;2;255 (partial RGB): " + CSI + "38;2;255m" + "partial?" + RESET + "\n")
    write("  SGR 38;2;255;0 (partial RGB): " + CSI + "38;2;255;0m" + "partial?" + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 12. Combined SGR Stress
# ---------------------------------------------------------------------------

def test_sgr_combos():
    header("Combined SGR Stress", 12)

    info("Every FG colour with bold:")
    write("  ")
    for i in range(8):
        write(sgr(1, 30 + i) + f" C{i} ")
    write(RESET + "\n  ")
    for i in range(8):
        write(sgr(1, 90 + i) + f" B{i} ")
    write(RESET + "\n\n")

    info("Every FG colour with underline:")
    write("  ")
    for i in range(8):
        write(sgr(4, 30 + i) + f" C{i} ")
    write(RESET + "\n  ")
    for i in range(8):
        write(sgr(4, 90 + i) + f" B{i} ")
    write(RESET + "\n\n")

    info("Every FG colour with reverse:")
    write("  ")
    for i in range(8):
        write(sgr(7, 30 + i) + f" C{i} ")
    write(RESET + "\n  ")
    for i in range(8):
        write(sgr(7, 90 + i) + f" B{i} ")
    write(RESET + "\n\n")

    info("256-col FG + bold + underline:")
    write("  ")
    for i in range(0, 255, 16):
        write(sgr(1, 4) + fg256(i) + "\u2588\u2588")
    write(RESET + "\n\n")

    info("RGB FG + reverse:")
    write("  ")
    for i in range(40):
        t = i / 40.0
        r = int(128 + 127 * math.sin(t * 6.28))
        g = int(128 + 127 * math.sin(t * 6.28 + 2.09))
        b = int(128 + 127 * math.sin(t * 6.28 + 4.19))
        write(sgr(7) + fg_rgb(r, g, b) + "\u2588\u2588")
    write(RESET + "\n\n")

    # Rapid SGR changes
    info("Rapid attribute toggling (1000 changes):")
    write("  ")
    start = time.time()
    for i in range(1000):
        attr = 1 + (i % 7)
        col = 31 + (i % 6)
        write(sgr(attr, col) + "X")
    elapsed = time.time() - start
    write(RESET + f"\n  1000 SGR changes in {elapsed*1000:.0f}ms\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 13. Attribute Persistence Across Operations
# ---------------------------------------------------------------------------

def test_attribute_persistence():
    header("Attribute Persistence Across Operations", 13)

    info("Attributes should persist across cursor movement:\n")

    # Set colour, move cursor, write — colour should persist
    write("  " + sgr(1, 31) + "Red bold start")
    write(cuf(3) + "...still red bold" + RESET + "\n")

    write("  " + sgr(4, 32) + "Green UL start")
    write(cup(5, 40) + "jumped here, still green UL" + RESET + "\n")
    write(cup(6, 1))

    # Attributes after save/restore
    info("Attributes saved with DECSC and restored with DECRC:")
    write("  " + sgr(1, 33) + "Yellow bold" + DECSC)
    write(sgr(0, 34) + " <-- changed to blue")
    write(DECRC + " <-- restored to yellow bold" + RESET + "\n\n")

    # Erase should use current attributes (not default)
    info("Erase uses current SGR attributes:")
    write("  " + sgr(44) + "Blue BG set, erasing right: ")
    write(el(0) + RESET + "\n")
    write("  " + sgr(41) + "Red BG set, erasing line: ")
    write(el(2) + RESET + "\n\n")

    info("(Erased areas should show the set background colour.)\n")

    wait_key()


# ---------------------------------------------------------------------------
# 14. Cursor Movement (Absolute & Relative)
# ---------------------------------------------------------------------------

def test_cursor_movement():
    header("Cursor Movement (Absolute & Relative)", 14)

    # Absolute positioning — CUP
    info("CUP (absolute positioning):")
    for i in range(5):
        write(cup(4 + i, 5 + i * 8) + sgr(31 + i) + f"({4+i},{5+i*8})" + RESET)
    write("\n\n\n\n\n\n")

    # Relative movement
    info("Relative movement (CUU/CUD/CUF/CUB):")
    write("  Start")
    write(cuf(3) + "-->Right3")
    write(cud(1) + cub(18) + "Down1+Left")
    write(cuu(1) + cuf(15) + "Back up")
    write("\n\n")

    # CNL and CPL
    info("CNL (Cursor Next Line) and CPL (Cursor Previous Line):")
    write("  Start here" + cnl(2) + "Two lines down, col 1")
    write(cpl(1) + "One line back up, col 1\n\n")

    # CHA (Cursor Horizontal Absolute)
    info("CHA (column absolute):")
    write("  " + "." * 40 + "\r")
    write(cha(5) + "5")
    write(cha(15) + "15")
    write(cha(25) + "25")
    write(cha(35) + "35")
    write("\n\n")

    # VPA (Vertical Position Absolute)
    info("VPA (row absolute):")
    row_base = 18
    write(cup(row_base, 3) + "Line A")
    write(cup(row_base + 1, 3) + "Line B")
    write(cup(row_base + 2, 3) + "Line C")
    write(vpa(row_base) + cuf(10) + sgr(33) + "<-- VPA back to A" + RESET)
    write(cup(row_base + 3, 1) + "\n")

    # HVP (same as CUP but with 'f' final)
    info("HVP (Horizontal and Vertical Position, 'f' final):")
    write(CSI + "22;5f" + sgr(36) + "HVP(22,5)" + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 15. Cursor Boundary Conditions
# ---------------------------------------------------------------------------

def test_cursor_boundaries():
    header("Cursor Boundary Conditions", 15)

    info("CUP with out-of-bounds coordinates (should clamp):\n")

    # CUP to (0,0) — should become (1,1) since it's 1-based
    write(cup(0, 0) + "  CUP(0,0) -> clamped to top-left\n")

    # CUP to (999,999) — should clamp to bottom-right
    write(cup(999, 999))
    # We can't easily verify position, but it shouldn't crash
    write(cup(4, 1) + "  CUP(999,999) -> clamped to bottom-right (no crash)\n")

    # CUP with explicit 0 parameters — treated as 1
    write(cup(5, 1) + "  CUP(0,5) -> " + CSI + "0;5H" + "here (row 0 -> 1)\n")

    # CUP with no parameters — default to (1,1)
    write(CSI + "H")
    write("  CUP with no params -> home position (1,1)\n")
    write(cup(7, 1))

    # Relative movement beyond bounds
    info("Relative movement beyond screen edges:")
    write(cup(8, 1) + "  CUU(999) from row 8: ")
    write(cuu(999))
    write("clamped to top" + cup(9, 1) + "\n")

    write(cup(10, 1) + "  CUB(999) from col 1: ")
    write(cub(999))
    write("clamped to left edge\n")

    write(cup(11, 1) + "  CUF(999) from col 1: ")
    write(cuf(999))
    write("X" + cup(12, 1))  # X should appear at right edge
    write("  (X at right edge above)\n")

    write(cup(13, 1) + "  CUD(999) from here: ")
    write(cud(999))
    write("clamped to bottom" + cup(14, 1) + "\n")

    # CHA boundary
    write(cup(15, 1) + "  CHA(0) -> " + cha(0) + "col 1 (0 clamped)")
    write(cup(16, 1) + "  CHA(999) -> " + cha(999) + "clamped right\n")

    write(cup(18, 1))
    write("\n")
    wait_key()


# ---------------------------------------------------------------------------
# 16. Cursor Save/Restore Edge Cases
# ---------------------------------------------------------------------------

def test_cursor_save_restore():
    header("Cursor Save/Restore Edge Cases", 16)

    # Basic DECSC/DECRC
    info("DECSC/DECRC (ESC 7 / ESC 8):")
    write("  Position A" + DECSC)
    write(cup(5, 30) + sgr(33) + "[jumped to B]" + RESET)
    write(DECRC + " <-- back to A\n\n")

    # CSI s / CSI u (ANSI save/restore)
    info("CSI s / CSI u (ANSI save/restore):")
    write("  Position C" + CSI + "s")
    write(cup(8, 30) + sgr(36) + "[jumped to D]" + RESET)
    write(CSI + "u" + " <-- back to C\n\n")

    # Save at one position, move around, restore
    info("Complex save/restore:")
    write(cup(10, 5) + "Start")
    write(DECSC)
    write(cup(11, 10) + "Move 1")
    write(cup(12, 15) + "Move 2")
    write(cup(13, 20) + "Move 3")
    write(DECRC + sgr(92) + " <-- restored to Start" + RESET + "\n")

    # Save with attributes, restore should bring them back
    write(cup(15, 1))
    info("Save/restore preserves SGR attributes:")
    write("  " + sgr(1, 4, 31) + "Bold+UL+Red" + DECSC + RESET)
    write("  Normal text here")
    write(DECRC + " restored attr" + RESET + "\n")
    info("  (Text after restore should be bold+underline+red)")
    write("\n")

    # Double save — second save overwrites first
    info("Double save (second overwrites first):")
    write(cup(19, 5) + "Pos1" + DECSC)
    write(cup(19, 20) + "Pos2" + DECSC)
    write(cup(19, 40) + "Pos3")
    write(DECRC + sgr(93) + " <-- back to Pos2 (not Pos1)" + RESET + "\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 17. Tab Stops (HT)
# ---------------------------------------------------------------------------

def test_tab_stops():
    header("Tab Stops (HT)", 17)

    info("Default tab stops at every 8 columns:")
    info("Ruler (every 10):")
    write("  ")
    for i in range(1, 9):
        write(f"{i*10:10d}")
    write("\n")
    write("  ")
    for i in range(80):
        write("|" if (i + 1) % 10 == 0 else (":" if (i + 1) % 5 == 0 else "."))
    write("\n\n")

    # Tab from various positions
    info("Tab from column 1:")
    write("  X" + HT + "Y (X at 1, tab -> Y at 9)\n")

    info("Tab from column 7:")
    write("  " + "." * 5 + "X" + HT + "Y (X at 7, tab -> Y at 9)\n")

    info("Tab from column 8:")
    write("  " + "." * 6 + "X" + HT + "Y (X at 8, tab -> Y at 9)\n")

    info("Tab from column 9:")
    write("  " + "." * 7 + "X" + HT + "Y (X at 9, tab -> Y at 17)\n")
    write("\n")

    # Multiple consecutive tabs
    info("Multiple consecutive tabs:")
    write("  A" + HT + "B" + HT + "C" + HT + "D" + HT + "E\n")
    write("  1       9       17      25      33\n\n")

    # Tab near right edge — should clamp
    info("Tab near right edge (should clamp to last column):")
    write("  " + "X" * 72 + HT + "|\n")
    info("  (The | should appear at or near the right edge.)\n")

    wait_key()


# ---------------------------------------------------------------------------
# 18. C0 Control Characters
# ---------------------------------------------------------------------------

def test_c0_controls():
    header("C0 Control Characters", 18)

    # Backspace (BS, 0x08)
    info("BS (backspace, 0x08):")
    write("  ABC" + BS + "X -> ABX (C overwritten)\n")
    write("  " + BS + "X -> X (BS at col 0 is no-op)\n\n")

    # Carriage return (CR, 0x0D)
    info("CR (carriage return, 0x0D):")
    write("  OVERWRITE_ME" + CR + "  CR+Rewrite -> ")
    write("CR+Rewrite\n\n")

    # Line feed variants
    info("LF/VT/FF (all treated as line feed):")
    write("  LF:" + LF + "  after LF")
    write(LF)
    write("  VT:" + VT + "  after VT")
    write(VT)
    write("  FF:" + FF + "  after FF\n\n")

    # CR + LF sequence
    info("CR+LF (Windows-style newline):")
    write("  Line 1" + CR + LF + "  Line 2 (after CRLF)\n\n")

    # BEL (should be ignored, no visible effect)
    info("BEL (0x07, should be silent/ignored):")
    write("  Before BEL" + BEL + " After BEL (no visible change)\n\n")

    # NUL and other C0 codes (should be ignored)
    info("NUL and misc C0 (should be ignored):")
    write("  Before\x00 NUL\x01 SOH\x02 STX\x03 ETX After\n")
    info("  (Should read: Before NUL SOH STX ETX After)\n")

    wait_key()


# ---------------------------------------------------------------------------
# 19. CSI Parameter Edge Cases
# ---------------------------------------------------------------------------

def test_csi_param_edge_cases():
    header("CSI Parameter Edge Cases", 19)

    info("CSI with no parameters (defaults):\n")

    # CSI m with no params = SGR 0 (reset)
    write("  " + sgr(31) + "Red text" + CSI + "m" + " <- CSI m = SGR 0 (reset)\n")

    # CSI H with no params = CUP(1,1)
    write(cup(5, 1) + "  CSI H (no params) moves cursor home:")
    write(CSI + "H" + " <- cursor at (1,1)")
    write(cup(6, 1) + "\n")

    # CSI J with no params = ED 0 (erase below)
    info("CSI J (no params) = ED 0 (erase cursor to end)")
    write("\n")

    # Empty parameters (semicolons with no digits)
    info("Empty parameters (leading/trailing semicolons):")
    write("  CSI ;H -> " + CSI + ";H" + "cursor at (1,1) (defaults)\n")
    write(cup(10, 1))
    write("  CSI 5;H -> " + CSI + "5;H" + "cursor at (5,default=1)\n")
    write(cup(11, 1))
    write("  CSI ;10H -> " + CSI + ";10H" + "cursor at (1,10)\n")
    write(cup(12, 1) + "\n")

    # Trailing semicolons
    info("Trailing semicolons:")
    write("  SGR 31; -> " + CSI + "31;m" + "red (trailing ; ignored)" + RESET + "\n")
    write("  SGR ;31; -> " + CSI + ";31;m" + "red (leading+trailing ;)" + RESET + "\n")
    write("  SGR ;;; -> " + CSI + ";;;m" + "reset (all empty = defaults)" + RESET + "\n\n")

    # Very large parameter (should cap at 65535 or similar)
    info("Large parameter values:")
    write("  CUP(99999,1) -> " + CSI + "99999;1H")
    write("clamped to bottom\n")
    write(cup(16, 1))
    write("  SGR 38;5;999 -> " + CSI + "38;5;999m" + "out of range (ignored?)" + RESET + "\n")
    write("  CUF(99999) -> " + cuf(99999) + "X")
    write(cup(17, 1) + "  (X at right edge above)\n\n")

    # Zero parameters (explicit 0 should default to 1 for CUP etc.)
    info("Explicit zero (should default to 1 for movement):")
    write("  CUP(0,0) -> ")
    write(CSI + "0;0H" + "at (1,1)")
    write(cup(19, 1))
    write("  CUU(0) -> treated as CUU(1)\n")
    write("  CUF(0) -> treated as CUF(1)\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 20. Malformed & Invalid Sequences
# ---------------------------------------------------------------------------

def test_malformed_sequences():
    header("Malformed & Invalid Sequences", 20)

    info("The parser must handle invalid sequences gracefully.\n")

    # ESC interrupting a CSI sequence
    info("ESC interrupting CSI (should reset parser):")
    write("  Before" + CSI + "31")  # Start CSI sequence
    write(ESC + "[32m")             # ESC starts new CSI
    write("Green (not red)" + RESET + "\n")

    # CAN (0x18) cancelling a sequence
    info("CAN (0x18) cancelling CSI sequence:")
    write("  Before" + CSI + "31" + CAN + sgr(33) + "Yellow (CAN cancelled red)" + RESET + "\n")

    # SUB (0x1A) cancelling a sequence
    info("SUB (0x1A) cancelling CSI sequence:")
    write("  Before" + CSI + "31" + SUB + sgr(34) + "Blue (SUB cancelled red)" + RESET + "\n\n")

    # Multiple consecutive ESCs
    info("Multiple consecutive ESCs:")
    write("  " + ESC + ESC + ESC + "[32mGreen after 3 ESCs" + RESET + "\n")

    # CSI with invalid intermediate triggering CSI_IGNORE
    info("CSI with colon in entry (triggers CSI_IGNORE):")
    write("  " + CSI + ":1m" + "After ignored sequence (should be normal)\n")

    # Private marker after parameters (triggers CSI_IGNORE)
    info("Private marker after params (invalid, triggers CSI_IGNORE):")
    write("  " + CSI + "5?25h" + "After ignored sequence\n")

    # Unknown CSI final byte (should be dispatched but no-op)
    info("Unknown CSI final byte:")
    write("  " + CSI + "99z" + "After unknown CSI z\n")
    write("  " + CSI + "1;2;3~" + "After CSI ~ (function key?)\n\n")

    # Very long parameter list (> 16 params — MAX_PARAMS)
    info("Exceeding MAX_PARAMS (16):")
    params = ";".join(str(i) for i in range(20))
    write("  " + CSI + params + "m" + "After 20-param SGR" + RESET + "\n")

    # Incomplete ESC sequence followed by printable
    info("Incomplete ESC sequence:")
    write("  " + ESC + "XAfter ESC X (SOS string, consumed)" + "\n")
    write("  " + ESC + "After lone ESC at end of line\n\n")

    info("(No crashes = parser handles edge cases correctly.)\n")

    wait_key()


# ---------------------------------------------------------------------------
# 21. Line Wrap & Pending Wrap
# ---------------------------------------------------------------------------

def test_line_wrap():
    header("Line Wrap & Pending Wrap", 21)

    info("Deferred (pending) wrap at right margin:\n")

    # Write exactly to the right edge — cursor stays at last column
    # with pending_wrap set, not yet wrapped
    info("Writing to right margin (80 chars on 80-col terminal):")
    write("  ")
    # Fill 78 chars (2 for indent) to reach edge
    for i in range(78):
        write(str(i % 10))
    write("\n")
    info("  (Last digit should be at the right edge, no premature wrap.)\n")

    # Pending wrap + next char wraps to new line
    info("Pending wrap then next character wraps:")
    write("  " + "A" * 78)
    write("B")  # This should wrap to next line
    write(" <-- B wrapped to new line\n\n")

    # Pending wrap cleared by CR
    info("CR clears pending wrap:")
    write("  " + "X" * 78)
    write(CR + "  CR returned to column 0, no wrap\n\n")

    # Pending wrap cleared by cursor movement
    info("Cursor movement clears pending wrap:")
    write("  " + "Y" * 78)
    write(cub(5) + sgr(31) + "HERE" + RESET)
    write(cuf(10))  # Move past edge
    write("\n  (CUB after edge clears pending wrap, no extra line)\n\n")

    # Wrap at screen bottom triggers scroll
    info("Wrap at screen bottom (triggers scroll):")
    write("  Writing at last row, filling to edge...")
    time.sleep(0.3)
    # This is hard to test exactly without knowing screen size,
    # but the output should scroll rather than crash
    write("\n  If you see this, scrolling worked.\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 22. Screen Erase Operations (ED/EL/ECH)
# ---------------------------------------------------------------------------

def test_erase_operations():
    header("Screen Erase Operations (ED/EL/ECH)", 22)

    # ----- EL (Erase in Line) -----
    info("EL (Erase in Line):")
    write("\n")

    # EL 0 — erase from cursor to end of line
    write("  AAAAABBBBB" + cub(5) + el(0) + cuf(5) + " <- EL 0 erased BBBBB\n")

    # EL 1 — erase from start of line to cursor
    write("  AAAAABBBBB" + cub(5) + el(1) + " <- EL 1 erased AAAAA+cursor\n")

    # EL 2 — erase entire line
    write("  XXXXXXXXXXX" + CR + el(2) + "  EL 2 cleared entire line\n\n")

    # ----- ED (Erase in Display) -----
    info("ED (Erase in Display):")
    write("\n")

    # Fill some content, then ED 0 (erase below)
    write("  Line above ED\n")
    write("  ED 0 clears from cursor down" + ed(0))
    write(cup(12, 1))
    info("  (Everything below 'ED 0' line was erased.)")
    write("\n")

    # ED 1 (erase above) — more disruptive, skip visual test
    info("  ED 1 (erase above cursor) — acknowledged, visual test skipped.")
    write("\n")

    # ED 3 (erase scrollback) — if supported
    info("  ED 3 (erase scrollback) — acknowledged, no visual effect.")
    write("\n")

    # ----- ECH (Erase Characters) -----
    info("ECH (Erase Characters):")
    write("  ABCDEFGHIJ" + cub(7) + ech(3) + cuf(3) + " <- ECH 3 erased DEF\n")

    # ECH beyond line end — should clamp
    write("  SHORT" + cub(3) + ech(999) + cuf(3) + " <- ECH 999 (clamped to line end)\n")

    # ECH 0 — should be no-op or erase 1
    write("  TEST" + cub(2) + ech(0) + cuf(2) + " <- ECH 0\n\n")

    # Erase at boundaries
    info("Erase at boundaries:")
    write("  " + cup(20, 1) + "X" * 40 + cha(1) + el(0) + "  EL 0 from col 1 (clear entire line)\n")
    write("  " + "Y" * 40 + el(1) + " <- EL 1 at end of text\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 23. Insert/Delete Characters (ICH/DCH)
# ---------------------------------------------------------------------------

def test_insert_delete_chars():
    header("Insert/Delete Characters (ICH/DCH)", 23)

    # ----- ICH (Insert Characters) -----
    info("ICH (Insert Characters):")
    write("\n")

    # Basic insert
    write("  HelloWorld" + cub(5) + ich(1) + " " + cuf(4))
    write(" <- ICH 1 inserted space\n")

    # Insert multiple
    write("  ABCDEFGHIJ" + cub(7) + ich(3) + "123" + cuf(4))
    write(" <- ICH 3, wrote 123\n")

    # Insert at column 0
    write("  ORIGINAL" + cha(3) + ich(5) + "XXXXX" + cuf(20))
    write(" <- ICH 5 at col 3\n")

    # Insert more than available columns — chars pushed off right edge
    write("  SHORT" + cha(3) + ich(70))
    write(cha(75) + "|\n")
    write("  (Above: ICH 70 pushed content off-screen)\n\n")

    # ----- DCH (Delete Characters) -----
    info("DCH (Delete Characters):")
    write("\n")

    # Basic delete
    write("  Hello World" + cub(6) + dch(1) + cuf(5))
    write(" <- DCH 1 deleted space\n")

    # Delete multiple
    write("  ABCDEFGHIJ" + cub(7) + dch(3) + cuf(4))
    write(" <- DCH 3 removed DEF\n")

    # Delete at end — should be no-op
    write("  END_TEST" + dch(1))
    write(" <- DCH at end (content unchanged)\n")

    # Delete more than available — clamps
    write("  CLAMPTEST" + cub(4) + dch(999))
    write(cuf(10) + " <- DCH 999 (clamped)\n\n")

    # ICH/DCH with 0 count
    info("ICH/DCH with 0 count:")
    write("  TEST" + cub(2) + ich(0) + cuf(2) + " <- ICH 0\n")
    write("  TEST" + cub(2) + dch(0) + cuf(2) + " <- DCH 0\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 24. Insert/Delete Lines (IL/DL)
# ---------------------------------------------------------------------------

def test_insert_delete_lines():
    header("Insert/Delete Lines (IL/DL)", 24)

    info("IL (Insert Lines):")
    write("  Line A\n")
    write("  Line B\n")
    write("  Line C\n")
    write("  Line D\n")
    write("  Line E\n")
    write(cuu(3))  # Move up to Line C
    write(il(2))   # Insert 2 blank lines
    write("  >> Inserted 1 <<\n")
    write("  >> Inserted 2 <<\n")
    write(cud(3))  # Skip past D, E
    write("\n")

    info("DL (Delete Lines):")
    write("  Keep A\n")
    write("  DELETE ME 1\n")
    write("  DELETE ME 2\n")
    write("  Keep B\n")
    write("  Keep C\n")
    write(cuu(3))  # Move to DELETE ME 2
    write(cuu(1))  # Move to DELETE ME 1
    write(dl(2))   # Delete 2 lines
    write(cud(2))  # Move past Keep B, C
    write("\n")

    # IL with large count
    info("IL with large count (clamped to available rows):")
    write("  Before IL(999)\n")
    write(il(999))
    write("  After IL(999) — pushed everything down\n")
    write(cud(3))
    write("\n")

    # DL with count 0
    info("DL(0) — should be no-op or delete 1:")
    write("  Unchanged line\n")
    write(cuu(1) + dl(0) + cud(1))
    write("\n")

    wait_key()


# ---------------------------------------------------------------------------
# 25. Scroll Regions (DECSTBM)
# ---------------------------------------------------------------------------

def test_scroll_regions():
    header("Scroll Regions (DECSTBM)", 25)

    info("Setting scroll region to rows 4-10, then scrolling:\n")

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
    info("Text outside the region should be unaffected.")
    info("The frame borders should remain intact.\n")

    wait_key()


# ---------------------------------------------------------------------------
# 26. Scroll Region Advanced Edge Cases
# ---------------------------------------------------------------------------

def test_scroll_region_advanced():
    header("Scroll Region Advanced Edge Cases", 26)

    # Single-row scroll region
    info("Single-row scroll region (rows 5-5):")
    write(cup(5, 1) + "  Original content on row 5")
    write(decstbm(5, 5))
    write(cup(5, 1) + "  LF in single-row region: ")
    write(LF)  # Should clear the row (scroll replaces)
    write(cup(5, 30) + "After LF")
    write(decstbm(1, 999))
    write(cup(6, 1) + "\n")

    # Scroll region does not affect content outside
    info("Content outside scroll region preserved:")
    write(cup(8, 3) + sgr(1, 33) + "ABOVE REGION (should stay)" + RESET)
    write(cup(14, 3) + sgr(1, 33) + "BELOW REGION (should stay)" + RESET)
    write(decstbm(10, 13))
    for i in range(6):
        write(cup(13, 5))
        write(sgr(32) + f"  Region line {i+1}" + RESET + LF)
        time.sleep(0.1)
    write(decstbm(1, 999))
    write(cup(15, 1))
    info("  Lines at rows 8 and 14 should remain.\n")

    # DECSTBM resets cursor to home
    info("DECSTBM resets cursor to (1,1):")
    write(cup(17, 20) + "Before DECSTBM")
    write(decstbm(3, 20))
    write("Home" + RESET)
    write(decstbm(1, 999))
    write(cup(18, 1))
    info("  'Home' should appear at top-left.\n")

    # DECSTBM with no params — reset to full screen
    info("DECSTBM with no params (CSI r) — full screen:")
    write("  " + CSI + "r" + "Scroll region reset to full screen.\n\n")

    # SU/SD (scroll up/down) within region
    info("SU/SD (explicit scroll up/down) within region:")
    write(decstbm(21, 23))
    write(cup(21, 3) + "Row A")
    write(cup(22, 3) + "Row B")
    write(cup(23, 3) + "Row C")
    write(su(1))  # Scroll up — A gone, B->21, C->22, blank at 23
    write(cup(23, 3) + sgr(36) + "New row" + RESET)
    write(decstbm(1, 999))
    write(cup(24, 1) + "\n")

    wait_key()


# ---------------------------------------------------------------------------
# 27. Reverse Index (RI)
# ---------------------------------------------------------------------------

def test_reverse_index():
    header("Reverse Index (RI = ESC M)", 27)

    info("RI moves cursor up one row. At top of scroll region,")
    info("it scrolls the region down (blank line at top).\n")

    # RI from middle — just moves up
    write(cup(7, 5) + "Line below")
    write(cup(8, 5) + "Start here")
    write(RI + " <- RI moved cursor up (should be next to 'Line below')")
    write(cup(10, 1) + "\n")

    # RI at top of scroll region — scroll down
    info("RI at top of scroll region (scrolls down):")
    write(decstbm(13, 17))
    write(cup(13, 3) + "Top row A")
    write(cup(14, 3) + "Row B")
    write(cup(15, 3) + "Row C")
    write(cup(16, 3) + "Row D")
    write(cup(17, 3) + "Bottom row E")
    write(cup(13, 3))  # Move to top of region
    write(RI)           # Scroll down — blank at top, E pushed out
    write(cup(13, 3) + sgr(92) + "New top (RI scrolled down)" + RESET)
    write(decstbm(1, 999))
    write(cup(19, 1))
    info("  'Top row A' should have moved to row 14.\n")

    # Multiple RIs
    info("Multiple consecutive RIs:")
    write(cup(21, 5) + "Bottom" + RI + RI + RI + sgr(33) + "Up 3" + RESET)
    write(cup(22, 1) + "\n")

    wait_key()


# ---------------------------------------------------------------------------
# 28. Alternate Screen Buffer
# ---------------------------------------------------------------------------

def test_alt_screen():
    header("Alternate Screen Buffer", 28)

    info("This text is on the MAIN screen.")
    info("Switching to alternate buffer in 2 seconds...")
    time.sleep(2)

    # Enter alternate screen
    write(decset(1049))
    write(ed(2) + cup(1, 1))

    write(sgr(1, 92) + "  === ALTERNATE SCREEN BUFFER ===" + RESET + "\n\n")
    info("You are now on the alternate screen.")
    info("The main screen content is preserved.\n")

    # Draw something distinctive
    for row in range(6, 18):
        write(cup(row, 10))
        for col in range(50):
            r = int(128 + 127 * math.sin((row + col) * 0.2))
            g = int(128 + 127 * math.sin((row + col) * 0.2 + 2))
            b = int(128 + 127 * math.sin((row + col) * 0.2 + 4))
            write(bg_rgb(r, g, b) + " ")
        write(RESET)

    write(cup(20, 1))
    info("Returning to main screen in 3 seconds...")
    time.sleep(3)

    # Leave alternate screen
    write(decrst(1049))

    write("\n  Back on the main screen. Previous content should be intact.\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 29. Alternate Buffer Advanced Edge Cases
# ---------------------------------------------------------------------------

def test_alt_screen_advanced():
    header("Alternate Buffer Advanced Edge Cases", 29)

    # Enter alternate, draw, re-enter (should clear)
    info("Double enter (re-entering clears alternate):")
    write(decset(1049))
    write(ed(2) + cup(1, 1))
    write("  First entry content AAAA\n")
    time.sleep(0.5)
    write(decset(1049))  # Second enter — should clear
    write(cup(1, 1))
    write("  Second entry (first content cleared?)\n")
    time.sleep(1)
    write(decrst(1049))
    write("  Back to main after double-enter test.\n\n")

    # Alternate buffer with cursor positioning
    info("Cursor preserved across buffer switch:")
    write(cup(7, 1) + "  Main cursor at row 7" + DECSC)
    write(decset(1049))
    write(ed(2) + cup(3, 5) + "  Alt cursor at (3,5)")
    time.sleep(1)
    write(decrst(1049))
    write(DECRC + " <- main cursor restored?\n\n")

    # Alternate buffer with scroll region
    info("Scroll region in alternate buffer:")
    write(decset(1049))
    write(ed(2) + cup(1, 1))
    write("  Top (outside region)\n")
    write(decstbm(3, 8))
    for i in range(10):
        write(cup(8, 3) + sgr(31 + (i % 6)) + f"Alt region line {i}" + RESET + LF)
        time.sleep(0.1)
    write(decstbm(1, 999))
    write(cup(1, 1))
    info("  Top line should be preserved.")
    time.sleep(1.5)
    write(decrst(1049))
    write("  Back from alt+scroll test.\n\n")

    # Leave without entering — should be no-op
    info("Leave without entering (no-op):")
    write(decrst(1049))
    write("  decrst(1049) without entering — no crash.\n\n")

    # Old-form alternate buffer (CSI ?47h / ?47l)
    info("Old-form alternate buffer (mode 47):")
    write(decset(47))
    write(ed(2) + cup(1, 1) + "  Old-form alternate (mode 47)\n")
    time.sleep(1)
    write(decrst(47))
    write("  Back from mode 47 test.\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 30. Private Modes (DECSET/DECRST)
# ---------------------------------------------------------------------------

def test_private_modes():
    header("Private Modes (DECSET/DECRST)", 30)

    # DECTCEM — cursor visibility
    info("DECTCEM (mode 25) — cursor visibility:")
    write("  Cursor visible (default). Hiding in 1s...")
    time.sleep(1)
    write(decrst(25))  # Hide cursor
    write("\n  Cursor hidden. Showing in 2s...")
    time.sleep(2)
    write(decset(25))  # Show cursor
    write("\n  Cursor visible again.\n\n")

    # DECCKM — application cursor keys (mode 1)
    info("DECCKM (mode 1) — application cursor keys:")
    info("  Enabling: arrow keys send SS3 O[ABCD] instead of CSI [ABCD]")
    write("  " + decset(1) + "DECCKM enabled")
    write(decrst(1) + " -> disabled\n\n")

    # Bracketed paste mode (mode 2004)
    info("Bracketed paste mode (mode 2004):")
    write("  " + decset(2004) + "Enabled")
    write(decrst(2004) + " -> disabled\n")
    info("  (Pasted text would be wrapped in ESC[200~ ... ESC[201~)\n")

    # Mouse tracking modes
    info("Mouse tracking modes:")
    info("  Mode 1000 (normal tracking): " + decset(1000) + "on" + decrst(1000) + " off")
    info("  Mode 1002 (button-event):    " + decset(1002) + "on" + decrst(1002) + " off")
    info("  Mode 1003 (any-event):       " + decset(1003) + "on" + decrst(1003) + " off")
    info("  Mode 1006 (SGR encoding):    " + decset(1006) + "on" + decrst(1006) + " off")
    info("  (Each enabled and immediately disabled — no visible effect.)\n")

    # Unknown private mode — should be silently ignored
    info("Unknown private mode (should be ignored):")
    write("  " + CSI + "?9999h" + "After unknown DECSET 9999\n")
    write("  " + CSI + "?9999l" + "After unknown DECRST 9999\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 31. Device Status Report (DSR)
# ---------------------------------------------------------------------------

def test_dsr():
    header("Device Status Report (DSR)", 31)

    info("DSR 5 — device status query:")
    info("  Sending CSI 5 n (should respond with CSI 0 n = OK)")
    write("  " + CSI + "5n")
    info("  (Response written to child stdin, not visible here.)\n")

    info("DSR 6 — cursor position query:")
    info("  Sending CSI 6 n (should respond with CSI row;col R)")
    write(cup(8, 15))
    write(CSI + "6n")
    write(cup(9, 1))
    info("  Cursor was at (8,15); response should be ESC[8;15R\n")

    # DSR at various positions
    info("DSR at boundaries:")
    write(cup(1, 1) + CSI + "6n")    # Top-left
    write(cup(11, 1) + "  DSR at (1,1) sent.\n")
    write(cup(999, 999) + CSI + "6n")  # Bottom-right (clamped)
    write(cup(12, 1) + "  DSR at (999,999) sent (clamped to screen bounds).\n\n")

    info("(Responses are sent to stdin, visible if echo is on.)\n")

    wait_key()


# ---------------------------------------------------------------------------
# 32. Repeat Character (REP)
# ---------------------------------------------------------------------------

def test_rep():
    header("Repeat Character (REP = CSI b)", 32)

    info("REP repeats the last printed graphic character.\n")

    # Basic repeat
    write("  X" + rep(5) + " <- X repeated 5 times (XXXXXX total)\n")
    write("  A" + rep(0) + " <- REP 0 (no extra or 1?)\n")
    write("  B" + rep(1) + " <- REP 1 (BB total)\n")
    write("  C" + rep(20) + " <- REP 20\n\n")

    # Repeat after different characters
    info("REP after various characters:")
    write("  =" + rep(30) + " (equals)\n")
    write("  \u2500" + rep(30) + " (box horizontal)\n")
    write("  \u2588" + rep(30) + " (full block)\n\n")

    # REP with no preceding character — should be no-op or space
    info("REP with no preceding character (after erase):")
    write(ed(2) + cup(1, 1))
    header("Repeat Character (REP = CSI b)", 32)
    write(cup(14, 3) + rep(5) + " <- REP after clear (no prior char)\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 33. Window Title (OSC 2)
# ---------------------------------------------------------------------------

def test_window_title():
    header("Window Title (OSC 2)", 33)

    original = "WinPatina Stress Test"
    info("Setting window title to various strings...\n")

    titles = [
        "WinPatina Test - Hello!",
        "WinPatina Test - Unicode: \u00e9\u00e8\u00ea\u00eb",
        "WinPatina Test - 12345",
        "WinPatina - \u2605\u2606\u2764 Symbols",
        original,
    ]

    for title in titles:
        write(OSC + "2;" + title + BEL)  # BEL terminates OSC
        info(f"Title set to: {title}")
        time.sleep(0.8)

    write("\n")

    # OSC with ST terminator (ESC \) instead of BEL
    info("OSC with ST terminator (ESC \\) instead of BEL:")
    write(OSC + "2;Title via ST" + ST)
    info("  Title set using ST terminator.")
    time.sleep(0.5)
    write(OSC + "2;" + original + BEL)
    write("\n")

    wait_key()


# ---------------------------------------------------------------------------
# 34. OSC Edge Cases
# ---------------------------------------------------------------------------

def test_osc_edge_cases():
    header("OSC Edge Cases", 34)

    # Empty OSC string
    info("Empty OSC (no payload):")
    write("  " + OSC + BEL + "After empty OSC\n")

    # OSC with just the command number
    info("OSC with command only (no data):")
    write("  " + OSC + "2" + BEL + "After OSC 2 with no title\n")

    # Very long OSC string (near WP_MAX_STRING_LEN = 4096)
    info("Long OSC string (4000 chars):")
    long_title = "A" * 4000
    write("  " + OSC + "2;" + long_title + BEL)
    info("  Sent 4000-char title (may be truncated).")
    write(OSC + "2;WinPatina Stress Test" + BEL)
    write("\n")

    # OSC string exceeding max length
    info("Very long OSC string (5000 chars, exceeds max):")
    very_long = "B" * 5000
    write("  " + OSC + "2;" + very_long + BEL)
    info("  Sent 5000-char title (should be handled safely).")
    write(OSC + "2;WinPatina Stress Test" + BEL)
    write("\n")

    # OSC terminated by ESC \ vs BEL
    info("OSC terminated by ESC \\\\ (ST):")
    write("  " + OSC + "2;ST terminated" + ST + "After OSC+ST\n")

    # ESC interrupting OSC
    info("ESC interrupting OSC string:")
    write("  " + OSC + "2;Interrupted")
    write(ESC + "[32m" + "Green after interrupted OSC" + RESET + "\n")

    # CAN interrupting OSC
    info("CAN (0x18) interrupting OSC:")
    write("  " + OSC + "2;Cancelled" + CAN + "After CAN\n\n")

    # Unknown OSC command
    info("Unknown OSC command (e.g., OSC 999):")
    write("  " + OSC + "999;data" + BEL + "After unknown OSC 999\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 35. DCS/SOS/PM/APC Sequences
# ---------------------------------------------------------------------------

def test_dcs_sos_pm_apc():
    header("DCS/SOS/PM/APC Sequences", 35)

    info("These sequences should be consumed and ignored.\n")

    # DCS (Device Control String)
    info("DCS (ESC P ... ST):")
    write("  Before" + DCS + "some DCS data here" + ST + "After DCS\n")

    # DCS with parameters
    info("DCS with params (ESC P 1;2 q data ST):")
    write("  Before" + ESC + "P1;2q" + "DCS payload" + ST + "After DCS\n")

    # SOS (Start of String)
    info("SOS (ESC X ... ST):")
    write("  Before" + SOS + "SOS string data" + ST + "After SOS\n")

    # PM (Privacy Message)
    info("PM (ESC ^ ... ST):")
    write("  Before" + PM + "PM string data" + ST + "After PM\n")

    # APC (Application Program Command)
    info("APC (ESC _ ... ST):")
    write("  Before" + APC + "APC string data" + ST + "After APC\n\n")

    # DCS with BEL terminator (some terminals accept this)
    info("DCS with BEL terminator:")
    write("  Before" + DCS + "data" + BEL + "After DCS+BEL\n")

    # Nested ESC inside DCS — ESC \ should end it
    info("ESC inside DCS (ESC \\\\ terminates):")
    write("  Before" + DCS + "data" + ESC + "more" + ST + "After\n")

    # CAN interrupting DCS
    info("CAN interrupting DCS:")
    write("  Before" + DCS + "data" + CAN + "After CAN\n\n")

    info("(All lines should show 'Before...After' with no stray data.)\n")

    wait_key()


# ---------------------------------------------------------------------------
# 36. Box Drawing & Block Elements
# ---------------------------------------------------------------------------

def test_box_drawing():
    header("Box Drawing & Block Elements", 36)

    # Single-line box
    info("Single-line box:")
    write("  \u250c\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n")
    write("  \u2502  Box drawing OK!  \u2502\n")
    write("  \u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n\n")

    # Double-line box
    info("Double-line box:")
    write("  \u2554\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2557\n")
    write("  \u2551  Double borders!  \u2551\n")
    write("  \u255a\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u2550\u255d\n\n")

    # Rounded corners
    info("Rounded corners:")
    write("  \u256d\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u256e\n")
    write("  \u2502  Rounded corners  \u2502\n")
    write("  \u2570\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u256f\n\n")

    # Block elements
    info("Block elements:")
    write("  Shades:     \u2591\u2591\u2592\u2592\u2593\u2593\u2588\u2588\n")
    write("  Half:       \u2580\u2584\u258c\u2590\n")
    write("  Quadrants:  \u2596\u2597\u2598\u2599\u259a\u259b\u259c\u259d\u259e\u259f\n")
    write("  Braille:    \u2800\u2801\u2802\u2803\u2804\u2805\u2806\u2807\u2808\u2809\u280a\u280b\n\n")

    # Table with intersections
    info("Table with all intersection types:")
    write("  \u250c\u2500\u2500\u2500\u2500\u2500\u2500\u252c\u2500\u2500\u2500\u2500\u2500\u2500\u252c\u2500\u2500\u2500\u2500\u2500\u2500\u2510\n")
    write("  \u2502 " + sgr(1) + "Col1" + RESET + " \u2502 " + sgr(1) + "Col2" + RESET + " \u2502 " + sgr(1) + "Col3" + RESET + " \u2502\n")
    write("  \u251c\u2500\u2500\u2500\u2500\u2500\u2500\u253c\u2500\u2500\u2500\u2500\u2500\u2500\u253c\u2500\u2500\u2500\u2500\u2500\u2500\u2524\n")
    write("  \u2502  A1  \u2502  B1  \u2502  C1  \u2502\n")
    write("  \u2502  A2  \u2502  B2  \u2502  C2  \u2502\n")
    write("  \u2514\u2500\u2500\u2500\u2500\u2500\u2500\u2534\u2500\u2500\u2500\u2500\u2500\u2500\u2534\u2500\u2500\u2500\u2500\u2500\u2500\u2518\n\n")

    # Mixed single and double line
    info("Mixed single/double borders:")
    write("  \u2552\u2550\u2550\u2550\u2550\u2564\u2550\u2550\u2550\u2550\u2555\n")
    write("  \u2502  A \u2502  B \u2502\n")
    write("  \u2558\u2550\u2550\u2550\u2550\u2567\u2550\u2550\u2550\u2550\u255b\n\n")

    # Powerline-style characters
    info("Powerline glyphs (if available):")
    write("  \ue0b0 \ue0b1 \ue0b2 \ue0b3 (may show as \ufffd)\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# 37. Full-screen Application Simulation
# ---------------------------------------------------------------------------

def test_fullscreen_simulation():
    header("Full-screen Application Simulation", 37)

    info("Simulating a TUI application (like a text editor):\n")
    time.sleep(1)

    # Enter alternate screen for clean workspace
    write(decset(1049))
    write(decrst(25))  # Hide cursor for clean rendering
    write(ed(2))

    # Draw a simulated editor UI
    # Title bar
    write(cup(1, 1) + sgr(1, 97, 44) + " WinPatina Editor v1.0")
    write(" " * 58 + RESET)

    # Menu bar
    write(cup(2, 1) + sgr(7) + " File  Edit  View  Help " + RESET)
    write(sgr(90) + "\u2500" * 56 + RESET)

    # Content area with line numbers
    lines = [
        '#include <stdio.h>',
        '',
        'int main(void) {',
        '    printf("Hello, world!\\n");',
        '    return 0;',
        '}',
        '',
        '// End of file',
    ]

    for i, line in enumerate(lines):
        write(cup(4 + i, 1))
        write(sgr(90) + f" {i+1:3d} " + RESET + sgr(33) + "\u2502" + RESET + " ")
        # Simple syntax highlighting
        if line.startswith('#'):
            write(sgr(35) + line + RESET)
        elif line.startswith('//'):
            write(sgr(90) + line + RESET)
        elif 'printf' in line:
            write(sgr(37) + '    ' + sgr(33) + 'printf' + sgr(37) + '(' + sgr(32) + '"Hello, world!\\n"' + sgr(37) + ');' + RESET)
        elif 'return' in line:
            write(sgr(35) + '    return ' + sgr(36) + '0' + sgr(37) + ';' + RESET)
        elif 'int main' in line:
            write(sgr(33) + 'int ' + sgr(34) + 'main' + sgr(37) + '(void) {' + RESET)
        else:
            write(line)

    # Status bar
    write(cup(23, 1) + sgr(97, 42) + " NORMAL " + RESET + sgr(97, 44))
    write(" main.c " + RESET + sgr(90, 44) + " UTF-8 | LF | C ")
    write(" " * 38 + " 1:1 " + RESET)

    # Command line
    write(cup(24, 1) + sgr(90) + "  :q to quit | :w to save | i for insert mode" + RESET)

    time.sleep(3)

    # Simulate cursor blinking
    write(decset(25))
    write(cup(4, 7))
    time.sleep(1)

    # Leave alternate screen
    write(decrst(1049))
    write(decset(25))

    info("Full-screen simulation complete.")
    info("Main screen content should be restored.\n")

    wait_key()


# ---------------------------------------------------------------------------
# 38. Rapid Output (Throughput Stress)
# ---------------------------------------------------------------------------

def test_rapid_output():
    header("Rapid Output (Throughput Stress)", 38)

    info("Filling screen with coloured text as fast as possible...\n")
    time.sleep(0.5)

    start = time.time()
    total_bytes = 0

    for iteration in range(5):
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


# ---------------------------------------------------------------------------
# 39. Mixed Sequence Interleaving
# ---------------------------------------------------------------------------

def test_mixed_sequences():
    header("Mixed Sequence Interleaving", 39)

    info("Rapid alternation between different sequence types:\n")

    # Interleave SGR + CUP + text + erase
    start = time.time()
    for i in range(200):
        row = 4 + (i % 18)
        col = 1 + (i * 7) % 70
        colour = 31 + (i % 6)
        write(cup(row, col) + sgr(colour) + "*" + RESET)
    elapsed = time.time() - start
    write(cup(23, 1))
    info(f"200 mixed CUP+SGR+char in {elapsed*1000:.0f}ms")
    write("\n")
    time.sleep(0.5)

    # Interleave cursor save/restore with drawing
    write(ed(2) + cup(1, 1))
    header("Mixed Sequence Interleaving", 39)
    write(cup(5, 1))
    info("Interleaved save/restore with drawing:")
    for i in range(10):
        write(cup(7 + i, 3) + DECSC)
        write(cup(7 + i, 40) + sgr(32) + f"Right side {i}" + RESET)
        write(DECRC + sgr(33) + f"Left side {i}" + RESET)
    write(cup(18, 1) + "\n")

    # Interleave scroll region ops with normal drawing
    info("Scroll region ops interleaved with normal text:")
    write(decstbm(20, 22))
    for i in range(5):
        write(cup(22, 3) + sgr(31 + (i % 6)) + f"Region scroll {i}" + RESET + LF)
        write(cup(19, 3) + sgr(90) + f"Outside region: step {i}" + RESET)
        time.sleep(0.1)
    write(decstbm(1, 999))
    write(cup(23, 1) + "\n")

    wait_key()


# ---------------------------------------------------------------------------
# 40. Burst & Fragmentation Stress
# ---------------------------------------------------------------------------

def test_burst_fragmentation():
    header("Burst & Fragmentation Stress", 40)

    info("Testing parser with rapid bursts of data:\n")

    # Large single write — many sequences concatenated
    info("Large single write (1000 SGR sequences in one flush):")
    big_chunk = ""
    for i in range(1000):
        big_chunk += sgr(31 + (i % 6)) + "X"
    big_chunk += RESET
    start = time.time()
    write("  " + big_chunk)
    elapsed = time.time() - start
    write(f"\n  1000 sequences in one write: {elapsed*1000:.0f}ms\n\n")

    # Many tiny writes — one byte at a time
    info("Many tiny writes (500 chars, flushed individually):")
    start = time.time()
    write("  ")
    for i in range(500):
        sys.stdout.write(sgr(31 + (i % 6)) + ".")
        sys.stdout.flush()
    elapsed = time.time() - start
    write(RESET + f"\n  500 individual flushes: {elapsed*1000:.0f}ms\n\n")

    # Sequences split across writes
    info("CSI sequence split across writes:")
    write("  ")
    sys.stdout.write("\x1b")       # ESC
    sys.stdout.flush()
    sys.stdout.write("[")          # [
    sys.stdout.flush()
    sys.stdout.write("32m")        # 32m
    sys.stdout.flush()
    write("Green (split CSI)" + RESET + "\n")

    # SGR with many parameters in one sequence
    info("SGR with many parameters (1;4;7;31;42):")
    write("  " + CSI + "1;4;7;31;42m" + "Bold+UL+Rev+Red+GreenBG" + RESET + "\n\n")

    # Alternating between printable text and escape sequences
    info("Rapid text-escape alternation (1000 cycles):")
    start = time.time()
    write("  ")
    for i in range(1000):
        write(sgr(31 + (i % 6)) + chr(65 + (i % 26)))
    elapsed = time.time() - start
    write(RESET + f"\n  1000 SGR+char cycles: {elapsed*1000:.0f}ms\n\n")

    wait_key()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Ensure output is unbuffered
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(write_through=True)

    write(ed(2) + cup(1, 1))
    write(sgr(1, 96) + "  WinPatina VT Stress Test \u2014 Comprehensive Edition" + RESET + "\n")
    write(sgr(90) + "  " + "=" * 52 + RESET + "\n\n")
    write("  This programme exercises the full VT translation pipeline\n")
    write("  with 40 test sections covering edge cases, boundary\n")
    write("  conditions, and stress scenarios.\n\n")

    write("  Note: 256-colour and RGB tests show quantised results\n")
    write("  (mapped to 16 colours) when running through WinPatina's\n")
    write("  Win32 translation layer. This is expected behaviour.\n\n")

    wait_key()

    tests = [
        test_basic_text,                # 1
        test_utf8_edge_cases,           # 2
        test_wide_characters,           # 3
        test_16_colours,                # 4
        test_256_colours,               # 5
        test_rgb_colours,               # 6
        test_colour_quantisation,       # 7
        test_attributes,                # 8
        test_sgr_selective_disable,     # 9
        test_sgr_bold_dim_conflict,     # 10
        test_sgr_extended_colours,      # 11
        test_sgr_combos,                # 12
        test_attribute_persistence,     # 13
        test_cursor_movement,           # 14
        test_cursor_boundaries,         # 15
        test_cursor_save_restore,       # 16
        test_tab_stops,                 # 17
        test_c0_controls,               # 18
        test_csi_param_edge_cases,      # 19
        test_malformed_sequences,       # 20
        test_line_wrap,                 # 21
        test_erase_operations,          # 22
        test_insert_delete_chars,       # 23
        test_insert_delete_lines,       # 24
        test_scroll_regions,            # 25
        test_scroll_region_advanced,    # 26
        test_reverse_index,             # 27
        test_alt_screen,                # 28
        test_alt_screen_advanced,       # 29
        test_private_modes,             # 30
        test_dsr,                       # 31
        test_rep,                       # 32
        test_window_title,              # 33
        test_osc_edge_cases,            # 34
        test_dcs_sos_pm_apc,            # 35
        test_box_drawing,               # 36
        test_fullscreen_simulation,     # 37
        test_rapid_output,              # 38
        test_mixed_sequences,           # 39
        test_burst_fragmentation,       # 40
    ]

    for test_fn in tests:
        try:
            test_fn()
        except Exception as e:
            write(RESET + f"\n  ERROR in {test_fn.__name__}: {e}\n\n")
            import traceback
            traceback.print_exc()
            wait_key()

    # Final summary
    write(ed(2) + cup(1, 1))
    write(sgr(1, 92) + "  All tests complete!" + RESET + "\n\n")
    write(f"  Ran {len(tests)} test sections covering:\n\n")

    categories = [
        ("Text & Unicode",     "Basic ASCII, multi-byte UTF-8, wide chars, combining"),
        ("Colour",             "16-colour, 256-colour, RGB, quantisation boundaries"),
        ("SGR Attributes",     "All attributes, selective disable, bold/dim conflict"),
        ("Extended Colours",   "Colon sub-params, mixed SGR, boundary indices"),
        ("Cursor",             "Absolute, relative, boundaries, save/restore, tabs"),
        ("C0 Controls",        "BS, CR, LF, VT, FF, BEL, NUL, HT"),
        ("CSI Parameters",     "Defaults, empty, overflow, trailing semicolons"),
        ("Malformed Sequences","ESC/CAN/SUB interrupts, CSI_IGNORE, unknown finals"),
        ("Line Wrap",          "Pending wrap, CR clear, cursor movement clear"),
        ("Erase",              "ED, EL, ECH at various positions and boundaries"),
        ("Insert/Delete",      "ICH, DCH, IL, DL with edge counts"),
        ("Scroll Regions",     "DECSTBM, single-row, SU/SD, content preservation"),
        ("Reverse Index",      "RI from middle, RI at top of scroll region"),
        ("Alternate Buffer",   "Enter/leave, double enter, old-form, cursor save"),
        ("Private Modes",      "DECTCEM, DECCKM, mouse, bracketed paste"),
        ("DSR",                "Device status, cursor position query"),
        ("REP",                "Repeat preceding character"),
        ("OSC",                "Window title, BEL/ST, long strings, edge cases"),
        ("DCS/SOS/PM/APC",     "Consumed silently, various terminators"),
        ("Box Drawing",        "Single, double, rounded, blocks, braille, tables"),
        ("Full-screen",        "TUI simulation with syntax highlighting"),
        ("Throughput",         "Rapid output, burst writes, fragmentation"),
        ("Interleaving",       "Mixed sequence types, rapid state transitions"),
    ]

    for name, desc in categories:
        write(f"    {sgr(1)}{name:22s}{RESET} {sgr(90)}{desc}{RESET}\n")

    write(f"\n  {sgr(1)}{len(tests)}{RESET} sections, "
          f"{sgr(1)}{len(categories)}{RESET} categories.\n\n")

    write(sgr(90) + "  Press Enter to exit." + RESET + "\n")
    try:
        input()
    except EOFError:
        pass

if __name__ == "__main__":
    main()
