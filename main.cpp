#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"

#include "ili9341.h"
#include "trs80.h"
#include "main.h"
#include "fonts.h"
#include "obstacle_run_cmd.h"

#define TFT_SCLK        18
#define TFT_MOSI        19
#define TFT_DC          20
#define TFT_RST         21
#define TFT_CS          17

#define TFT_WIDTH       320
#define TFT_HEIGHT      240
#define TFT_ROTATION    1

// Colors are in 565 (FFFF) format. To convert from RGB888 to RGB565, use:
#define RGB888TO565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define BACKGROUND RGB888TO565(0x00, 0x00, 0x00)
#define FOREGROUND RGB888TO565(0xFF, 0xFF, 0xFF)

#define HMARGIN 32
#define VMARGIN 24

const uint LED_PIN = 25;

static uint16_t COLORS[4] = {
    RGB888TO565(0x00, 0x00, 0x00),
    RGB888TO565(0x80, 0x80, 0x80),
    RGB888TO565(0x80, 0x80, 0x80),
    RGB888TO565(0xFF, 0xFF, 0xFF),
};

void configure_lcd() {
    LCD_setPins(TFT_DC, TFT_CS, TFT_RST, TFT_SCLK, TFT_MOSI);
    LCD_initDisplay();
    LCD_setRotation(TFT_ROTATION);
    LCD_fillRect(0, 0, LCD_getWidth(), LCD_getHeight(), BACKGROUND);
}

void writeScreenChar(int position, uint8_t ch) {
    if (ch > 32 && ch < 128) {
        printf("%4d %c\n", position, ch);
    }
    int textCol = position % 64;
    int textRow = position / 64;

    int ox = HMARGIN + textCol*4;
    int oy = VMARGIN + textRow*12;
    for (int y = 0; y < 12; y++) {
        unsigned char b = Trs80FontBits[ch*12 + y];
        for (int x = 0; x < 4; x++) {
            int gray = (b >> (x*2)) & 0x03;
            LCD_writePixel(ox + x, oy + y, COLORS[gray]);
        }
    }
}

void keyCallback(int ch) {
    handleKeypress(ch, true);
    handleKeypress(ch, false);
}

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    configure_lcd();

#if 1
    queueEvent(1, keyCallback, 'L');
    queueEvent(2, keyCallback, '0');
    queueEvent(3, keyCallback, '\n');
#endif

    trs80_main();
}
