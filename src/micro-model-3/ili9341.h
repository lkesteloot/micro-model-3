
// Control the ILI9341 LCD driver.

#pragma once

#include "hardware/spi.h"

// Convert from RGB888 to RGB565:
#define RGB888TO565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// Some ready-made 16-bit (565) color settings:
#define LCD_BLACK 0x0000
#define LCD_WHITE 0xFFFF
#define LCD_GRAY_128 0x8410
#define LCD_GRAY_186 0xBDD7
#define LCD_RED 0xF800
#define LCD_GREEN 0x07E0
#define LCD_BLUE 0x001F
#define LCD_CYAN 0x07FF
#define LCD_MAGENTA 0xF81F
#define LCD_YELLOW 0xFFE0
#define LCD_ORANGE 0xFC00

// Values for the LCD_setRotation() function:
#define LCD_ROTATION_NONE 0
#define LCD_ROTATION_90 1
#define LCD_ROTATION_180 2
#define LCD_ROTATION_270 3

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure pins used to control the LCD.
 *
 * dc - data or command selector (D/CX).
 * cs - chip select (CSX).
 * rst - hardware reset (RESX), or -1 if not connected.
 * sck - serial clock (SCL).
 * tx - data transmit (WRX).
 */
void LCD_setPins(uint16_t dc, uint16_t cs, int16_t rst, uint16_t sck, uint16_t tx);

/**
 * Which SPI peripheral to use. Skip this call to use the default peripheral.
 */
void LCD_setSPIperiph(spi_inst_t *s);

/**
 * Send the initialization sequence to the display.
 */
void LCD_initDisplay();

/**
 * Set the rotation of the display. See the LCD_ROTATION_* constants.
 */
void LCD_setRotation(int rotation);

/**
 * Width of the display (modified by rotation).
 */
uint16_t LCD_getWidth();

/**
 * Height of the display (modified by rotation).
 */
uint16_t LCD_getHeight();

/**
 * Write the 565 color at the (x,y) pixel.
 */
void LCD_writePixel(int x, int y, uint16_t col);

/**
 * Write the 565 bitmap of the specified (w,h) size at the (x,y) location.
 */
void LCD_writeBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *bitmap);

/**
 * Fill a solid rectangle of size (w,h) at (x,y) and 565 color.
 */
void LCD_fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

#ifdef __cplusplus
};
#endif
