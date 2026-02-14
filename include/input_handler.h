#pragma once
// ============================================================
// input_handler.h – Rotary encoder + button input
//
// Encoder: polled state-machine decoder.
// This encoder uses 3 states (00, 10, 11 — never 01).
// Direction is determined from the transition order:
//   CW (right): 10→00→11→10   (3 transitions per detent)
//   CCW (left): 10→11→00→10   (3 transitions per detent)
//
// Button: polled with debounce + long-press detection.
// ============================================================

#include "config.h"
#include "events.h"
#include <Arduino.h>

class InputHandler {
public:
  static InputHandler &instance() {
    static InputHandler inst;
    return inst;
  }

  void init() {
    pinMode(Config::Encoder::BTN_PIN, INPUT_PULLUP);
    pinMode(Config::Encoder::CW_PIN, INPUT_PULLUP);
    pinMode(Config::Encoder::CCW_PIN, INPUT_PULLUP);

    lastEncoderState_ =
        (digitalRead(Config::Encoder::CW_PIN) << 1) |
        digitalRead(Config::Encoder::CCW_PIN);
  }

  // Called from a high-frequency FreeRTOS task (~2 ms)
  void update() {
    uint32_t now = millis();

    // ── Encoder state machine ────────────────────────────
    uint8_t state =
        (digitalRead(Config::Encoder::CW_PIN) << 1) |
        digitalRead(Config::Encoder::CCW_PIN);

    if (state != lastEncoderState_) {
      // Transition table derived from measured encoder output:
      //   CW  (right): 10→00 (+1), 00→11 (+1), 11→10 (+1)
      //   CCW (left):  00→10 (-1), 10→11 (-1), 11→00 (-1)
      //
      // Index = (old_state << 2) | new_state
      static const int8_t DIR_TABLE[16] = {
       // to:  00   01   10   11
       /* 00 */ 0,   0,  -1,  +1,
       /* 01 */ 0,   0,   0,   0,
       /* 10 */+1,   0,   0,  -1,
       /* 11 */-1,   0,  +1,   0,
      };

      int8_t dir = DIR_TABLE[(lastEncoderState_ << 2) | state];
      lastEncoderState_ = state;

      if (dir != 0) {
        encoderAccum_ += dir;

        // 3 transitions per detent → emit one event per full detent
        if (encoderAccum_ >= 2) {
          encoderAccum_ = 0;
          EventQueue::send(EventType::ENCODER_CW);
        } else if (encoderAccum_ <= -2) {
          encoderAccum_ = 0;
          EventQueue::send(EventType::ENCODER_CCW);
        }
      }
    }

    // ── Button handling ──────────────────────────────────
    bool rawState =
        (digitalRead(Config::Encoder::BTN_PIN) == LOW); // active low

    if (rawState != lastRawState_) {
      lastStateChangeTime_ = now;
      lastRawState_ = rawState;
    }

    if ((now - lastStateChangeTime_) >= Config::Encoder::DEBOUNCE_MS) {
      if (rawState != buttonPressed_) {
        buttonPressed_ = rawState;

        if (buttonPressed_) {
          buttonPressTime_ = now;
          longPressFired_ = false;
        } else {
          if (!longPressFired_) {
            EventQueue::send(EventType::BUTTON_PRESS);
          }
          EventQueue::send(EventType::BUTTON_RELEASE);
        }
      }
    }

    if (buttonPressed_ && !longPressFired_) {
      if ((now - buttonPressTime_) >= Config::Encoder::LONG_PRESS_MS) {
        longPressFired_ = true;
        EventQueue::send(EventType::BUTTON_LONG_PRESS);
      }
    }
  }

private:
  InputHandler() = default;

  // Encoder
  static uint8_t lastEncoderState_;
  static int8_t encoderAccum_;

  // Button
  static uint32_t buttonPressTime_;
  static bool buttonPressed_;
  static bool longPressFired_;
  static bool lastRawState_;
  static uint32_t lastStateChangeTime_;
};
