#pragma once
// ============================================================
// events.h – Event types and thread-safe event queue
// ============================================================

#include <Arduino.h>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ── Event Types ─────────────────────────────────────────────
enum class EventType : uint8_t {
  // Input events
  ENCODER_CW,        // Rotary encoder clockwise
  ENCODER_CCW,       // Rotary encoder counter-clockwise
  BUTTON_PRESS,      // Short press
  BUTTON_LONG_PRESS, // Long press (>800ms)
  BUTTON_RELEASE,

  // Sensor events
  SENSOR_DATA_READY, // New measurement available
  SENSOR_ERROR,      // Sensor communication error

  // System events
  SD_MOUNTED,
  SD_ERROR,
  CALIBRATION_COMPLETE,
  COLOR_SAVED,
  COLOR_DELETED,
  SAVE_ERROR,

  // UI events (internal)
  SCREEN_REFRESH,
  NAVIGATE_BACK,
};

// ── Event Data ──────────────────────────────────────────────
struct Event {
  EventType type;
  int32_t data;       // Optional payload (index, error code, etc.)
  uint32_t timestamp; // millis() at event creation
};

// ── Event Queue (singleton-style, FreeRTOS queue) ───────────
class EventQueue {
public:
  static constexpr int QUEUE_SIZE = 32;

  static bool init() {
    if (handle_ == nullptr) {
      handle_ = xQueueCreate(QUEUE_SIZE, sizeof(Event));
    }
    return handle_ != nullptr;
  }

  static bool send(EventType type, int32_t data = 0) {
    if (handle_ == nullptr)
      return false;
    Event evt;
    evt.type = type;
    evt.data = data;
    evt.timestamp = millis();
    return xQueueSend(handle_, &evt, 0) == pdTRUE;
  }

  static bool sendFromISR(EventType type, int32_t data = 0) {
    if (handle_ == nullptr)
      return false;
    Event evt;
    evt.type = type;
    evt.data = data;
    evt.timestamp = millis();
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    bool ok =
        xQueueSendFromISR(handle_, &evt, &higherPriorityTaskWoken) == pdTRUE;
    if (higherPriorityTaskWoken) {
      portYIELD_FROM_ISR();
    }
    return ok;
  }

  // Blocking receive with timeout
  static bool receive(Event &evt, uint32_t timeoutMs = portMAX_DELAY) {
    if (handle_ == nullptr)
      return false;
    return xQueueReceive(handle_, &evt, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
  }

  // Non-blocking peek
  static bool peek(Event &evt) {
    if (handle_ == nullptr)
      return false;
    return xQueuePeek(handle_, &evt, 0) == pdTRUE;
  }

  static int pending() {
    if (handle_ == nullptr)
      return 0;
    return uxQueueMessagesWaiting(handle_);
  }

private:
  static inline QueueHandle_t handle_ = nullptr;
};
