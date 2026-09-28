#pragma once

#include <Arduino.h>

// CrowPanel 4.2" front controls: MENU and EXIT keys plus a 3-way rotary
// switch (rotate up / press / rotate down).
enum class Button : uint8_t { Menu, Up, Ok, Down, Exit };
static constexpr int BUTTON_COUNT = 5;

const char* buttonName(Button b);

// Starts a background task that debounces the buttons and queues presses, so
// presses made while the panel is busy refreshing are not lost.
bool buttonsBegin();

// Waits up to timeoutTicks (portMAX_DELAY = forever) for the next press.
bool buttonsNext(Button& out, TickType_t timeoutTicks);
