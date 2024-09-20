#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "gfx.h"

#include "ili9341.h"
// #include "gfx.h"
#include "trs80.h"
#include "main.h"
#include "fonts.h"

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

void lcd_test() {
    LCD_setPins(TFT_DC, TFT_CS, TFT_RST, TFT_SCLK, TFT_MOSI);
    LCD_initDisplay();
    LCD_setRotation(TFT_ROTATION);

    GFX_setClearColor(BACKGROUND);
    GFX_clearScreen();
}

void writeScreenChar(int position, uint8_t ch) {
    if (ch > 32 && ch < 128) {
        printf("%4d %c\n", position, ch);
    }
    int textCol = position % 64;
    int textRow = position / 64;

    int ox = HMARGIN + textCol*4;
    int oy = VMARGIN + textRow*12;
    if (true) {
        for (int y = 0; y < 12; y++) {
            unsigned char b = Trs80FontBits[ch*12 + y];
            for (int x = 0; x < 4; x++) {
                int gray = (b >> (x*2)) & 0x03;
                GFX_drawPixel(ox + x, oy + y, COLORS[gray]);
            }
        }
    } else {
        uint16_t color = ch == 32 || ch == 128 ? BACKGROUND : FOREGROUND;
        GFX_drawPixel(ox, oy, color);
    }
}

int main() {
    bi_decl(bi_program_description("This is a test binary."));
    bi_decl(bi_1pin_with_name(LED_PIN, "On-board LED"));

    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    for (int i = 0; i < 3; i++) {
        gpio_put(LED_PIN, 0);
        sleep_ms(250);
        gpio_put(LED_PIN, 1);
        printf("Hello world %d\n", i);
        sleep_ms(750);
    }

    lcd_test();

    queueKey(1000000, 'L', true);
    queueKey(1010000, 'L', false);
    queueKey(2000000, '0', true);
    queueKey(2010000, '0', false);
    queueKey(3000000, '\n', true);
    queueKey(3010000, '\n', false);

    trs80_main();
}
