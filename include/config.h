#pragma once
// ============================================================
// config.h – Central hardware & firmware configuration
// ESP32-C6 Color Picker Device
// ============================================================

#include <Arduino.h>
#include <cstdint>

namespace Config {

// ── Display (ST7789 172×320) ────────────────────────────────
namespace LCD {
constexpr int MOSI = 6;
constexpr int SCLK = 7;
constexpr int CS = 14;
constexpr int DC = 15;
constexpr int RST = 21;
constexpr int BL = 22;

constexpr int WIDTH = 320;  // landscape
constexpr int HEIGHT = 172; // landscape
constexpr int ROTATION = 1; // landscape orientation

constexpr uint32_t SPI_FREQ = 40000000; // 40 MHz SPI clock
constexpr uint8_t BL_PWM_CHANNEL = 0;
constexpr uint32_t BL_PWM_FREQ = 5000;
constexpr uint8_t BL_PWM_RESOLUTION = 8;
constexpr uint8_t BL_DEFAULT = 200; // 0–255
} // namespace LCD

// ── microSD (SPI, shared bus) ───────────────────────────────
namespace SD {
constexpr int MISO = 5;
constexpr int MOSI = 6; // shared with LCD
constexpr int SCLK = 7; // shared with LCD
constexpr int CS = 4;
} // namespace SD

// ── AS7343 Spectral Sensor (I²C) ───────────────────────────
namespace Sensor {
constexpr int SDA = 18;
constexpr int SCL = 19;
constexpr int INT_PIN = 20;
constexpr uint8_t I2C_ADDR = 0x39;

constexpr uint32_t I2C_FREQ = 400000; // 400 kHz Fast Mode

// AS7343 has 14 channels across multiple SMUX configurations
constexpr int NUM_CHANNELS = 14;

// Integration time defaults (adjustable via calibration)
constexpr uint8_t DEFAULT_ATIME = 29;   // (ATIME+1)*(ASTEP+1) = integration
constexpr uint16_t DEFAULT_ASTEP = 599; // ~18ms integration time
constexpr uint8_t DEFAULT_GAIN = 5;     // 16x gain (AS7343 gain index)
} // namespace Sensor

// ── Rotary Encoder ──────────────────────────────────────────
namespace Encoder {
constexpr int BTN_PIN = 2; // select / push button
constexpr int CCW_PIN = 3; // rotation counter-clockwise
constexpr int CW_PIN = 1;  // rotation clockwise

constexpr uint32_t DEBOUNCE_MS = 10;        // button debounce
constexpr uint32_t ENCODER_DEBOUNCE_MS = 5; // rotation debounce

// Long press threshold
constexpr uint32_t LONG_PRESS_MS = 800;
} // namespace Encoder

// ── Storage ─────────────────────────────────────────────────
namespace Storage {
// CSV chosen over JSON for storage:
// Justification:
//   1. Lower memory footprint per record (no keys repeated)
//   2. Easy append-only writes (no need to re-parse entire file)
//   3. Human readable & trivially importable to spreadsheets
//   4. ArduinoJson still used for calibration data (structured)
constexpr const char *COLORS_FILE = "/colors.csv";
constexpr const char *CALIB_FILE = "/calibration.json";
constexpr int MAX_SAVED_COLORS = 500;
} // namespace Storage

// ── UI ──────────────────────────────────────────────────────
namespace UI {
constexpr uint32_t MENU_ANIMATION_MS = 100;
constexpr int FONT_SIZE_TITLE = 2;
constexpr int FONT_SIZE_BODY = 1;
constexpr int MENU_ITEM_HEIGHT = 28;
constexpr int HEADER_HEIGHT = 30;
constexpr int PADDING = 8;

// Color theme (RGB565)
constexpr uint16_t COLOR_BG = 0x0000;        // black
constexpr uint16_t COLOR_FG = 0xFFFF;        // white
constexpr uint16_t COLOR_ACCENT = 0x07FF;    // cyan
constexpr uint16_t COLOR_SELECTED = 0x001F;  // blue
constexpr uint16_t COLOR_HEADER_BG = 0x18E3; // dark gray
constexpr uint16_t COLOR_WARNING = 0xFBE0;   // orange
constexpr uint16_t COLOR_SUCCESS = 0x07E0;   // green
constexpr uint16_t COLOR_ERROR = 0xF800;     // red
} // namespace UI

// ── Measure (digital caliper) ───────────────────────────────
namespace Measure {
constexpr float PIXEL_PITCH_MM = 0.10109f;   // 32.35mm / 320px
constexpr int16_t MAX_OFFSET_PX = 155;       // max from center to edge
constexpr int16_t INITIAL_OFFSET_PX = 30;    // starting offset (~6mm visible)
constexpr float MAX_RANGE_MM = MAX_OFFSET_PX * 2 * PIXEL_PITCH_MM;

// Encoder acceleration thresholds
constexpr uint32_t ACCEL_SLOW_MS = 150;  // > 150ms between clicks = 1px
constexpr uint32_t ACCEL_MED_MS = 80;    // 80-150ms = 3px
constexpr uint8_t STEP_SLOW = 1;         // ~0.1mm
constexpr uint8_t STEP_MED = 3;          // ~0.3mm
constexpr uint8_t STEP_FAST = 8;         // ~0.8mm

constexpr const char *DATA_FILE = "/measurements.csv";
constexpr int MAX_SAVED_MEASUREMENTS = 500;
} // namespace Measure

// ── System ──────────────────────────────────────────────────
namespace System {
constexpr uint32_t TASK_STACK_UI = 8192;
constexpr uint32_t TASK_STACK_SENSOR = 4096;
constexpr uint32_t TASK_STACK_INPUT = 4096;
constexpr int TASK_PRIORITY_UI = 2;
constexpr int TASK_PRIORITY_SENSOR = 3;
constexpr int TASK_PRIORITY_INPUT = 4;
constexpr int CORE_UI = 0;
constexpr int CORE_OTHER = 0; // ESP32-C6 is single-core RISC-V
} // namespace System

} // namespace Config