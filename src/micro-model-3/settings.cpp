
#include "settings.h"
#include "main.h"

static int gColor;
static int gSound;

void initSettings() {
    gColor = COLOR_WHITE;
    gSound = SOUND_OFF;
    // Don't call callbacks, it's their responsibility to check them.
}

int getSettingsColor() {
    return gColor;
}

int getSettingsSound() {
    return gSound;
}

void setSettingsColor(int color) {
    gColor = color;
    updateColorFromSettings(color);
}

void setSettingsSound(int sound) {
    gSound = sound;
    updateSoundFromSettings(sound);
}

