#pragma once
// ============================================================
// state_machine.h – Hierarchical State Machine for UI
// ============================================================

#include <cstdint>
#include <functional>

// ── Application States ──────────────────────────────────────
enum class AppState : uint8_t {
  BOOT,               // Splash screen / initialization
  MAIN_MENU,          // Top-level menu (3 items)

  // Color Picker sub-menu
  COLOR_PICKER_MENU,  // Sub-menu: New Color / Saved Color
  PICK_COLOR,         // Live measurement screen
  PICK_RESULT,        // After measurement – save/discard
  SAVED_COLORS_LIST,  // List of saved colors
  SAVED_COLOR_DETAIL, // Detail view of a single saved color

  // Calliper sub-menu
  CALLIPER_MENU,      // Sub-menu: New Measure / Saved Measure
  MEASURE,            // Active ruler / caliper screen
  MEASURE_RESULT,     // Measurement result – save/discard
  MEASUREMENTS_LIST,  // Saved measurements list
  MEASUREMENT_DETAIL, // Detail view of a single measurement

  // Settings sub-menu
  SETTINGS_MENU,      // Sub-menu: Calibration / Gain / Orientation
  CALIB_DARK,         // Dark reference capture
  CALIB_GRAY,         // Gray card reference capture
  CALIB_WHITE,        // (Optional) White reference capture
  CALIB_COMPLETE,     // Calibration results
  ERROR_SCREEN,       // Generic error display
};

// ── State Transition ────────────────────────────────────────
struct StateTransition {
  AppState from;
  AppState to;
};

// ── State Machine ───────────────────────────────────────────
class StateMachine {
public:
  using OnTransition =
      std::function<void(AppState oldState, AppState newState)>;

  StateMachine() : current_(AppState::BOOT), previous_(AppState::BOOT) {}

  void init(OnTransition callback) { onTransition_ = callback; }

  AppState current() const { return current_; }
  AppState previous() const { return previous_; }

  bool transitionTo(AppState newState) {
    if (newState == current_)
      return false;

    // Validate transition
    if (!isValidTransition(current_, newState)) {
      return false;
    }

    previous_ = current_;
    current_ = newState;

    if (onTransition_) {
      onTransition_(previous_, current_);
    }
    return true;
  }

  // Go back to previous state (simple back navigation)
  bool goBack() { return transitionTo(getParentState(current_)); }

  // Get the logical parent state for back navigation
  static AppState getParentState(AppState state) {
    switch (state) {
    // Sub-menus → Main Menu
    case AppState::COLOR_PICKER_MENU:
    case AppState::CALLIPER_MENU:
    case AppState::SETTINGS_MENU:
      return AppState::MAIN_MENU;

    // Color Picker children → Color Picker Menu
    case AppState::PICK_COLOR:
    case AppState::SAVED_COLORS_LIST:
      return AppState::COLOR_PICKER_MENU;

    case AppState::PICK_RESULT:
      return AppState::PICK_COLOR;

    case AppState::SAVED_COLOR_DETAIL:
      return AppState::SAVED_COLORS_LIST;

    // Calliper children → Calliper Menu
    case AppState::MEASURE:
    case AppState::MEASUREMENTS_LIST:
      return AppState::CALLIPER_MENU;

    case AppState::MEASURE_RESULT:
      return AppState::MEASURE;

    case AppState::MEASUREMENT_DETAIL:
      return AppState::MEASUREMENTS_LIST;

    // Settings children → Settings Menu
    case AppState::CALIB_DARK:
    case AppState::CALIB_GRAY:
    case AppState::CALIB_WHITE:
    case AppState::CALIB_COMPLETE:
      return AppState::SETTINGS_MENU;

    case AppState::ERROR_SCREEN:
      return AppState::MAIN_MENU;

    default:
      return AppState::MAIN_MENU;
    }
  }

private:
  bool isValidTransition(AppState from, AppState to) {
    // BOOT can go to MAIN_MENU or ERROR_SCREEN
    if (from == AppState::BOOT) {
      return to == AppState::MAIN_MENU || to == AppState::ERROR_SCREEN;
    }
    // ERROR_SCREEN can go back to MAIN_MENU
    if (to == AppState::ERROR_SCREEN)
      return true;
    // Any other transition is allowed in this simplified model
    // (a production system would have an explicit transition table)
    return true;
  }

  AppState current_;
  AppState previous_;
  OnTransition onTransition_;
};
