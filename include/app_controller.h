#pragma once
// ============================================================
// app_controller.h – Main application controller
//
// Orchestrates:
//   - State machine transitions
//   - Event processing
//   - Screen rendering
//   - Sensor/storage coordination
//
// Runs as a FreeRTOS task for non-blocking operation.
// ============================================================

#include "config.h"
#include "display_manager.h"
#include "events.h"
#include "input_handler.h"
#include "sensor_manager.h"
#include "state_machine.h"
#include "storage_manager.h"
#include "ui_screens.h"
#include <Arduino.h>

#include <vector>

class AppController {
public:
  static AppController &instance() {
    static AppController inst;
    return inst;
  }

  void init() {
    // Initialize event queue
    EventQueue::init();

    // Initialize subsystems
    auto &disp = DisplayManager::instance();
    disp.init();
    Screens::drawBoot(disp, 0.1f, "Initializing display...");

    Screens::drawBoot(disp, 0.3f, "Initializing sensor...");
    sensorOk_ = SensorManager::instance().init();
    if (!sensorOk_) {
      Screens::drawBoot(disp, 0.3f, "WARNING: Sensor not found!");
      vTaskDelay(pdMS_TO_TICKS(2000));
    }

    Screens::drawBoot(disp, 0.6f, "Mounting SD card...");
    storageOk_ = StorageManager::instance().init();
    if (storageOk_) {
      // Load calibration if available
      CalibrationData cal;
      if (StorageManager::instance().loadCalibration(cal)) {
        SensorManager::instance().setCalibration(cal);
        Screens::drawBoot(disp, 0.8f, "Calibration loaded from SD");
      }
    } else {
      Screens::drawBoot(disp, 0.6f, "WARNING: SD card not found!");
      vTaskDelay(pdMS_TO_TICKS(2000));
    }

    Screens::drawBoot(disp, 0.9f, "Initializing input...");
    InputHandler::instance().init();

    // Setup state machine
    stateMachine_.init([this](AppState oldState, AppState newState) {
      onStateTransition(oldState, newState);
    });

    Screens::drawBoot(disp, 1.0f, "Ready!");
    vTaskDelay(pdMS_TO_TICKS(500));

    // Transition to main menu
    stateMachine_.transitionTo(AppState::MAIN_MENU);
    renderCurrentScreen();
  }

  // ── Main loop (called from FreeRTOS task) ───────────────
  void run() {
    Event evt;

    while (true) {
      // Process events (blocking with timeout for power efficiency)
      if (EventQueue::receive(evt, 50)) {
        processEvent(evt);
      }

      // Periodic screen refresh for animations
      if (needsRefresh_ || (millis() - lastRefresh_ > 100)) {
        renderCurrentScreen();
        needsRefresh_ = false;
        lastRefresh_ = millis();
      }
    }
  }

private:
  AppController() = default;

  // ── Event Processing ────────────────────────────────────
  void processEvent(const Event &evt) {
    AppState state = stateMachine_.current();

    switch (state) {
    case AppState::MAIN_MENU:
      handleMainMenu(evt);
      break;
    case AppState::COLOR_PICKER_MENU:
      handleColorPickerMenu(evt);
      break;
    case AppState::PICK_COLOR:
      handlePickColor(evt);
      break;
    case AppState::PICK_RESULT:
      handlePickResult(evt);
      break;
    case AppState::CALLIPER_MENU:
      handleCalliperMenu(evt);
      break;
    case AppState::MEASURE:
      handleMeasure(evt);
      break;
    case AppState::MEASURE_RESULT:
      handleMeasureResult(evt);
      break;
    case AppState::SAVED_COLORS_LIST:
      handleSavedColorsList(evt);
      break;
    case AppState::SAVED_COLOR_DETAIL:
      handleSavedColorDetail(evt);
      break;
    case AppState::MEASUREMENTS_LIST:
      handleMeasurementsList(evt);
      break;
    case AppState::MEASUREMENT_DETAIL:
      handleMeasurementDetail(evt);
      break;
    case AppState::SETTINGS_MENU:
      handleSettingsMenu(evt);
      break;
    case AppState::CALIB_DARK:
    case AppState::CALIB_GRAY:
    case AppState::CALIB_WHITE:
      handleCalibCapture(evt);
      break;
    case AppState::CALIB_COMPLETE:
      handleCalibComplete(evt);
      break;
    case AppState::ERROR_SCREEN:
      if (evt.type == EventType::BUTTON_PRESS) {
        stateMachine_.transitionTo(AppState::MAIN_MENU);
      }
      break;
    default:
      break;
    }

    needsRefresh_ = true;
  }

  // ── Main Menu Handler ───────────────────────────────────
  // Items: 0 = Color Picker, 1 = Calliper, 2 = Settings
  void handleMainMenu(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      menuIndex_ = (menuIndex_ + 1) % 3;
      break;
    case EventType::ENCODER_CCW:
      menuIndex_ = (menuIndex_ + 2) % 3;
      break;
    case EventType::BUTTON_PRESS:
      switch (menuIndex_) {
      case 0:
        colorPickerMenuIndex_ = 0;
        stateMachine_.transitionTo(AppState::COLOR_PICKER_MENU);
        break;
      case 1:
        calliperMenuIndex_ = 0;
        stateMachine_.transitionTo(AppState::CALLIPER_MENU);
        break;
      case 2:
        settingsMenuIndex_ = 0;
        stateMachine_.transitionTo(AppState::SETTINGS_MENU);
        break;
      }
      break;
    default:
      break;
    }
  }

  // ── Color Picker Sub-Menu Handler ─────────────────────
  // Items: 0 = New Color, 1 = Saved Colors, 2 = Back
  void handleColorPickerMenu(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      colorPickerMenuIndex_ = (colorPickerMenuIndex_ + 1) % 3;
      break;
    case EventType::ENCODER_CCW:
      colorPickerMenuIndex_ = (colorPickerMenuIndex_ + 2) % 3;
      break;
    case EventType::BUTTON_PRESS:
      switch (colorPickerMenuIndex_) {
      case 0:
        stateMachine_.transitionTo(AppState::PICK_COLOR);
        break;
      case 1:
        StorageManager::instance().loadColors(savedColors_);
        colorListIndex_ = 0;
        colorListScroll_ = 0;
        stateMachine_.transitionTo(AppState::SAVED_COLORS_LIST);
        break;
      case 2:
        stateMachine_.goBack();
        break;
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Calliper Sub-Menu Handler ─────────────────────────
  // Items: 0 = New Measure, 1 = Saved Measure, 2 = Back
  void handleCalliperMenu(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      calliperMenuIndex_ = (calliperMenuIndex_ + 1) % 3;
      break;
    case EventType::ENCODER_CCW:
      calliperMenuIndex_ = (calliperMenuIndex_ + 2) % 3;
      break;
    case EventType::BUTTON_PRESS:
      switch (calliperMenuIndex_) {
      case 0:
        measureOffset_ = Config::Measure::INITIAL_OFFSET_PX;
        lastMeasureEncoderTime_ = 0;
        stateMachine_.transitionTo(AppState::MEASURE);
        break;
      case 1:
        StorageManager::instance().loadMeasurements(savedMeasurements_);
        measureListIndex_ = 0;
        measureListScroll_ = 0;
        stateMachine_.transitionTo(AppState::MEASUREMENTS_LIST);
        break;
      case 2:
        stateMachine_.goBack();
        break;
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Pick Color Handler ──────────────────────────────────
  void handlePickColor(const Event &evt) {
    switch (evt.type) {
    case EventType::BUTTON_PRESS:
      if (!measuring_) {
        measuring_ = true;
        needsRefresh_ = true;

        // Perform measurement
        bool ok = SensorManager::instance().measure(currentMeasurement_);
        measuring_ = false;

        if (ok) {
          actionIndex_ = 0;
          stateMachine_.transitionTo(AppState::PICK_RESULT);
        } else {
          Screens::drawError(DisplayManager::instance(), "Sensor Error",
                             "Failed to read AS7343. Check connection.");
        }
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Pick Result Handler ─────────────────────────────────
  void handlePickResult(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      actionIndex_ = (actionIndex_ + 1) % 3;
      break;
    case EventType::ENCODER_CCW:
      actionIndex_ = (actionIndex_ + 2) % 3;
      break;
    case EventType::BUTTON_PRESS:
      switch (actionIndex_) {
      case 0: // Save
        if (StorageManager::instance().saveColor(currentMeasurement_)) {
          stateMachine_.transitionTo(AppState::PICK_COLOR);
        }
        break;
      case 1: // Discard
        stateMachine_.transitionTo(AppState::PICK_COLOR);
        break;
      case 2: // Measure again
        stateMachine_.transitionTo(AppState::PICK_COLOR);
        break;
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.transitionTo(AppState::PICK_COLOR);
      break;
    default:
      break;
    }
  }

  // ── Measure Handler ─────────────────────────────────────
  void handleMeasure(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
    case EventType::ENCODER_CCW: {
      uint32_t now = evt.timestamp;

      // Only apply acceleration for consecutive same-direction events.
      // Direction change resets acceleration to prevent encoder bounce
      // from causing unwanted large steps in the wrong direction.
      bool sameDir = (evt.type == lastMeasureEncoderDir_);
      uint32_t dt = (lastMeasureEncoderTime_ > 0 && sameDir)
                        ? (now - lastMeasureEncoderTime_)
                        : 999;
      lastMeasureEncoderTime_ = now;
      lastMeasureEncoderDir_ = evt.type;

      int step;
      if (dt > Config::Measure::ACCEL_SLOW_MS)
        step = Config::Measure::STEP_SLOW;
      else if (dt > Config::Measure::ACCEL_MED_MS)
        step = Config::Measure::STEP_MED;
      else
        step = Config::Measure::STEP_FAST;

      if (evt.type == EventType::ENCODER_CW) {
        measureOffset_ += step;
        if (measureOffset_ > Config::Measure::MAX_OFFSET_PX)
          measureOffset_ = Config::Measure::MAX_OFFSET_PX;
      } else {
        measureOffset_ -= step;
        if (measureOffset_ < 0)
          measureOffset_ = 0;
      }
    } break;
    case EventType::BUTTON_PRESS: {
      // Confirm measurement
      uint16_t totalPx = measureOffset_ * 2;
      measuredMm_ = totalPx * Config::Measure::PIXEL_PITCH_MM;
      measuredPx_ = totalPx;
      measureActionIndex_ = 0;
      stateMachine_.transitionTo(AppState::MEASURE_RESULT);
    } break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Measure Result Handler ─────────────────────────────
  void handleMeasureResult(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      measureActionIndex_ = (measureActionIndex_ + 1) % 3;
      break;
    case EventType::ENCODER_CCW:
      measureActionIndex_ = (measureActionIndex_ + 2) % 3;
      break;
    case EventType::BUTTON_PRESS:
      switch (measureActionIndex_) {
      case 0: // Save
        StorageManager::instance().saveMeasurement(measuredMm_, measuredPx_);
        measureOffset_ = Config::Measure::INITIAL_OFFSET_PX;
        lastMeasureEncoderTime_ = 0;
        stateMachine_.transitionTo(AppState::MEASURE);
        break;
      case 1: // Discard
        measureOffset_ = Config::Measure::INITIAL_OFFSET_PX;
        lastMeasureEncoderTime_ = 0;
        stateMachine_.transitionTo(AppState::MEASURE);
        break;
      case 2: // Measure again
        measureOffset_ = Config::Measure::INITIAL_OFFSET_PX;
        lastMeasureEncoderTime_ = 0;
        stateMachine_.transitionTo(AppState::MEASURE);
        break;
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      measureOffset_ = Config::Measure::INITIAL_OFFSET_PX;
      lastMeasureEncoderTime_ = 0;
      stateMachine_.transitionTo(AppState::MEASURE);
      break;
    default:
      break;
    }
  }

  // ── Measurements List Handler ──────────────────────────
  void handleMeasurementsList(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      if (!savedMeasurements_.empty()) {
        measureListIndex_ =
            (measureListIndex_ + 1) % savedMeasurements_.size();
        if (measureListIndex_ >= measureListScroll_ + 6)
          measureListScroll_ = measureListIndex_ - 5;
        if (measureListIndex_ < measureListScroll_)
          measureListScroll_ = measureListIndex_;
      }
      break;
    case EventType::ENCODER_CCW:
      if (!savedMeasurements_.empty()) {
        measureListIndex_ = (measureListIndex_ == 0)
                                ? savedMeasurements_.size() - 1
                                : measureListIndex_ - 1;
        if (measureListIndex_ < measureListScroll_)
          measureListScroll_ = measureListIndex_;
        if (measureListIndex_ >= measureListScroll_ + 6)
          measureListScroll_ = measureListIndex_ - 5;
      }
      break;
    case EventType::BUTTON_PRESS:
      if (!savedMeasurements_.empty()) {
        selectedMeasurement_ = savedMeasurements_[measureListIndex_];
        measureDetailActionIndex_ = 0;
        stateMachine_.transitionTo(AppState::MEASUREMENT_DETAIL);
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Measurement Detail Handler ─────────────────────────
  void handleMeasurementDetail(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
    case EventType::ENCODER_CCW:
      measureDetailActionIndex_ = 1 - measureDetailActionIndex_;
      break;
    case EventType::BUTTON_PRESS:
      if (measureDetailActionIndex_ == 0) {
        // Back
        stateMachine_.transitionTo(AppState::MEASUREMENTS_LIST);
      } else {
        // Delete
        StorageManager::instance().deleteMeasurement(
            selectedMeasurement_.index);
        StorageManager::instance().loadMeasurements(savedMeasurements_);
        measureListIndex_ = 0;
        measureListScroll_ = 0;
        stateMachine_.transitionTo(AppState::MEASUREMENTS_LIST);
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.transitionTo(AppState::MEASUREMENTS_LIST);
      break;
    default:
      break;
    }
  }

  // ── Saved Colors List Handler ───────────────────────────
  void handleSavedColorsList(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
      if (!savedColors_.empty()) {
        colorListIndex_ = (colorListIndex_ + 1) % savedColors_.size();
        // Adjust scroll
        if (colorListIndex_ >= colorListScroll_ + 6) {
          colorListScroll_ = colorListIndex_ - 5;
        }
        if (colorListIndex_ < colorListScroll_) {
          colorListScroll_ = colorListIndex_;
        }
      }
      break;
    case EventType::ENCODER_CCW:
      if (!savedColors_.empty()) {
        colorListIndex_ = (colorListIndex_ == 0) ? savedColors_.size() - 1
                                                 : colorListIndex_ - 1;
        if (colorListIndex_ < colorListScroll_) {
          colorListScroll_ = colorListIndex_;
        }
        if (colorListIndex_ >= colorListScroll_ + 6) {
          colorListScroll_ = colorListIndex_ - 5;
        }
      }
      break;
    case EventType::BUTTON_PRESS:
      if (!savedColors_.empty()) {
        selectedColor_ = savedColors_[colorListIndex_];
        detailActionIndex_ = 0;
        stateMachine_.transitionTo(AppState::SAVED_COLOR_DETAIL);
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Saved Color Detail Handler ──────────────────────────
  void handleSavedColorDetail(const Event &evt) {
    switch (evt.type) {
    case EventType::ENCODER_CW:
    case EventType::ENCODER_CCW:
      detailActionIndex_ = 1 - detailActionIndex_; // Toggle 0/1
      break;
    case EventType::BUTTON_PRESS:
      if (detailActionIndex_ == 0) {
        // Back
        stateMachine_.transitionTo(AppState::SAVED_COLORS_LIST);
      } else {
        // Delete
        StorageManager::instance().deleteColor(selectedColor_.index);
        StorageManager::instance().loadColors(savedColors_);
        colorListIndex_ = 0;
        colorListScroll_ = 0;
        stateMachine_.transitionTo(AppState::SAVED_COLORS_LIST);
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.transitionTo(AppState::SAVED_COLORS_LIST);
      break;
    default:
      break;
    }
  }

  // ── Settings Menu Handler ──────────────────────────────
  // Items: 0 = Color Calibration CG-3, 1 = Sensor Gain, 2 = Screen orientation
  void handleSettingsMenu(const Event &evt) {
    auto &sensor = SensorManager::instance();
    switch (evt.type) {
    case EventType::ENCODER_CW:
      settingsMenuIndex_ = (settingsMenuIndex_ + 1) % 3;
      break;
    case EventType::ENCODER_CCW:
      settingsMenuIndex_ = (settingsMenuIndex_ + 2) % 3;
      break;
    case EventType::BUTTON_PRESS:
      switch (settingsMenuIndex_) {
      case 0:
        // Start unified calibration wizard (Dark → Gray → White)
        stateMachine_.transitionTo(AppState::CALIB_DARK);
        break;
      case 1:
        // Cycle sensor gain to next value
        sensor.setGainIndex(sensor.getGainIndex() + 1);
        needsRefresh_ = true;
        break;
      case 2:
        // Cycle screen orientation (0→1→2→3→0)
        {
          auto &disp = DisplayManager::instance();
          screenRotation_ = (screenRotation_ + 1) % 4;
          disp.setRotation(screenRotation_);
        }
        needsRefresh_ = true;
        break;
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      stateMachine_.goBack();
      break;
    default:
      break;
    }
  }

  // ── Calibration Capture Handler ─────────────────────────
  // Unified wizard: CALIB_DARK → CALIB_GRAY → CALIB_WHITE → CALIB_COMPLETE
  void handleCalibCapture(const Event &evt) {
    AppState state = stateMachine_.current();

    switch (evt.type) {
    case EventType::BUTTON_PRESS:
      if (!calibrating_) {
        calibrating_ = true;
        needsRefresh_ = true;

        auto &sensor = SensorManager::instance();
        bool ok = false;

        switch (state) {
        case AppState::CALIB_DARK:
          ok = sensor.captureDarkReference();
          break;
        case AppState::CALIB_GRAY:
          ok = sensor.captureGrayReference();
          break;
        case AppState::CALIB_WHITE:
          ok = sensor.captureWhiteReference();
          break;
        default:
          break;
        }

        calibrating_ = false;

        if (ok) {
          // Save calibration to SD after each step
          StorageManager::instance().saveCalibration(sensor.getCalibration());

          // Advance to next wizard step
          switch (state) {
          case AppState::CALIB_DARK:
            stateMachine_.transitionTo(AppState::CALIB_GRAY);
            break;
          case AppState::CALIB_GRAY:
            stateMachine_.transitionTo(AppState::CALIB_WHITE);
            break;
          case AppState::CALIB_WHITE:
            stateMachine_.transitionTo(AppState::CALIB_COMPLETE);
            break;
          default:
            break;
          }
        } else {
          Screens::drawError(DisplayManager::instance(), "Calibration Error",
                             "Failed to capture reference.");
        }
      }
      break;
    case EventType::BUTTON_LONG_PRESS:
      // Cancel entire wizard
      stateMachine_.transitionTo(AppState::SETTINGS_MENU);
      break;
    default:
      break;
    }
  }

  // ── Calibration Complete Handler ────────────────────────
  void handleCalibComplete(const Event &evt) {
    if (evt.type == EventType::BUTTON_PRESS ||
        evt.type == EventType::BUTTON_LONG_PRESS) {
      stateMachine_.transitionTo(AppState::SETTINGS_MENU);
    }
  }

  // ── State Transition Callback ───────────────────────────
  void onStateTransition(AppState oldState, AppState newState) {
    Serial.printf("[State] %d -> %d\n", (int)oldState, (int)newState);
    needsRefresh_ = true;
  }

  // ── Render Current Screen ───────────────────────────────
  void renderCurrentScreen() {
    auto &disp = DisplayManager::instance();
    auto &sensor = SensorManager::instance();

    switch (stateMachine_.current()) {
    case AppState::MAIN_MENU:
      Screens::drawMainMenu(disp, menuIndex_);
      break;

    case AppState::COLOR_PICKER_MENU:
      Screens::drawColorPickerMenu(disp, colorPickerMenuIndex_);
      break;

    case AppState::CALLIPER_MENU:
      Screens::drawCalliperMenu(disp, calliperMenuIndex_);
      break;

    case AppState::PICK_COLOR:
      Screens::drawPickColor(disp, currentMeasurement_, measuring_);
      break;

    case AppState::PICK_RESULT:
      Screens::drawPickResult(disp, currentMeasurement_, actionIndex_);
      break;

    case AppState::MEASURE:
      Screens::drawMeasure(disp, measureOffset_);
      break;

    case AppState::MEASURE_RESULT:
      Screens::drawMeasureResult(disp, measuredMm_, measuredPx_,
                                 measureActionIndex_);
      break;

    case AppState::SAVED_COLORS_LIST:
      Screens::drawSavedColorsList(disp, savedColors_, colorListIndex_,
                                   colorListScroll_);
      break;

    case AppState::SAVED_COLOR_DETAIL:
      Screens::drawSavedColorDetail(disp, selectedColor_, detailActionIndex_);
      break;

    case AppState::MEASUREMENTS_LIST:
      Screens::drawMeasurementsList(disp, savedMeasurements_, measureListIndex_,
                                    measureListScroll_);
      break;

    case AppState::MEASUREMENT_DETAIL:
      Screens::drawMeasurementDetail(disp, selectedMeasurement_,
                                     measureDetailActionIndex_);
      break;

    case AppState::SETTINGS_MENU:
      Screens::drawSettingsMenu(disp, sensor.getCalibration(),
                                settingsMenuIndex_, sensor.getGainLabel(),
                                screenRotation_);
      break;

    case AppState::CALIB_DARK:
      Screens::drawCalibCapture(disp, "Dark Reference",
                                "Cover sensor completely (no light).",
                                calibrating_, 1, 3);
      break;

    case AppState::CALIB_GRAY:
      Screens::drawCalibCapture(disp, "Gray Card 18%",
                                "Place sensor on GC-3 gray card.",
                                calibrating_, 2, 3);
      break;

    case AppState::CALIB_WHITE:
      Screens::drawCalibCapture(disp, "White Reference",
                                "Place sensor on white reference.",
                                calibrating_, 3, 3);
      break;

    case AppState::CALIB_COMPLETE:
      Screens::drawCalibComplete(disp, sensor.getCalibration());
      break;

    case AppState::ERROR_SCREEN:
      // Already rendered by the error handler
      break;

    default:
      break;
    }
  }

  // ── State ───────────────────────────────────────────────
  StateMachine stateMachine_;

  // Menu state
  int menuIndex_ = 0;
  int actionIndex_ = 0;

  // Measurement state
  SpectralData currentMeasurement_;
  bool measuring_ = false;

  // Saved colors state
  std::vector<SavedColor> savedColors_;
  int colorListIndex_ = 0;
  int colorListScroll_ = 0;
  SavedColor selectedColor_;
  int detailActionIndex_ = 0;

  // Measure state
  int16_t measureOffset_ = 0;         // current offset from center in px
  uint32_t lastMeasureEncoderTime_ = 0; // for acceleration
  EventType lastMeasureEncoderDir_ = EventType::ENCODER_CW; // last direction
  float measuredMm_ = 0.0f;           // last confirmed measurement
  uint16_t measuredPx_ = 0;           // last confirmed px value
  int measureActionIndex_ = 0;        // result screen action

  // Measurements history state
  std::vector<SavedMeasurement> savedMeasurements_;
  int measureListIndex_ = 0;
  int measureListScroll_ = 0;
  SavedMeasurement selectedMeasurement_;
  int measureDetailActionIndex_ = 0;

  // Sub-menu indices
  int colorPickerMenuIndex_ = 0;
  int calliperMenuIndex_ = 0;
  int settingsMenuIndex_ = 0;
  uint8_t screenRotation_ = Config::LCD::ROTATION;

  // Calibration state
  bool calibrating_ = false;

  // System state
  bool sensorOk_ = false;
  bool storageOk_ = false;
  bool needsRefresh_ = true;
  uint32_t lastRefresh_ = 0;
};
