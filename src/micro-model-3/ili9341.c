#include "pico/stdlib.h"
#include "hardware/dma.h"

#include "ili9341.h"

// Constants for ILI9341_MADCTL:
#define MADCTL_MY 0x80  // Bottom to top
#define MADCTL_MX 0x40  // Right to left
#define MADCTL_MV 0x20  // Reverse axes
#define MADCTL_ML 0x10  // LCD refresh bottom to top
#define MADCTL_RGB 0x00 // Red-Green-Blue pixel order
#define MADCTL_BGR 0x08 // Blue-Green-Red pixel order
#define MADCTL_MH 0x04  // LCD refresh right to left

// Unrotated size of the display:
#define ILI9341_TFTWIDTH 240
#define ILI9341_TFTHEIGHT 320

#define ILI9341_NOP 0x00     // No-op register
#define ILI9341_SWRESET 0x01 // Software reset register
#define ILI9341_RDDID 0x04   // Read display identification information
#define ILI9341_RDDST 0x09   // Read Display Status

#define ILI9341_SLPIN 0x10  // Enter Sleep Mode
#define ILI9341_SLPOUT 0x11 // Sleep Out
#define ILI9341_PTLON 0x12  // Partial Mode ON
#define ILI9341_NORON 0x13  // Normal Display Mode ON

#define ILI9341_RDMODE 0x0A     // Read Display Power Mode
#define ILI9341_RDMADCTL 0x0B   // Read Display MADCTL
#define ILI9341_RDPIXFMT 0x0C   // Read Display Pixel Format
#define ILI9341_RDIMGFMT 0x0D   // Read Display Image Format
#define ILI9341_RDSELFDIAG 0x0F // Read Display Self-Diagnostic Result

#define ILI9341_INVOFF 0x20   // Display Inversion OFF
#define ILI9341_INVON 0x21    // Display Inversion ON
#define ILI9341_GAMMASET 0x26 // Gamma Set
#define ILI9341_DISPOFF 0x28  // Display OFF
#define ILI9341_DISPON 0x29   // Display ON

#define ILI9341_CASET 0x2A // Column Address Set
#define ILI9341_PASET 0x2B // Page Address Set
#define ILI9341_RAMWR 0x2C // Memory Write
#define ILI9341_RAMRD 0x2E // Memory Read

#define ILI9341_PTLAR 0x30    // Partial Area
#define ILI9341_VSCRDEF 0x33  // Vertical Scrolling Definition
#define ILI9341_MADCTL 0x36   // Memory Access Control
#define ILI9341_VSCRSADD 0x37 // Vertical Scrolling Start Address
#define ILI9341_PIXFMT 0x3A   // COLMOD: Pixel Format Set

#define ILI9341_FRMCTR1 0xB1 // Frame Rate Control (In Normal Mode/Full Colors)
#define ILI9341_FRMCTR2 0xB2 // Frame Rate Control (In Idle Mode/8 colors)
#define ILI9341_FRMCTR3 0xB3 // Frame Rate control (In Partial Mode/Full Colors)
#define ILI9341_INVCTR 0xB4  // Display Inversion Control
#define ILI9341_DFUNCTR 0xB6 // Display Function Control

#define ILI9341_PWCTR1 0xC0 // Power Control 1
#define ILI9341_PWCTR2 0xC1 // Power Control 2
#define ILI9341_PWCTR3 0xC2 // Power Control 3
#define ILI9341_PWCTR4 0xC3 // Power Control 4
#define ILI9341_PWCTR5 0xC4 // Power Control 5
#define ILI9341_VMCTR1 0xC5 // VCOM Control 1
#define ILI9341_VMCTR2 0xC7 // VCOM Control 2

#define ILI9341_RDID1 0xDA // Read ID 1
#define ILI9341_RDID2 0xDB // Read ID 2
#define ILI9341_RDID3 0xDC // Read ID 3
#define ILI9341_RDID4 0xDD // Read ID 4

#define ILI9341_GMCTRP1 0xE0 // Positive Gamma Correction
#define ILI9341_GMCTRN1 0xE1 // Negative Gamma Correction

static uint16_t gWidth;  // Display width as modified by current rotation
static uint16_t gHeight; // Display height as modified by current rotation
static spi_inst_t *gSpi = spi_default;

static uint16_t gPinCs = PICO_DEFAULT_SPI_CSN_PIN;
static uint16_t gPinDc = 20;
static int16_t gPinRst = 16;
static uint16_t gPinSck = PICO_DEFAULT_SPI_SCK_PIN;
static uint16_t gPinTx = PICO_DEFAULT_SPI_TX_PIN;

static uint gDmaChannel;
static dma_channel_config gDmaConfig;

// Clean up SPI after a DMA write. This code is copied from spi_write16_blocking().
static void flushSpi(spi_inst_t *spi) {
    while (spi_is_readable(spi)) {
        (void) spi_get_hw(spi)->dr;
    }
    while ((spi_get_hw(spi)->sr & SPI_SSPSR_BSY_BITS) != 0) {
        tight_loop_contents();
    }
    while (spi_is_readable(spi)) {
        (void) spi_get_hw(spi)->dr;
    }

    // Don't leave overrun flag set
    spi_get_hw(spi)->icr = SPI_SSPICR_RORIC_BITS;
}

static const uint8_t gInitializationCommands[] = {
    // 24 commands to follow.
    24,

    // Undocumented.
    0xEF, 3, 0x03, 0x80, 0x02,

    // Undocumented.
    0xCF, 3, 0x00, 0xC1, 0x30,

    // Undocumented.
    0xED, 4, 0x64, 0x03, 0x12, 0x81,

    // Undocumented.
    0xE8, 3, 0x85, 0x00, 0x78,

    // Undocumented.
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,

    // Undocumented.
    0xF7, 1, 0x20,

    // Undocumented.
    0xEA, 2, 0x00, 0x00,

    // Set grayscale voltage level. 0x23 is 4.6 V.
    ILI9341_PWCTR1, 1, 0x23,

    // Set the factor used in the step-up circuits. 0x10 is the default value.
    ILI9341_PWCTR2, 1, 0x10,

    // Set the VCOMH voltage (0x3e is 4.250 V) and VCOML voltage (0x28 is -1.500 V).
    ILI9341_VMCTR1, 2, 0x3e, 0x28,

    // Set the VCOM offset voltage. 0x86 is VMH-58, VML-58.
    ILI9341_VMCTR2, 1, 0x86,

    // Set memory access control to normal screen orientation.
    ILI9341_MADCTL, 1, MADCTL_MX | MADCTL_BGR,

    // Set vertical scrolling start address. In the docs this has two parameters.
    ILI9341_VSCRSADD, 1, 0x00,

    // Set pixel format. 0x55 is 16 bits per pixel.
    ILI9341_PIXFMT, 1, 0x55,

    // Set frame rate. DIVA = 0, RTNA = 79 Hz (?).
    ILI9341_FRMCTR1, 2, 0x00, 0x18,

    // Set various display controls. In the docs this has four parameters.
    ILI9341_DFUNCTR, 3, 0x08, 0x82, 0x27,

    // 3Gamma function disable. (Undocumented)
    0xF2, 1, 0x00,

    // Set the gamma curve (gamma 2.2).
    ILI9341_GAMMASET, 1, 0x01,

    // Set positive gamma correction.
    ILI9341_GMCTRP1, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
        0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,

    // Set negative gamma correction.
    ILI9341_GMCTRN1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
        0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,

    // Exit sleep mode.
    ILI9341_SLPOUT, 0x80,

    // Turn on the display.
    ILI9341_DISPON, 0x80,
};

static void initializeSpi() {
    // Configure SPI at 40 MHz.
    spi_init(gSpi, 1000 * 40000);
    spi_set_format(gSpi, 16, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    gpio_set_function(gPinSck, GPIO_FUNC_SPI);
    gpio_set_function(gPinTx, GPIO_FUNC_SPI);

    gpio_init(gPinCs);
    gpio_set_dir(gPinCs, GPIO_OUT);
    gpio_put(gPinCs, 1);

    gpio_init(gPinDc);
    gpio_set_dir(gPinDc, GPIO_OUT);
    gpio_put(gPinDc, 1);

    if (gPinRst != -1) {
        gpio_init(gPinRst);
        gpio_set_dir(gPinRst, GPIO_OUT);
        gpio_put(gPinRst, 1);
    }

    gDmaChannel = dma_claim_unused_channel(true);
    gDmaConfig = dma_channel_get_default_config(gDmaChannel);
    channel_config_set_transfer_data_size(&gDmaConfig, DMA_SIZE_16);
    channel_config_set_dreq(&gDmaConfig, spi_get_dreq(gSpi, true));
}

// Select the ILI9341.
static void ILI9341_Select() {
    gpio_put(gPinCs, 0);
}

// No longer select the ILI9341.
static void ILI9341_Deselect() {
    gpio_put(gPinCs, 1);
}

// Subsequent writes are commands.
static void ILI9341_RegCommand() {
    gpio_put(gPinDc, 0);
}

// Subsequent writes are data.
static void ILI9341_RegData() {
    gpio_put(gPinDc, 1);
}

// Write a command.
static void ILI9341_WriteCommand(uint8_t command) {
    ILI9341_RegCommand();
    spi_set_format(gSpi, 8, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    spi_write_blocking(gSpi, &command, sizeof(command));
}

// Write data.
static void ILI9341_WriteData(uint8_t const *data, size_t count) {
    ILI9341_RegData();
    spi_set_format(gSpi, 8, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    spi_write_blocking(gSpi, data, count);
}

// Send a command followed by data.
static void ILI9341_SendCommand(uint8_t command, uint8_t const *data, uint8_t count) {
    ILI9341_Select();
    ILI9341_WriteCommand(command);
    ILI9341_WriteData(data, count);
    ILI9341_Deselect();
}

// Reset the ILI9341, using a hardware pin if available, otherwise using the reset command.
// The chip must already be selected.
static void ILI9341_Reset() {
    if (gPinRst == -1) {
        ILI9341_WriteCommand(ILI9341_SWRESET);
    } else {
        gpio_put(gPinRst, 0);
        sleep_ms(5);
        gpio_put(gPinRst, 1);
    }
    sleep_ms(150);
}

// Start a write to the specified rectangle. Follow this with w*h colors.
static void LCD_configureWriteToRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    // Pack first and last coordinate into 32-bit int.
    uint32_t xa = ((uint32_t) x << 16) | (x + w - 1);
    uint32_t ya = ((uint32_t) y << 16) | (y + h - 1);

    // Swap the order of the bytes. The ILI9341 wants the high byte first, but
    // we're a little-endian machine.
    xa = __builtin_bswap32(xa);
    ya = __builtin_bswap32(ya);

    // Column address set.
    ILI9341_WriteCommand(ILI9341_CASET);
    ILI9341_WriteData((uint8_t *) &xa, sizeof(xa));

    // Row address set.
    ILI9341_WriteCommand(ILI9341_PASET);
    ILI9341_WriteData((uint8_t *) &ya, sizeof(ya));

    // Start writing w*h colors to memory, in the above rectangle.
    ILI9341_WriteCommand(ILI9341_RAMWR);
}

void LCD_setPins(uint16_t dc, uint16_t cs, int16_t rst, uint16_t sck, uint16_t tx) {
    gPinDc = dc;
    gPinCs = cs;
    gPinRst = rst;
    gPinSck = sck;
    gPinTx = tx;
}

void LCD_setSPIperiph(spi_inst_t *s) {
    gSpi = s;
}

void LCD_initDisplay() {
    initializeSpi();
    ILI9341_Select();
    ILI9341_Reset();

    uint8_t const *addr = gInitializationCommands;
    int commandCount = *addr++;
    while (commandCount--) {
        uint8_t command = *addr++;
        uint8_t flags = *addr++;
        int numArgs = flags & 0x7F;
        bool delay = flags & 0x80;

        ILI9341_SendCommand(command, addr, numArgs);
        addr += numArgs;

        if (delay) {
            sleep_ms(150);
        }
    }

    gWidth = ILI9341_TFTWIDTH;
    gHeight = ILI9341_TFTHEIGHT;
}

void LCD_setRotation(int rotation) {
    uint8_t command;

    switch (rotation % 4) {
        case LCD_ROTATION_NONE:
            command = MADCTL_MX;
            gWidth = ILI9341_TFTWIDTH;
            gHeight = ILI9341_TFTHEIGHT;
            break;

        case LCD_ROTATION_90:
            command = MADCTL_MV;
            gWidth = ILI9341_TFTHEIGHT;
            gHeight = ILI9341_TFTWIDTH;
            break;

        case LCD_ROTATION_180:
            command = MADCTL_MY;
            gWidth = ILI9341_TFTWIDTH;
            gHeight = ILI9341_TFTHEIGHT;
            break;

        case LCD_ROTATION_270:
            command = MADCTL_MX | MADCTL_MY | MADCTL_MV;
            gWidth = ILI9341_TFTHEIGHT;
            gHeight = ILI9341_TFTWIDTH;
            break;
    }

    command |= MADCTL_BGR;

    ILI9341_SendCommand(ILI9341_MADCTL, &command, 1);
}

uint16_t LCD_getWidth() {
    return gWidth;
}

uint16_t LCD_getHeight() {
    return gHeight;
}

void LCD_writePixel(int x, int y, uint16_t color) {
    ILI9341_Select();
    LCD_configureWriteToRect(x, y, 1, 1);
    ILI9341_RegData();
    spi_set_format(gSpi, 16, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);
    spi_write16_blocking(gSpi, &color, 1);
    ILI9341_Deselect();
}

void LCD_writeBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
    ILI9341_Select();
    LCD_configureWriteToRect(x, y, w, h);
    ILI9341_RegData();
    spi_set_format(gSpi, 16, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);

    channel_config_set_read_increment(&gDmaConfig, true);
    channel_config_set_write_increment(&gDmaConfig, false);
    dma_channel_configure(gDmaChannel, &gDmaConfig,
            &spi_get_hw(gSpi)->dr,
            bitmap,
            (uint) w * h,
            true);
    dma_channel_wait_for_finish_blocking(gDmaChannel);

    flushSpi(gSpi);

    ILI9341_Deselect();
}

void LCD_fillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    ILI9341_Select();
    LCD_configureWriteToRect(x, y, w, h);
    ILI9341_RegData();
    spi_set_format(gSpi, 16, SPI_CPOL_1, SPI_CPOL_1, SPI_MSB_FIRST);

    channel_config_set_read_increment(&gDmaConfig, false);
    channel_config_set_write_increment(&gDmaConfig, false);
    dma_channel_configure(gDmaChannel, &gDmaConfig,
            &spi_get_hw(gSpi)->dr,
            &color,
            (int) w * h,
            true);
    dma_channel_wait_for_finish_blocking(gDmaChannel);

    flushSpi(gSpi);

    ILI9341_Deselect();
}
