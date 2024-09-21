#include <stdio.h>
#include <cstring>
#include <chrono>
#include <map>
#include <deque>
#include <vector>
#include "fonts.h"
#include "z80user.h"
#include "z80emu.h"
#include "model3_rom.h"
#include "trs80.h"
#include "main.h"

/**
 * Emulator for a TRS-80 Model III. Based on the TypeScript version available here:
 * https://github.com/lkesteloot/trs80/tree/master/packages/trs80-emulator
 */

typedef long long clk_t;

constexpr clk_t Trs80ClockHz = 2027520;
constexpr clk_t Trs80TimerHz = 30;

constexpr int Trs80ColumnCount = 64;
constexpr int Trs80RowCount = 16;
constexpr int Trs80ScreenSize = Trs80ColumnCount*Trs80RowCount;
constexpr int Trs80ScreenBegin = 15*1024;
constexpr int Trs80ScreenEnd = Trs80ScreenBegin + Trs80ScreenSize;
constexpr int Trs80CharWidth = 8;
constexpr int Trs80CharHeight = 12;

// Handle keyboard mapping. The TRS-80 Model III keyboard has keys in different
// places, so we must occasionally fake a Shift key being up or down when it's
// really not.

// Whether to force a Shift key, and how.
enum ShiftState { ST_NEUTRAL, ST_FORCE_DOWN, ST_FORCE_UP };

// Keyboard is in several identical (mirrored) banks.
constexpr int Trs80KeyboardBankSize = 0x100;
constexpr int Trs80KeyboardBankCount = 4;
constexpr int Trs80KeyboardBegin = 0x3800;
constexpr int Trs80KeyboardEnd = Trs80KeyboardBegin + Trs80KeyboardBankSize*Trs80KeyboardBankCount;
constexpr int Trs80KeyboardThrottleCycles = 50000;

// Use these two for the byteIndex field of the KeyInfo:
// Use the unshifted data when shifted.
constexpr int KEYBOARD_USE_UNSHIFTED = -1;
// Ignore this key.
constexpr int KEYBOARD_IGNORE = -2;

// What we know about this keycap.
struct KeyInfo {
    // Static information about this keycap.
    int byteIndex;
    int bitNumber;
    ShiftState shiftForce = ST_NEUTRAL;

    // If these are non-zero, use them when Shift key is pressed.
    int shiftedByteIndex = KEYBOARD_USE_UNSHIFTED;
    int shiftedBitNumber = KEYBOARD_USE_UNSHIFTED;
    ShiftState shiftedShiftForce = ST_NEUTRAL;

    // Dynamic information about whether the Shift key was pressed when
    // the key was pressed. We use this when releasing the key to know
    // how to map it to the right TRS-80 raw key.
    bool shiftPressed = false;
};

// Structure to record a raw key event for queuing.
struct KeyEvent {
    int key;
    bool isPress;

    KeyEvent(int key, bool isPress) : key(key), isPress(isPress) {}
};

struct QueuedKeyEvent {
    // Since Z80 boot.
    unsigned long long microseconds;
    uint8_t ch;

    QueuedKeyEvent(unsigned long long microseconds, uint8_t ch) :
        microseconds(microseconds), ch(ch) {}
};

static std::vector<QueuedKeyEvent> gQueuedKeyEvents;

// IRQs
// constexpr uint8_t M1_TIMER_IRQ_MASK = 0x80;
// constexpr uint8_t M3_CASSETTE_RISE_IRQ_MASK = 0x01;
// constexpr uint8_t M3_CASSETTE_FALL_IRQ_MASK = 0x02;
constexpr uint8_t M3_TIMER_IRQ_MASK = 0x04;
// constexpr uint8_t M3_IO_BUS_IRQ_MASK = 0x08;
// constexpr uint8_t M3_UART_SED_IRQ_MASK = 0x10;
// constexpr uint8_t M3_UART_RECEIVE_IRQ_MASK = 0x20;
// constexpr uint8_t M3_UART_ERROR_IRQ_MASK = 0x40;
// constexpr uint8_t CASSETTE_IRQ_MASKS = M3_CASSETTE_RISE_IRQ_MASK | M3_CASSETTE_FALL_IRQ_MASK;

// NMIs
constexpr uint8_t RESET_NMI_MASK = 0x20;
// constexpr uint8_t DISK_MOTOR_OFF_NMI_MASK = 0x40;
// constexpr uint8_t DISK_INTRQ_NMI_MASK = 0x80;

// Holds the state of the physical machine.
typedef struct Trs80Machine {
    clk_t clock;
    Z80_STATE z80;
    uint8_t memory[MEMSIZE];

    // 8 bytes, each a bitfield of keys currently pressed.
    uint8_t keys[8];
    ShiftState shiftForce;
    bool leftShiftPressed;
    bool rightShiftPressed;
    clk_t keyProcessMinClock;
    // Map from Rosa's raw keycap to our key info structure.
    std::map<int, KeyInfo> keyMap;
    // We queue up key events so that we don't overwhelm the ROM polling
    // routines.
    std::deque<KeyEvent> keyQueue;

    // Which IRQs should be handled.
    uint8_t irqMask;
    // Which IRQs have been requested by the hardware.
    uint8_t irqLatch;
    // Which NMIs should be handled.
    uint8_t nmiMask;
    // Which NMIs have been requested by the hardware.
    uint8_t nmiLatch;
    // Whether we've seen this NMI and handled it.
    bool nmiSeen;
    // Latch that mostly does nothing.
    uint8_t modeImage;
} Trs80Machine;

// Record what values the system wants us to us for black and white.
static uint8_t NTSCBlack, NTSCWhite;

// Called by the system to initialize our video memory.
static int Trs80TextModeInit([[maybe_unused]] void *private_data,
        uint8_t black, uint8_t white)
{
    NTSCBlack = black;
    NTSCWhite = white;

    return 1;
}

static void Trs80TextModeFini([[maybe_unused]] void *private_data)
{
    // Nothing.
}

// Called by the system to know whether to generate the color burst signal.
static int Trs80TextModeNeedsColorburst()
{
    return 0;
}

// Generate the map from Rosa's keycap enum to the TRS-80's memory-mapped
// keyboard bytes.
static void initializeKeyboardMap(Trs80Machine *machine) {
    auto &m = machine->keyMap;

    m['0'] = { 4, 0, ST_NEUTRAL, 5, 1, ST_FORCE_DOWN };
    m['L'] = { 1, 4 };
    m['\n'] = { 6, 0, ST_NEUTRAL };

    /*
    m[KEYCAP_A] = { 0, 1 };
    m[KEYCAP_B] = { 0, 2 };
    m[KEYCAP_C] = { 0, 3 };
    m[KEYCAP_D] = { 0, 4 };
    m[KEYCAP_E] = { 0, 5 };
    m[KEYCAP_F] = { 0, 6 };
    m[KEYCAP_G] = { 0, 7 };
    m[KEYCAP_H] = { 1, 0 };
    m[KEYCAP_I] = { 1, 1 };
    m[KEYCAP_J] = { 1, 2 };
    m[KEYCAP_K] = { 1, 3 };
    m[KEYCAP_L] = { 1, 4 };
    m[KEYCAP_M] = { 1, 5 };
    m[KEYCAP_N] = { 1, 6 };
    m[KEYCAP_O] = { 1, 7 };
    m[KEYCAP_P] = { 2, 0 };
    m[KEYCAP_Q] = { 2, 1 };
    m[KEYCAP_R] = { 2, 2 };
    m[KEYCAP_S] = { 2, 3 };
    m[KEYCAP_T] = { 2, 4 };
    m[KEYCAP_U] = { 2, 5 };
    m[KEYCAP_V] = { 2, 6 };
    m[KEYCAP_W] = { 2, 7 };
    m[KEYCAP_X] = { 3, 0 };
    m[KEYCAP_Y] = { 3, 1 };
    m[KEYCAP_Z] = { 3, 2 };

    m[KEYCAP_1_EXCLAMATION] = { 4, 1 };
    m[KEYCAP_2_AT] = { 4, 2, ST_NEUTRAL, 0, 0, ST_FORCE_UP };
    m[KEYCAP_3_NUMBER] = { 4, 3 };
    m[KEYCAP_4_DOLLAR] = { 4, 4 };
    m[KEYCAP_5_PERCENT] = { 4, 5 };
    m[KEYCAP_6_CARET] = { 4, 6, ST_NEUTRAL, -2, -2 };
    m[KEYCAP_7_AMPERSAND] = { 4, 7, ST_NEUTRAL, 4, 6, ST_FORCE_DOWN };
    m[KEYCAP_8_ASTERISK] = { 5, 0, ST_NEUTRAL, 5, 2, ST_FORCE_DOWN };
    m[KEYCAP_9_OPAREN] = { 5, 1, ST_NEUTRAL, 5, 0, ST_FORCE_DOWN };
    m[KEYCAP_0_CPAREN] = { 4, 0, ST_NEUTRAL, 5, 1, ST_FORCE_DOWN };

    m[KEYCAP_SINGLEQUOTE_DOUBLEQUOTE] = { 4, 7, ST_FORCE_DOWN, 4, 2, ST_FORCE_DOWN };
    m[KEYCAP_ENTER] = { 6, 0, ST_NEUTRAL };
    m[KEYCAP_BACKSLASH_PIPE] = { 6, 1, ST_NEUTRAL, KEYBOARD_IGNORE }; // Clear
    m[KEYCAP_ESCAPE] = { 6, 2, ST_NEUTRAL, KEYBOARD_IGNORE }; // Break
    m[KEYCAP_UP] = { 6, 3, ST_NEUTRAL };
    m[KEYCAP_DOWN] = { 6, 4, ST_NEUTRAL };
    m[KEYCAP_LEFT] = { 6, 5, ST_NEUTRAL };
    m[KEYCAP_BACKSPACE] = { 6, 5, ST_NEUTRAL };
    m[KEYCAP_RIGHT] = { 6, 6, ST_NEUTRAL };
    m[KEYCAP_SPACE] = { 6, 7, ST_NEUTRAL };
    m[KEYCAP_LEFTSHIFT] = { 7, 0, ST_NEUTRAL };
    m[KEYCAP_RIGHTSHIFT] = { 7, 1, ST_NEUTRAL };
    // Simulate Shift-0, like trsemu:
    m[KEYCAP_HYPHEN_UNDER] = { 5, 5, ST_FORCE_UP, 4, 0, ST_FORCE_DOWN };
    m[KEYCAP_SEMICOLON_COLON] = { 5, 3, ST_FORCE_UP, 5, 2, ST_FORCE_UP };
    m[KEYCAP_COMMA_LESS] = { 5, 4 };
    m[KEYCAP_PERIOD_GREATER] = { 5, 6 };
    m[KEYCAP_SLASH_QUESTION] = { 5, 7 };
    m[KEYCAP_EQUAL_PLUS] = { 5, 5, ST_FORCE_DOWN, 5, 3, ST_FORCE_DOWN };
    m[KEYCAP_GRAVE_TILDE] = { 0, 0, ST_FORCE_DOWN, KEYBOARD_IGNORE };
    */
}

// Release all keys.
static void clearKeyboard(Trs80Machine *machine) {
    memset(machine->keys, 0, sizeof(machine->keys));
    machine->shiftForce = ST_NEUTRAL;
    machine->leftShiftPressed = false;
    machine->rightShiftPressed = false;
    machine->keyProcessMinClock = 0;
}

// Process the next queued key event, if available. Returns whether a key was
// dequeued.
static bool processKeyQueue(Trs80Machine *machine) {
    if (machine->keyQueue.empty()) {
        return false;
    }

    KeyEvent const &keyEvent = machine->keyQueue.front();
    machine->keyQueue.pop_front();
    int key = keyEvent.key;
    bool isPress = keyEvent.isPress;

    // Remember shift state.
    /*
    if (key == KEYCAP_LEFTSHIFT) {
        machine->leftShiftPressed = isPress;
    }
    if (key == KEYCAP_RIGHTSHIFT) {
        machine->rightShiftPressed = isPress;
    }
    */

    // Find the info for this keycap.
    auto itr = machine->keyMap.find(key);
    if (itr != machine->keyMap.end()) {
        KeyInfo &keyInfo = itr->second;

        // Remember shifted state for the release, in case the user releases
        // Shift before releasing the key.
        bool shiftPressed;
        if (isPress) {
            shiftPressed = machine->leftShiftPressed || machine->rightShiftPressed;
            keyInfo.shiftPressed = shiftPressed;
        } else {
            shiftPressed = keyInfo.shiftPressed;
        }

        // Use the alternate info if available for this keycap.
        bool haveShiftedData = keyInfo.shiftedByteIndex != KEYBOARD_USE_UNSHIFTED;
        bool useShiftedData = shiftPressed && haveShiftedData;
        int byteIndex = useShiftedData
            ? keyInfo.shiftedByteIndex
            : keyInfo.byteIndex;
        int bitNumber = useShiftedData
            ? keyInfo.shiftedBitNumber
            : keyInfo.bitNumber;

        if (byteIndex != KEYBOARD_IGNORE) {
            // Update the keyboard matrix bit.
            machine->shiftForce = useShiftedData
                ? keyInfo.shiftedShiftForce
                : keyInfo.shiftForce;
            uint8_t bit = 1 << bitNumber;
            if (isPress) {
                machine->keys[byteIndex] |= bit;
            } else {
                machine->keys[byteIndex] &= ~bit;
            }
        }
    }

    return true;
}

// Read a byte from the keyboard memory bank. This is an odd system where
// bits in the address map to the various bytes, and you can read the OR'ed
// addresses to read more than one byte at a time. For the last byte we fake
// the Shift key if necessary.
static uint8_t readKeyboard(Trs80Machine *machine, uint16_t addr) {
    addr = (addr - Trs80KeyboardBegin) % Trs80KeyboardBankSize;

    // Dequeue if necessary.
    if (machine->clock > machine->keyProcessMinClock) {
        bool keyWasPressed = processKeyQueue(machine);
        if (keyWasPressed) {
            machine->keyProcessMinClock = machine->clock + Trs80KeyboardThrottleCycles;
        }
    }

    // There are 8 keyboard bytes, and the corresponding bit of the I/O/ read
    // address tells you which of the 8 bytes are OR'ed into the result. Address 0
    // is always 0x00, and address 255 is the OR of all 8 bytes. To check
    // all the keys individually, you want to check address 1, 2, 4, 8, 16, etc.
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) {
        if ((addr & (1 << i)) != 0) {
            uint8_t keys = machine->keys[i];

            if (i == 7) {
                // Modify keys based on the shift force.
                switch (machine->shiftForce) {
                    case ST_NEUTRAL:
                        // Nothing.
                        break;

                    case ST_FORCE_UP:
                        // On the Model III the first two bits are left and right shift.
                        keys &= ~0x03;
                        break;

                    case ST_FORCE_DOWN:
                        keys |= 0x01;
                        break;
                }
            }

            b |= keys;
        }
    }

    return b;
}

// Handle a keypress on the real machine, update memory-mapped I/O.
static void handleKeypress(Trs80Machine *machine, int key, bool isPress) {
    machine->keyQueue.emplace_back(key, isPress);
}

/**
 * Set the mask for IRQ (regular) interrupts.
 */
static void setIrqMask(Trs80Machine *machine, uint8_t irqMask) {
    machine->irqMask = irqMask;
}

// Reset whether we've seen this NMI interrupt if the mask and latch no longer overlap.
static void updateNmiSeen(Trs80Machine *machine) {
    if ((machine->nmiLatch & machine->nmiMask) == 0) {
        machine->nmiSeen = false;
    }
}

/**
 * Set the mask for non-maskable interrupts. (Yes.)
 */
static void setNmiMask(Trs80Machine *machine, uint8_t nmiMask) {
    // Reset is always allowed:
    machine->nmiMask = nmiMask | RESET_NMI_MASK;
    updateNmiSeen(machine);
}

static uint8_t interruptLatchRead(Trs80Machine *machine) {
    return ~machine->irqLatch;
}

// Set or reset the timer interrupt.
void setTimerInterrupt(Trs80Machine *machine, bool state) {
    if (state) {
        machine->irqLatch |= M3_TIMER_IRQ_MASK;
    } else {
        machine->irqLatch &= ~M3_TIMER_IRQ_MASK;
    }
}

// Set the state of the reset button interrupt.
void resetButtonInterrupt(Trs80Machine *machine, bool state) {
    if (state) {
        machine->nmiLatch |= RESET_NMI_MASK;
    } else {
        machine->nmiLatch &= ~RESET_NMI_MASK;
    }
    updateNmiSeen(machine);
}

// What to do when the hardware timer goes off.
static void handleTimer(Trs80Machine *machine) {
    setTimerInterrupt(machine, true);
}

static void resetMachine(Trs80Machine *machine) {
    machine->clock = 0;
    machine->modeImage = 0x80;
    setIrqMask(machine, 0);
    machine->irqLatch = 0;
    setNmiMask(machine, 0);
    machine->nmiLatch = 0;
    // resetCassette(machine);
    clearKeyboard(machine);
    setTimerInterrupt(machine, false);
    Z80Reset(&machine->z80);
}

uint8_t Trs80ReadByte(Trs80Machine *machine, uint16_t address) {
    if (address >= Trs80KeyboardBegin && address < Trs80KeyboardEnd) {
        return readKeyboard(machine, address);
    }

    return machine->memory[address];
}

void Trs80WriteByte(Trs80Machine *machine, uint16_t address, uint8_t value) {
    if (address >= ROMSIZE) {
        if (address >= Trs80ScreenBegin && address < Trs80ScreenEnd) {
            writeScreenChar(address - Trs80ScreenBegin, value);
        }
        machine->memory[address] = value;
    }
}

uint8_t Trs80ReadPort(Trs80Machine *machine, uint8_t address) {
    uint8_t value = 0xFF;

    switch (address) {
        case 0xE0:
            // IRQ latch read.
            value = interruptLatchRead(machine);
            break;

        case 0xE4:
            // NMI latch read.
            value = ~machine->nmiLatch;
            break;

        case 0xEC:
        case 0xED:
        case 0xEE:
        case 0xEF:
            // Acknowledge timer.
            setTimerInterrupt(machine, false);
            break;

        case 0xF8:
            // Printer status. Printer selected, ready, with paper, not busy.
            value = 0x30;
            break;

        case 0xFF:
            // Cassette and various flags.
            value = machine->modeImage & 0x7E;
            // value |= this.getCassetteByte();
            break;
    }

#if 0
    printf("Read port 0x%02X to get 0x%02X\n", address, value);
#endif

    return value;
}

void Trs80WritePort(Trs80Machine *machine, uint8_t address, uint8_t value) {
#if 0
    printf("Write port 0x%02X value 0x%02X\n", address, value);
#endif

    switch (address) {
        case 0xE0:
            // Set interrupt mask.
            setIrqMask(machine, value);
            break;

        case 0xE4:
        case 0xE5:
        case 0xE6:
        case 0xE7:
            // Set NMI state.
            setNmiMask(machine, value);
            break;

        case 0xEC:
        case 0xED:
        case 0xEE:
        case 0xEF:
            // Various controls.
            machine->modeImage = value;
            // this.setCassetteMotor((value & 0x02) != 0);
            // this.screen.setExpandedCharacters((value & 0x04) != 0);
            // this.screen.setAlternateCharacters((value & 0x08) == 0);
            break;
    }
}

void queueKey(long long microseconds, uint8_t ch) {
    gQueuedKeyEvents.emplace_back(microseconds, ch);
}

int trs80_main()
{
    bool quit = false;
    Trs80Machine *machine = new Trs80Machine;

    // Read the ROM.
    if (MODEL3_ROM_SIZE != ROMSIZE) {
        printf("ROM is wrong size (%zd bytes)\n", MODEL3_ROM_SIZE);
        while (1) {}
    }
    memcpy(machine->memory, MODEL3_ROM, MODEL3_ROM_SIZE);

    initializeKeyboardMap(machine);
    resetMachine(machine);

    clk_t previousTimerClock = 0;

    auto emulationStartTime = std::chrono::system_clock::now();

    while (!quit) {
        clk_t cyclesToDo = 10000;

        // See if we should interrupt the emulator early for our timer interrupt.
        clk_t nextTimerClock = previousTimerClock + Trs80ClockHz / Trs80TimerHz;
        if (nextTimerClock >= machine->clock) {
            clk_t clocksUntilTimer = nextTimerClock - machine->clock;
            if (cyclesToDo > clocksUntilTimer) {
                cyclesToDo = clocksUntilTimer;
            }
        }

        // See if we should slow down if we're going too fast.
        auto now = std::chrono::system_clock::now();
        auto microsSinceStart = std::chrono::duration_cast<std::chrono::microseconds>(now - emulationStartTime);
        if (!gQueuedKeyEvents.empty() && gQueuedKeyEvents[0].microseconds < microsSinceStart.count()) {
            printf("Inserting key event (%d) (%llu < %llu)\n", 
                    gQueuedKeyEvents[0].ch,
                    gQueuedKeyEvents[0].microseconds,
                    microsSinceStart.count());
            handleKeypress(machine, gQueuedKeyEvents[0].ch, true);
            handleKeypress(machine, gQueuedKeyEvents[0].ch, false);
            gQueuedKeyEvents.erase(gQueuedKeyEvents.begin());
        }
        clk_t expectedClock = Trs80ClockHz * microsSinceStart.count() / 1000000;
        if (expectedClock < machine->clock) {
#if 0
            printf("Skipping because %lld < %lld (%d left)\n",
                    expectedClock, machine->clock, machine->clock - expectedClock);
#endif
            continue;
        }

        // Emulate!
        int doneCycles = Z80Emulate(&machine->z80, cyclesToDo, machine);
        machine->clock += doneCycles;
#if 0
        printf("E %llu 0x%04X %lld %d\n", machine->clock, machine->z80.pc, cyclesToDo, doneCycles);
#endif

        // Handle non-maskable interrupts.
        if ((machine->nmiLatch & machine->nmiMask) != 0 && !machine->nmiSeen) {
#if 0
            printf("N %llu 0x%04X\n", machine->clock, machine->z80.pc);
#endif
            machine->clock += Z80NonMaskableInterrupt(&machine->z80, machine);
            machine->nmiSeen = true;

            // Simulate the reset button being released.
            resetButtonInterrupt(machine, false);
        }

        // Handle interrupts.
        if ((machine->irqLatch & machine->irqMask) != 0) {
#if 0
            printf("I %llu 0x%04X 0x%02X 0x%02X %d\n", machine->clock, machine->z80.pc,
                    machine->irqLatch, machine->irqMask, machine->z80.iff1);
#endif
            machine->clock += Z80Interrupt(&machine->z80, 0, machine);
        }

        // Set off a timer interrupt.
        if (machine->clock > nextTimerClock) {
#if 0
            printf("T %llu 0x%04X\n", machine->clock, machine->z80.pc);
#endif
            handleTimer(machine);
            previousTimerClock = machine->clock;
        }

        // Check user input.
        /*
        RoEvent ev;
        int haveEvent = RoEventPoll(&ev);

        if (haveEvent) {
            switch(ev.eventType) {
                case RoEvent::KEYBOARD_RAW: {
                    const struct KeyboardRawEvent raw = ev.u.keyboardRaw;
                    handleKeypress(machine, raw.key, raw.isPress);
                    break;
                }
                
                default:
                    // pass;
                    break;
            }
        }
        RoDoHousekeeping();
        */
    }

    return 0;
}
