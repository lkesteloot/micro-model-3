
#pragma once

#include <stdint.h>

void writeScreenChar(int x, int y, uint8_t ch);
void writeScreenChar(int position, uint8_t ch);
uint8_t readJoystick();
