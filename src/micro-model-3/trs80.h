
#pragma once

#include <stdint.h>

// These match the TRS-80 byte 6 keyboard bits.
#define JOYSTICK_UP_MASK (1 << 3)
#define JOYSTICK_DOWN_MASK (1 << 4)
#define JOYSTICK_LEFT_MASK (1 << 5)
#define JOYSTICK_RIGHT_MASK (1 << 6)
#define JOYSTICK_FIRE_MASK (1 << 7)

int trs80_main();
void queueEvent(float seconds, void (*callback)(int data), int data);
void handleKeypress(int key, bool isPress);
void writeMemoryByte(uint16_t address, uint8_t value);
void jumpToAddress(uint16_t pc);


