
#pragma once

#define COLOR_WHITE 0
#define COLOR_GREEN 1
#define COLOR_AMBER 2
#define SOUND_OFF 0
#define SOUND_ON 1

void initSettings();
int getSettingsColor();
int getSettingsSound();
void setSettingsColor(int color);
void setSettingsSound(int sound);

