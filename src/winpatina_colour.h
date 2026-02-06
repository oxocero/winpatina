/**
 * @file winpatina_colour.h
 * @brief Colour mapping utilities - internal header
 *
 * Translates VT colour specifications (ANSI 16, xterm 256, RGB) to
 * Win32 console attribute WORD values. On legacy systems where only
 * 16 colours are available, 256-colour and RGB values are quantised
 * to the nearest match.
 *
 * The Win32 console uses BGR bit order for colour attributes:
 *   Bit 0 = BLUE, Bit 1 = GREEN, Bit 2 = RED, Bit 3 = INTENSITY
 * ANSI uses a different order (RGB), so a mapping table is needed.
 *
 * Only implementation files should include this header.
 */

#ifndef WINPATINA_COLOUR_H
#define WINPATINA_COLOUR_H

#include <stdint.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0500
#endif
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * RGB Structure
 *============================================================================*/

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} WPColourRGB;

/*============================================================================
 * ANSI 16 → Win32 Attribute Conversion
 *============================================================================*/

/**
 * @brief Convert an ANSI colour index (0-15) to Win32 foreground bits
 *
 * @param index  ANSI colour index (0-15)
 * @return Win32 attribute bits for the foreground (bits 0-3)
 */
WORD wp_colour_ansi_to_fg(int index);

/**
 * @brief Convert an ANSI colour index (0-15) to Win32 background bits
 *
 * @param index  ANSI colour index (0-15)
 * @return Win32 attribute bits for the background (bits 4-7)
 */
WORD wp_colour_ansi_to_bg(int index);

/*============================================================================
 * 256-Colour Palette
 *============================================================================*/

/**
 * @brief Convert a 256-colour index to RGB
 *
 * The 256-colour palette has three regions:
 *   0-15:    Standard ANSI colours
 *   16-231:  6x6x6 colour cube
 *   232-255: Greyscale ramp (24 shades)
 *
 * @param index  Colour index (0-255)
 * @return RGB values
 */
WPColourRGB wp_colour_256_to_rgb(int index);

/**
 * @brief Convert a 256-colour index to the nearest ANSI 16-colour index
 *
 * @param index  Colour index (0-255)
 * @return Nearest ANSI colour index (0-15)
 */
int wp_colour_256_to_16(int index);

/*============================================================================
 * RGB → 16-Colour Quantisation
 *============================================================================*/

/**
 * @brief Find the nearest ANSI 16-colour index for an RGB value
 *
 * Uses Euclidean distance in RGB space by default. If the library was
 * built with WINPATINA_USE_CIE_LAB, perceptual CIE76 distance is used
 * instead (slower but more accurate).
 *
 * @param r  Red (0-255)
 * @param g  Green (0-255)
 * @param b  Blue (0-255)
 * @return Nearest ANSI colour index (0-15)
 */
int wp_colour_rgb_to_16(int r, int g, int b);

/*============================================================================
 * SGR Attribute Handling
 *============================================================================*/

/**
 * @brief Attribute state tracked during SGR processing
 *
 * This holds the logical text attributes as set by SGR sequences.
 * It is converted to a Win32 WORD when needed.
 */
typedef struct {
    int fg_index;       /**< Foreground: ANSI index 0-15, or -1 for default */
    int bg_index;       /**< Background: ANSI index 0-15, or -1 for default */
    bool bold;          /**< SGR 1: bold (mapped to INTENSITY) */
    bool dim;           /**< SGR 2: dim (ignored on Win32, tracked for reset) */
    bool underline;     /**< SGR 4: underline (Vista+ via LVB_UNDERSCORE) */
    bool reverse;       /**< SGR 7: reverse video */
} WPSGRState;

/**
 * @brief Initialise SGR state to defaults
 *
 * @param sgr            State to initialise
 * @param default_attrs  Win32 default attributes (used to derive default fg/bg)
 */
void wp_sgr_init(WPSGRState* sgr, WORD default_attrs);

/**
 * @brief Reset SGR state to defaults (SGR 0)
 *
 * @param sgr            State to reset
 * @param default_attrs  Win32 default attributes
 */
void wp_sgr_reset(WPSGRState* sgr, WORD default_attrs);

/**
 * @brief Convert current SGR state to a Win32 attribute WORD
 *
 * Combines foreground, background, bold/intensity, underline, and
 * reverse into a single WORD suitable for CHAR_INFO.Attributes.
 *
 * @param sgr            Current SGR state
 * @param default_attrs  Win32 default attributes (for default fg/bg)
 * @param has_lvb        True if LVB attributes (underline) are available
 * @return Win32 attribute WORD
 */
WORD wp_sgr_to_attrs(const WPSGRState* sgr, WORD default_attrs, bool has_lvb);

#ifdef __cplusplus
}
#endif

#endif /* WINPATINA_COLOUR_H */
