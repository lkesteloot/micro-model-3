#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"

#include "ili9341.h"
#include "trs80.h"
#include "main.h"
#include "fonts.h"
#include "obstacle_run_cmd.h"
#include "scarfman2_cmd.h"
#include "defense_command_cmd.h"
#include "sea_dragon_cmd.h"
#include "splash.h"

#define TFT_SCLK        18
#define TFT_MOSI        19
#define TFT_DC          20
#define TFT_RST         21
#define TFT_CS          17

#define TFT_ROTATION    1

#define SCREEN_WIDTH 64
#define SCREEN_HEIGHT 16

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

#define JOYSTICK_UP_PIN 6
#define JOYSTICK_DOWN_PIN 2
#define JOYSTICK_LEFT_PIN 3
#define JOYSTICK_RIGHT_PIN 4
#define JOYSTICK_FIRE_PIN 5

#define FONT_CHAR_COUNT 256
#define FONT_WIDTH 4
#define FONT_HEIGHT 12
#define FONT_CHAR_SIZE (FONT_WIDTH*FONT_HEIGHT)

/**
 * A key we should handle in a menu.
 */
struct MenuKey {
    // The text to recognize on the screen.
    const char *text;
    // Where on the screen it is.
    int position;
    // Key to map the fire button to if the text is at this location.
    char key;
};

struct Game {
    size_t cmdSize;
    uint8_t *cmd;
    std::vector<MenuKey> menuKeys;
};

namespace {
    uint16_t gFontGlyphs[FONT_CHAR_COUNT*FONT_CHAR_SIZE];

    uint16_t COLORS[4] = {
        RGB888TO565(0x00, 0x00, 0x00),
        RGB888TO565(0x80, 0x80, 0x80),
        RGB888TO565(0x80, 0x80, 0x80),
        RGB888TO565(0xFF, 0xFF, 0xFF),
    };

    bool mFirePressed = false;
    bool mFireSwallowed = false;

    const std::vector<Game> gGameList = {
        {
            .cmdSize = OBSTACLE_RUN_CMD_SIZE,
            .cmd = OBSTACLE_RUN_CMD,
            .menuKeys = {
                {
                    // Main menu, press Clear to start game.
                    .text = "YOU ARE",
                    .position = 0x0055,
                    .key = '\\',
                },
                {
                    // Instructions screen, press Clear to start game.
                    .text = "OBSTACLE RUN",
                    .position = 0x0092,
                    .key = '\\',
                },
                {
                    // Player menu, press "1" to start game.
                    .text = "Enter number of players",
                    .position = 0x020F,
                    .key = '1',
                },
            },
        },
        {
            .cmdSize = SCARFMAN2_CMD_SIZE,
            .cmd = SCARFMAN2_CMD,
            .menuKeys = {
                {
                    // Scarfman end of game, press Enter to restart.
                    .text = "G A M E   O V E R",
                    .position = 0x0157,
                    .key = '\n',
                },
            },
        },
        {
            .cmdSize = DEFENSE_COMMAND_CMD_SIZE,
            .cmd = DEFENSE_COMMAND_CMD,
            .menuKeys = {
                {
                    // Splash screen, 1 player to begin.
                    .text = "Players to Start the Game",
                    .position = 0x039C,
                    .key = '1',
                },
            },
        },
        {
            .cmdSize = SEA_DRAGON_CMD_SIZE,
            .cmd = SEA_DRAGON_CMD,
            .menuKeys = {
                {
                    // Splash screen, Enter to begin.
                    .text = "to Begin",
                    .position = 0x0321,
                    .key = '\n',
                },
                {
                    // Player menu, press "1" to start game.
                    .text = "1 or 2 Players?",
                    .position = 0x0358,
                    .key = '1',
                },
                {
                    // Skill level, 0 for novice.
                    .text = "Skill level",
                    .position = 0x0315,
                    .key = '0',
                },
            },
        },
    };
    // Current game.
    Game const *mGame = nullptr;

    void configureGpio() {
        gpio_init(LED_PIN);
        gpio_set_dir(LED_PIN, GPIO_OUT);

        gpio_init(JOYSTICK_UP_PIN);
        gpio_set_dir(JOYSTICK_UP_PIN, GPIO_IN);
        gpio_pull_up(JOYSTICK_UP_PIN);

        gpio_init(JOYSTICK_DOWN_PIN);
        gpio_set_dir(JOYSTICK_DOWN_PIN, GPIO_IN);
        gpio_pull_up(JOYSTICK_DOWN_PIN);

        gpio_init(JOYSTICK_LEFT_PIN);
        gpio_set_dir(JOYSTICK_LEFT_PIN, GPIO_IN);
        gpio_pull_up(JOYSTICK_LEFT_PIN);

        gpio_init(JOYSTICK_RIGHT_PIN);
        gpio_set_dir(JOYSTICK_RIGHT_PIN, GPIO_IN);
        gpio_pull_up(JOYSTICK_RIGHT_PIN);

        gpio_init(JOYSTICK_FIRE_PIN);
        gpio_set_dir(JOYSTICK_FIRE_PIN, GPIO_IN);
        gpio_pull_up(JOYSTICK_FIRE_PIN);
    }

    void configureLcd() {
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

    void showSplashScreen() {
        int marginLines = SCREEN_HEIGHT - SPLASH_NUM_LINES;
        int topMarginLines = marginLines / 2;
        int bottomMarginLines = marginLines - topMarginLines;
        uint16_t addr = 15360;

        // Top margin.
        for (int line = 0; line < topMarginLines; line++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                writeMemoryByte(addr++, ' ');
            }
        }

        // Screen.
        uint8_t *s = SPLASH;
        for (int line = 0; line < SPLASH_NUM_LINES; line++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                writeMemoryByte(addr++, *s++);
            }
        }

        // Bottom margin.
        for (int line = 0; line < bottomMarginLines; line++) {
            for (int x = 0; x < SCREEN_WIDTH; x++) {
                writeMemoryByte(addr++, ' ');
            }
        }

        sleep_ms(2000);
    }

    void keyCallback(int ch) {
        handleKeypress(ch, true);
        handleKeypress(ch, false);
    }

    void runCmdProgram(int program) {
        showSplashScreen();

        if (program < 0 || program >= gGameList.size()) {
            program = 0;
        }

        mGame = &gGameList[program];

        int size = mGame->cmdSize;
        uint8_t *binary = mGame->cmd;

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

    /**
     * The characters 32 and 128 look the same, so we normalize them to 128
     * when comparing strings.
     */
    uint8_t normalizeChar(uint8_t ch) {
        return ch == 32 ? 128 : ch;
    }

    /**
     * Whether the string is at the given position (within the screen).
     */
    bool textIsAt(char const *s, int position) {
        // printf("textIsAt()\n");
        while (*s != '\0') {
            uint8_t mem = readMemoryByte(15360 + position);
            // printf("    position = %04x, s = %d, screen = %d\n", position, (int) *s, (int) mem);
            if (normalizeChar(mem) != normalizeChar(*s)) {
                return false;
            }
            s += 1;
            position += 1;
        }
        return true;
    }
}

void writeScreenChar(int x, int y, uint8_t ch) {
    LCD_writeBitmap(
            LEFT_MARGIN + x*FONT_WIDTH,
            TOP_MARGIN + y*FONT_HEIGHT,
            FONT_WIDTH,
            FONT_HEIGHT,
            &gFontGlyphs[ch*FONT_CHAR_SIZE]);
}

void writeScreenChar(int position, uint8_t ch) {
    int x = position % SCREEN_WIDTH;
    int y = position / SCREEN_WIDTH;
    writeScreenChar(x, y, ch);
}

void pollInput() {
    // Get GPIO state.
    bool up = !gpio_get(JOYSTICK_UP_PIN);
    bool down = !gpio_get(JOYSTICK_DOWN_PIN);
    bool left = !gpio_get(JOYSTICK_LEFT_PIN);
    bool right = !gpio_get(JOYSTICK_RIGHT_PIN);
    bool fire = !gpio_get(JOYSTICK_FIRE_PIN);

    // Simulate various keys.
    if (fire) {
        if (!mFirePressed && mGame != nullptr) {
            // Just pressed the fire button. See if we're in a menu and should
            // submit a special key.
            for (MenuKey const &menuKey : mGame->menuKeys) {
                if (textIsAt(menuKey.text, menuKey.position)) {
                    handleKeypress(menuKey.key, true);
                    handleKeypress(menuKey.key, false);
                    mFireSwallowed = true;
                    break;
                }
            }
        }
    } else {
        mFireSwallowed = false;
    }
    mFirePressed = fire;

    uint8_t joystick =
        (up ? JOYSTICK_UP_MASK : 0) |
        (down ? JOYSTICK_DOWN_MASK : 0) |
        (left ? JOYSTICK_LEFT_MASK : 0) |
        (right ? JOYSTICK_RIGHT_MASK : 0) |
        (fire && !mFireSwallowed ? JOYSTICK_FIRE_MASK : 0);

    setJoystick(joystick);
}

int main() {
    stdio_init_all();

    configureGpio();
    prepareFontBitmaps();
    configureLcd();

#if 0
    // Basic ROM:
    queueEvent(1, keyCallback, 'L');
    queueEvent(2, keyCallback, '0');
    queueEvent(3, keyCallback, '\n');
#endif

    // Obstacle Run:
    queueEvent(0.1, runCmdProgram, 2);

    trs80_main();
}
