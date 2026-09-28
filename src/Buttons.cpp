#include "Buttons.h"

namespace {

// Indexed by Button. Active-low. HOME=2 is the MENU key, PRV/NEXT/OK are the
// rotary switch's up/down/press contacts.
constexpr uint8_t PINS[BUTTON_COUNT] = {2, 6, 5, 4, 1};
constexpr const char* NAMES[BUTTON_COUNT] = {"MENU", "UP", "OK", "DOWN", "EXIT"};

constexpr TickType_t POLL_TICKS = pdMS_TO_TICKS(5);
constexpr uint8_t STABLE_SAMPLES = 4;  // 20 ms of agreement before a change counts
constexpr int QUEUE_LEN = 16;

QueueHandle_t queue = nullptr;

void pollTask(void*) {
  bool pressed[BUTTON_COUNT] = {};
  uint8_t streak[BUTTON_COUNT] = {};
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
      bool raw = digitalRead(PINS[i]) == LOW;
      if (raw == pressed[i]) {
        streak[i] = 0;
        continue;
      }
      if (++streak[i] < STABLE_SAMPLES) continue;
      pressed[i] = raw;
      streak[i] = 0;
      if (raw) {
        Button b = static_cast<Button>(i);
        xQueueSend(queue, &b, 0);
      }
    }
    vTaskDelayUntil(&wake, POLL_TICKS);
  }
}

}  // namespace

const char* buttonName(Button b) { return NAMES[static_cast<int>(b)]; }

bool buttonsBegin() {
  for (uint8_t pin : PINS) pinMode(pin, INPUT_PULLUP);
  queue = xQueueCreate(QUEUE_LEN, sizeof(Button));
  if (!queue) return false;
  // Core 0: the main loop (core 1) blocks for seconds inside EPD busy-waits.
  return xTaskCreatePinnedToCore(pollTask, "buttons", 2048, nullptr, 2, nullptr, 0) == pdPASS;
}

bool buttonsNext(Button& out, TickType_t timeoutTicks) {
  return queue && xQueueReceive(queue, &out, timeoutTicks) == pdTRUE;
}
