#include <stdio.h>
#include <string.h>
#include <string>
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

#define TFT_ROTATION    1

#define CMD_LOAD_BLOCK 0x01
#define CMD_TRANSFER_ADDRESS 0x02
#define CMD_LOAD_MODULE_HEADER 0x05


// Colors are in 565 (FFFF) format. To convert from RGB888 to RGB565, use:
#define RGB888TO565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define BACKGROUND RGB888TO565(0x00, 0x00, 0x00)
#define FOREGROUND RGB888TO565(0xFF, 0xFF, 0xFF)

// Centered:
// #define LEFT_MARGIN 32
// #define TOP_MARGIN 24
// Screen in upper-left of display:
#define LEFT_MARGIN 0
#define TOP_MARGIN 0

const uint LED_PIN = 25;

#define FONT_CHAR_COUNT 256
#define FONT_WIDTH 4
#define FONT_HEIGHT 12
#define FONT_CHAR_SIZE (FONT_WIDTH*FONT_HEIGHT)

static uint16_t gFontGlyphs[FONT_CHAR_COUNT*FONT_CHAR_SIZE];

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

void prepareFontBitmaps() {
    uint16_t *p = gFontGlyphs;

    for (int ch = 0; ch < FONT_CHAR_COUNT; ch++) {
        for (int y = 0; y < FONT_HEIGHT; y++) {
            uint8_t b = Trs80FontBits[ch*FONT_HEIGHT + y];
            for (int x = 0; x < FONT_WIDTH; x++) {
                int gray = (b >> (x*2)) & 0x03;
                *p++ = COLORS[gray];
            }
        }
    }
}

void writeScreenChar(int position, uint8_t ch) {
#if 0
    if (ch > 32 && ch < 128) {
        printf("%4d %c\n", position, ch);
    }
#endif
    int textCol = position % 64;
    int textRow = position / 64;

    LCD_writeBitmap(
            LEFT_MARGIN + textCol*FONT_WIDTH,
            TOP_MARGIN + textRow*FONT_HEIGHT,
            FONT_WIDTH,
            FONT_HEIGHT,
            &gFontGlyphs[ch*FONT_CHAR_SIZE]);
}

void keyCallback(int ch) {
    handleKeypress(ch, true);
    handleKeypress(ch, false);
}

void runCmdProgram(int program) {
    int size;
    uint8_t *binary;

    switch (program) {
        case 0:
        default:
            size = OBSTACLE_RUN_CMD_SIZE;
            binary = OBSTACLE_RUN_CMD;
            break;
    }

    int i = 0;
    while (true) {
        if (i >= size) {
            printf("CMD program ran off the end (%d >= %d)\n", i, size);
            return;
        }

        int chunkType = binary[i++];
        int chunkLength = binary[i++];

        // Adjust load block length.
        if (chunkType == CMD_LOAD_BLOCK && chunkLength <= 2) {
            chunkLength += 256;
        } else if (chunkType == CMD_LOAD_MODULE_HEADER && chunkLength == 0) {
            chunkLength = 256;
        }

        uint8_t *data = &binary[i];
        i += chunkLength;

        switch (chunkType) {
            case CMD_LOAD_BLOCK: {
                uint16_t address = data[0] | (data[1] << 8);
                int dataLength = chunkLength - 2;
                // printf("CMD loading %d bytes at 0x%04X\n", dataLength, address);
                for (int i = 0; i < dataLength; i++) {
                    writeMemoryByte(address + i, data[2 + i]);
                }
                break;
            }

            case CMD_TRANSFER_ADDRESS: {
                uint16_t address = data[0] | (data[1] << 8);
                // printf("CMD jumping to 0x%04X\n", address);
                jumpToAddress(address);
                // Stop parsing.
                return;
            }

            case CMD_LOAD_MODULE_HEADER: {
                std::string name((char *) data, chunkLength);
                // printf("CMD loading \"%s\"\n", name.c_str());
                break;
            }

            default:
                printf("Unknown CMD chunk type %d\n", chunkType);
                return;
        }
    }
}

int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    prepareFontBitmaps();
    configure_lcd();

#if 0
    queueEvent(1, keyCallback, 'L');
    queueEvent(2, keyCallback, '0');
    queueEvent(3, keyCallback, '\n');
    queueEvent(4, runCmdProgram, 0);
#endif

    queueEvent(0.1, runCmdProgram, 0);

    trs80_main();
}
