
#pragma once

#include <stdint.h>

int trs80_main();
void queueEvent(float seconds, void (*callback)(int data), int data);
void handleKeypress(int key, bool isPress);
void writeMemoryByte(uint16_t address, uint8_t value);
void jumpToAddress(uint16_t pc);
