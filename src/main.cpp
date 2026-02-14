// ============================================================
// main.cpp – ESP32-C6 Color Picker firmware entry point
//
// Architecture: Arduino framework on ESP32-C6 (RISC-V)
// Uses FreeRTOS tasks for:
//   - UI rendering & event processing (main task)
//   - Input polling (high-priority task for long-press)
//
// The encoder rotation is interrupt-driven (ISR).
// Button press uses ISR + periodic polling for long-press.
// All communication between components uses the EventQueue.
// ============================================================

#include "app_controller.h"
#include "config.h"
#include <Arduino.h>

// ── FreeRTOS Task: Application Main Loop ────────────────────
void taskApp(void *param) {
  (void)param;
  AppController::instance().run(); // Never returns
}

// ── FreeRTOS Task: Input Polling ────────────────────────────
// Polls encoder pins + button at ~500 Hz for reliable edge detection.
void taskInput(void *param) {
  (void)param;
  while (true) {
    InputHandler::instance().update();
    vTaskDelay(pdMS_TO_TICKS(2)); // 500 Hz – fast enough for encoder edges
  }
}

// ── Arduino Setup ───────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(100); // Brief delay for serial monitor connection

  Serial.println("================================");
  Serial.println("ESP32-C6 Color Picker v1.0.0");
  Serial.println("================================");
  Serial.printf("CPU Freq: %d MHz\n", getCpuFrequencyMhz());
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());

  // Initialize application controller
  // (initializes display, sensor, storage, input)
  AppController::instance().init();

  // Create FreeRTOS tasks
  // NOTE: ESP32-C6 is single-core RISC-V, so both tasks
  // run on core 0 with preemptive scheduling.
  xTaskCreatePinnedToCore(taskApp, "app", Config::System::TASK_STACK_UI,
                          nullptr, Config::System::TASK_PRIORITY_UI, nullptr,
                          Config::System::CORE_UI);

  xTaskCreatePinnedToCore(taskInput, "input", Config::System::TASK_STACK_INPUT,
                          nullptr, Config::System::TASK_PRIORITY_INPUT, nullptr,
                          Config::System::CORE_OTHER);

  Serial.println("[Main] Tasks created, scheduler running");
  Serial.printf("[Main] Free Heap after init: %d bytes\n", ESP.getFreeHeap());
}

// ── Arduino Loop ────────────────────────────────────────────
// Not used – all work done in FreeRTOS tasks
void loop() { vTaskDelay(portMAX_DELAY); }
