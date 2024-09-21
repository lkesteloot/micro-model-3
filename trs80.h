
#pragma once

int trs80_main();
void queueEvent(float seconds, void (*callback)(int data), int data);
void handleKeypress(int key, bool isPress);
