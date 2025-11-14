
#pragma once

#include <stdint.h>

// Called from trs80:
void writeScreenChar(int x, int y, uint8_t ch);
void writeScreenChar(int position, uint8_t ch);
void pollInput();
void trs80Idle();

// Called from settings:
void updateColorFromSettings(int color);
void updateSoundFromSettings(int sound);

