/**
 * @file winpatina_colour.cpp
 * @brief Colour mapping utilities implementation
 *
 * Translates VT colour specifications to Win32 console attributes.
 *
 * Key insight: Win32 console uses BGR bit order while ANSI uses an
 * index-based scheme. The mapping tables handle the translation:
 *
 *   ANSI 0 (Black)   -> Win32 0x00  (000)
 *   ANSI 1 (Red)     -> Win32 0x04  (100 = RED)
 *   ANSI 2 (Green)   -> Win32 0x02  (010 = GREEN)
 *   ANSI 3 (Yellow)  -> Win32 0x06  (110 = RED|GREEN)
 *   ANSI 4 (Blue)    -> Win32 0x01  (001 = BLUE)
 *   ANSI 5 (Magenta) -> Win32 0x05  (101 = RED|BLUE)
 *   ANSI 6 (Cyan)    -> Win32 0x03  (011 = GREEN|BLUE)
 *   ANSI 7 (White)   -> Win32 0x07  (111 = RED|GREEN|BLUE)
 *   8-15: same as 0-7 with INTENSITY bit (0x08)
 */

#include "winpatina_colour.h"
#include <string.h>
#include <limits.h>

#ifdef WINPATINA_USE_CIE_LAB
#include <math.h>
#endif

/*============================================================================
 * ANSI → Win32 Lookup Tables
 *============================================================================*/

/**
 * ANSI colour index (0-15) to Win32 foreground attribute bits.
 * Foreground occupies bits 0-3.
 */
static const WORD ansi_to_win32_fg[16] = {
    0x00,   /*  0: Black */
    0x04,   /*  1: Red            (FOREGROUND_RED) */
    0x02,   /*  2: Green          (FOREGROUND_GREEN) */
    0x06,   /*  3: Yellow         (FOREGROUND_RED | FOREGROUND_GREEN) */
    0x01,   /*  4: Blue           (FOREGROUND_BLUE) */
    0x05,   /*  5: Magenta        (FOREGROUND_RED | FOREGROUND_BLUE) */
    0x03,   /*  6: Cyan           (FOREGROUND_GREEN | FOREGROUND_BLUE) */
    0x07,   /*  7: White          (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) */
    0x08,   /*  8: Bright Black   (FOREGROUND_INTENSITY) */
    0x0C,   /*  9: Bright Red     (FOREGROUND_INTENSITY | FOREGROUND_RED) */
    0x0A,   /* 10: Bright Green   (FOREGROUND_INTENSITY | FOREGROUND_GREEN) */
    0x0E,   /* 11: Bright Yellow  (FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN) */
    0x09,   /* 12: Bright Blue    (FOREGROUND_INTENSITY | FOREGROUND_BLUE) */
    0x0D,   /* 13: Bright Magenta (FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_BLUE) */
    0x0B,   /* 14: Bright Cyan    (FOREGROUND_INTENSITY | FOREGROUND_GREEN | FOREGROUND_BLUE) */
    0x0F    /* 15: Bright White   (FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE) */
};

/**
 * ANSI colour index (0-15) to Win32 background attribute bits.
 * Background occupies bits 4-7 (same values shifted left by 4).
 */
static const WORD ansi_to_win32_bg[16] = {
    0x00, 0x40, 0x20, 0x60, 0x10, 0x50, 0x30, 0x70,
    0x80, 0xC0, 0xA0, 0xE0, 0x90, 0xD0, 0xB0, 0xF0
};

/*============================================================================
 * Standard 16-Colour Palette (approximate sRGB values)
 *============================================================================*/

static const WPColourRGB palette_16[16] = {
    {  0,   0,   0},   /*  0: Black */
    {128,   0,   0},   /*  1: Red */
    {  0, 128,   0},   /*  2: Green */
    {128, 128,   0},   /*  3: Yellow */
    {  0,   0, 128},   /*  4: Blue */
    {128,   0, 128},   /*  5: Magenta */
    {  0, 128, 128},   /*  6: Cyan */
    {192, 192, 192},   /*  7: White */
    {128, 128, 128},   /*  8: Bright Black (Grey) */
    {255,   0,   0},   /*  9: Bright Red */
    {  0, 255,   0},   /* 10: Bright Green */
    {255, 255,   0},   /* 11: Bright Yellow */
    {  0,   0, 255},   /* 12: Bright Blue */
    {255,   0, 255},   /* 13: Bright Magenta */
    {  0, 255, 255},   /* 14: Bright Cyan */
    {255, 255, 255}    /* 15: Bright White */
};

/*============================================================================
 * ANSI 16 → Win32 Conversion
 *============================================================================*/

WORD wp_colour_ansi_to_fg(int index)
{
    if (index < 0 || index > 15) {
        return 0x07;  /* Default: white */
    }
    return ansi_to_win32_fg[index];
}

WORD wp_colour_ansi_to_bg(int index)
{
    if (index < 0 || index > 15) {
        return 0x00;  /* Default: black */
    }
    return ansi_to_win32_bg[index];
}

/*============================================================================
 * 256-Colour Palette
 *============================================================================*/

WPColourRGB wp_colour_256_to_rgb(int index)
{
    if (index < 0 || index > 255) {
        WPColourRGB black = {0, 0, 0};
        return black;
    }

    if (index < 16) {
        /* Standard ANSI colours */
        return palette_16[index];
    }

    if (index < 232) {
        /* 6x6x6 colour cube (indices 16-231)
         * Each axis has values: 0, 95, 135, 175, 215, 255
         * Simplified: value = (component == 0) ? 0 : 55 + component * 40 */
        int ci = index - 16;
        int ri = ci / 36;
        int gi = (ci / 6) % 6;
        int bi = ci % 6;

        WPColourRGB rgb;
        rgb.r = (uint8_t)(ri == 0 ? 0 : 55 + ri * 40);
        rgb.g = (uint8_t)(gi == 0 ? 0 : 55 + gi * 40);
        rgb.b = (uint8_t)(bi == 0 ? 0 : 55 + bi * 40);
        return rgb;
    }

    /* Greyscale ramp (indices 232-255): 24 shades from 8 to 238 */
    int grey = 8 + (index - 232) * 10;
    WPColourRGB rgb;
    rgb.r = (uint8_t)grey;
    rgb.g = (uint8_t)grey;
    rgb.b = (uint8_t)grey;
    return rgb;
}

/*============================================================================
 * Colour Distance
 *============================================================================*/

#ifdef WINPATINA_USE_CIE_LAB

/* CIE Lab perceptual colour distance (more accurate, slower) */

static double srgb_to_linear(double c)
{
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static void rgb_to_lab(WPColourRGB rgb, double* L, double* a, double* b)
{
    double r = srgb_to_linear(rgb.r / 255.0);
    double g = srgb_to_linear(rgb.g / 255.0);
    double bl = srgb_to_linear(rgb.b / 255.0);

    /* Linear RGB to XYZ (D65 illuminant) */
    double x = r * 0.4124564 + g * 0.3575761 + bl * 0.1804375;
    double y = r * 0.2126729 + g * 0.7151522 + bl * 0.0721750;
    double z = r * 0.0193339 + g * 0.1191920 + bl * 0.9503041;

    /* Normalise by D65 reference white */
    x /= 0.95047;
    y /= 1.00000;
    z /= 1.08883;

    auto f = [](double t) -> double {
        return (t > 0.008856) ? pow(t, 1.0 / 3.0) : (7.787 * t + 16.0 / 116.0);
    };

    *L = 116.0 * f(y) - 16.0;
    *a = 500.0 * (f(x) - f(y));
    *b = 200.0 * (f(y) - f(z));
}

static double colour_distance(WPColourRGB c1, WPColourRGB c2)
{
    double L1, a1, b1, L2, a2, b2;
    rgb_to_lab(c1, &L1, &a1, &b1);
    rgb_to_lab(c2, &L2, &a2, &b2);

    double dL = L1 - L2;
    double da = a1 - a2;
    double db = b1 - b2;

    return dL * dL + da * da + db * db;
}

#else

/* Euclidean distance in RGB space (simple and fast) */

static int colour_distance(WPColourRGB c1, WPColourRGB c2)
{
    int dr = (int)c1.r - (int)c2.r;
    int dg = (int)c1.g - (int)c2.g;
    int db = (int)c1.b - (int)c2.b;
    return dr * dr + dg * dg + db * db;
}

#endif /* WINPATINA_USE_CIE_LAB */

/*============================================================================
 * RGB → 16-Colour Quantisation
 *============================================================================*/

int wp_colour_rgb_to_16(int r, int g, int b)
{
    WPColourRGB target;
    target.r = (uint8_t)(r < 0 ? 0 : (r > 255 ? 255 : r));
    target.g = (uint8_t)(g < 0 ? 0 : (g > 255 ? 255 : g));
    target.b = (uint8_t)(b < 0 ? 0 : (b > 255 ? 255 : b));

    int best_index = 0;

#ifdef WINPATINA_USE_CIE_LAB
    double best_dist = 1e30;
#else
    int best_dist = INT_MAX;
#endif

    for (int i = 0; i < 16; i++) {
        auto dist = colour_distance(target, palette_16[i]);
        if (dist < best_dist) {
            best_dist = dist;
            best_index = i;
        }
    }

    return best_index;
}

int wp_colour_256_to_16(int index)
{
    if (index < 0 || index > 255) {
        return 7;  /* Default: white */
    }

    if (index < 16) {
        return index;  /* Direct mapping */
    }

    WPColourRGB rgb = wp_colour_256_to_rgb(index);
    return wp_colour_rgb_to_16(rgb.r, rgb.g, rgb.b);
}

/*============================================================================
 * SGR State Management
 *============================================================================*/

/**
 * Extract the foreground ANSI index (0-7) from a Win32 attribute WORD.
 * Maps Win32 BGR bits back to ANSI index.
 */
static int attrs_to_fg_index(WORD attrs)
{
    WORD fg = attrs & 0x07;

    /* Reverse the BGR→RGB mapping */
    for (int i = 0; i < 8; i++) {
        if (ansi_to_win32_fg[i] == fg) {
            return i;
        }
    }
    return 7;  /* Default: white */
}

/**
 * Extract the background ANSI index (0-7) from a Win32 attribute WORD.
 */
static int attrs_to_bg_index(WORD attrs)
{
    WORD bg = attrs & 0x70;

    for (int i = 0; i < 8; i++) {
        if (ansi_to_win32_bg[i] == bg) {
            return i;
        }
    }
    return 0;  /* Default: black */
}

void wp_sgr_init(WPSGRState* sgr, WORD default_attrs)
{
    memset(sgr, 0, sizeof(*sgr));
    sgr->fg_index = attrs_to_fg_index(default_attrs);
    sgr->bg_index = attrs_to_bg_index(default_attrs);

    /* Check if default attrs include intensity (bold) */
    if (default_attrs & FOREGROUND_INTENSITY) {
        sgr->bold = true;
    }
}

void wp_sgr_reset(WPSGRState* sgr, WORD default_attrs)
{
    wp_sgr_init(sgr, default_attrs);
}

WORD wp_sgr_to_attrs(const WPSGRState* sgr, WORD default_attrs, bool has_lvb)
{
    WORD fg_bits;
    WORD bg_bits;

    /* Resolve foreground */
    if (sgr->fg_index < 0) {
        fg_bits = default_attrs & 0x0F;
    } else {
        fg_bits = ansi_to_win32_fg[sgr->fg_index & 0x0F];
    }

    /* Resolve background */
    if (sgr->bg_index < 0) {
        bg_bits = default_attrs & 0xF0;
    } else {
        bg_bits = ansi_to_win32_bg[sgr->bg_index & 0x0F];
    }

    /* Bold maps to foreground intensity */
    if (sgr->bold) {
        fg_bits |= FOREGROUND_INTENSITY;
    }

    /* Dim removes foreground intensity (darker text) */
    if (sgr->dim) {
        fg_bits &= ~FOREGROUND_INTENSITY;
    }

    WORD attrs = fg_bits | bg_bits;

    /* Underline via LVB attribute (Vista+) */
    if (sgr->underline && has_lvb) {
        attrs |= 0x8000;  /* COMMON_LVB_UNDERSCORE */
    }

    /* Overline via LVB grid horizontal (Vista+) */
    if (sgr->overline && has_lvb) {
        attrs |= 0x0400;  /* COMMON_LVB_GRID_HORIZONTAL */
    }

    /* Hidden: set foreground to match background (text invisible) */
    if (sgr->hidden) {
        fg_bits = (bg_bits >> 4) & 0x0F;
        attrs = fg_bits | bg_bits;
        /* Preserve LVB flags */
        if (sgr->underline && has_lvb) attrs |= 0x8000;
        if (sgr->overline && has_lvb)  attrs |= 0x0400;
    }

    /* Reverse video: swap foreground and background */
    if (sgr->reverse) {
        WORD eff_fg = attrs & 0x0F;
        WORD eff_bg = (attrs >> 4) & 0x0F;
        WORD new_fg = eff_bg;
        WORD new_bg = eff_fg << 4;
        attrs = (attrs & 0xFF00) | new_fg | new_bg;

        /* Preserve intensity on the (now-swapped) foreground */
        if (sgr->bold) {
            attrs |= FOREGROUND_INTENSITY;
        }
    }

    return attrs;
}
