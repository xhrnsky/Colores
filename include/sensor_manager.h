#pragma once
// ============================================================
// sensor_manager.h – AS7343 spectral sensor abstraction
//
// Uses SparkFun AS7343 library for all register-level access.
// This layer adds:
//   - Calibration (dark/gray/white references)
//   - Color space conversion (spectral → XYZ → sRGB)
//   - Thread-safe measurement interface
// ============================================================

#include "config.h"
#include "events.h"
#include <Arduino.h>
#include <SparkFun_AS7343.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// ── Channel Data ────────────────────────────────────────────
// AS7343 provides 14 spectral channels via two SMUX configurations
// Channels: FZ, FY, FXL, NIR, 2xVIS, FD, F1..F8
// We store all raw + calibrated values

struct SpectralData {
  // Raw ADC counts from sensor
  uint16_t raw[Config::Sensor::NUM_CHANNELS];

  // Calibrated (dark-subtracted, gain-normalized) values
  float calibrated[Config::Sensor::NUM_CHANNELS];

  // Derived color values
  float cie_X, cie_Y, cie_Z; // CIE 1931 XYZ
  uint8_t r, g, b;           // sRGB (0-255)
  float L, a_star, b_star;   // CIE Lab (future)

  // Metadata
  uint32_t timestamp;
  bool valid;

  // Convenience
  uint32_t toRGB888() const {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  uint16_t toRGB565() const {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
  }

  void toHexString(char *buf, size_t len) const {
    snprintf(buf, len, "#%02X%02X%02X", r, g, b);
  }
};

// ── Calibration Data ────────────────────────────────────────
struct CalibrationData {
  float darkRef[Config::Sensor::NUM_CHANNELS];  // Dark reference (sensor noise
                                                // floor)
  float grayRef[Config::Sensor::NUM_CHANNELS];  // 18% gray card reference
  float whiteRef[Config::Sensor::NUM_CHANNELS]; // (Optional) white reference
  bool hasDark;
  bool hasGray;
  bool hasWhite;
  uint32_t calibTimestamp;

  // Gray card reflectance factor (18% = 0.18)
  static constexpr float GRAY_REFLECTANCE = 0.18f;
};

// ── Sensor Manager ──────────────────────────────────────────
class SensorManager {
public:
  static SensorManager &instance() {
    static SensorManager inst;
    return inst;
  }

  bool init() {
    Wire.begin(Config::Sensor::SDA, Config::Sensor::SCL,
               Config::Sensor::I2C_FREQ);

    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_)
      return false;

    // Initialize sensor using SparkFun library
    if (!sensor_.begin(Config::Sensor::I2C_ADDR, Wire)) {
      Serial.println("[Sensor] AS7343 not found at 0x39");
      return false;
    }

    sensor_.powerOn();

    // Set AutoSmux to 18 channels (to read all channels)
    sensor_.setAutoSmux(AUTOSMUX_18_CHANNELS);

    // Enable Spectral Measurement (Required to start valid readings)
    sensor_.enableSpectralMeasurement();

    // Configure gain (AGAIN_16 = 16x)
    sensor_.setAgain(AGAIN_16);

    // Configure LED drive current but keep it OFF by default.
    // LED is turned on/off only during active measurements.
    sensor_.setLedDrive(0); // 4 mA (Minimum)
    sensor_.ledOff();

    initialized_ = true;
    Serial.println("[Sensor] AS7343 initialized (LED off)");
    return true;
  }

  // ── Single measurement ──────────────────────────────────
  // withLed: when true the on-board LED is turned on for the
  //          duration of the sensor read and turned off afterwards.
  //          Pass false for dark-reference capture (no illumination).
  bool measure(SpectralData &data, bool withLed = true) {
    if (!initialized_)
      return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);

    if (withLed) {
      sensor_.ledOn();
      // Wait for at least one full integration cycle with LED on.
      // Integration = (ATIME+1)*(ASTEP+1)*2.78µs ≈ 50 ms per SMUX config,
      // ×2 for AUTOSMUX_18_CHANNELS ≈ 100 ms. Add margin for settling.
      vTaskDelay(pdMS_TO_TICKS(150));
      // Flush stale data captured before LED was on
      sensor_.readSpectraDataFromSensor();
    }

    // Read fresh measurement (captured with LED illumination if withLed)
    bool ok = sensor_.readSpectraDataFromSensor();

    if (withLed)
      sensor_.ledOff();

    if (ok) {
      // Read all 14 channels
      // AS7343 channel mapping (SparkFun library order):
      // Index 0:  F1  (405-425 nm, violet)
      // Index 1:  F2  (435-455 nm, blue)
      // Index 2:  FZ  (CIE Z approximation)
      // Index 3:  F3  (470-490 nm, cyan-blue)
      // Index 4:  F4  (505-525 nm, green)
      // Index 5:  FY  (CIE Y approximation)
      // Index 6:  F5  (545-565 nm, yellow-green)
      // Index 7:  FXL (CIE X low approximation)
      // Index 8:  F6  (580-600 nm, orange)
      // Index 9:  F7  (620-640 nm, red)
      // Index 10: F8  (670-690 nm, deep red)
      // Index 11: NIR (near infrared)
      // Index 12: Clear/VIS
      // Index 13: FD  (flicker detect)

      data.raw[0] = sensor_.getChannelData(CH_PURPLE_F1_405NM);
      data.raw[1] = sensor_.getChannelData(CH_DARK_BLUE_F2_425NM);
      data.raw[2] = sensor_.getChannelData(CH_BLUE_FZ_450NM);
      data.raw[3] = sensor_.getChannelData(CH_LIGHT_BLUE_F3_475NM);
      data.raw[4] = sensor_.getChannelData(CH_BLUE_F4_515NM);
      data.raw[5] = sensor_.getChannelData(CH_GREEN_FY_555NM);
      data.raw[6] = sensor_.getChannelData(CH_GREEN_F5_550NM);
      data.raw[7] = sensor_.getChannelData(CH_ORANGE_FXL_600NM);
      data.raw[8] = sensor_.getChannelData(CH_BROWN_F6_640NM);
      data.raw[9] = sensor_.getChannelData(CH_RED_F7_690NM);
      data.raw[10] = sensor_.getChannelData(CH_DARK_RED_F8_745NM);
      data.raw[11] = sensor_.getChannelData(CH_NIR_855NM);
      data.raw[12] = sensor_.getChannelData(
          CH_VIS_1); // Using VIS_1 as Clear approximation
      data.raw[13] = sensor_.getChannelData(CH_FD_1);

      // Apply calibration
      applyCalibration(data);

      // Convert to color
      spectralToXYZ(data);
      xyzToSRGB(data);

      data.timestamp = millis();
      data.valid = true;
    } else {
      data.valid = false;
    }

    xSemaphoreGive(mutex_);
    return ok;
  }

  // ── Calibration routines ────────────────────────────────

  // Step 1: Dark reference – sensor covered, no light
  bool captureDarkReference() {
    SpectralData temp;
    constexpr int AVG_COUNT = 10;

    // Average multiple readings for stability
    float accum[Config::Sensor::NUM_CHANNELS] = {0};

    for (int i = 0; i < AVG_COUNT; i++) {
      if (!measure(temp, false)) // LED off – measuring dark noise floor
        return false;
      for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
        accum[ch] += temp.raw[ch];
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
      calib_.darkRef[ch] = accum[ch] / AVG_COUNT;
    }
    calib_.hasDark = true;
    Serial.println("[Sensor] Dark reference captured");
    return true;
  }

  // Step 2: Gray card reference (18% neutral gray, GC-3)
  // This establishes the relationship between sensor counts
  // and known reflectance, enabling absolute color measurement.
  bool captureGrayReference() {
    if (!calib_.hasDark) {
      Serial.println("[Sensor] ERROR: Dark reference required first");
      return false;
    }

    SpectralData temp;
    constexpr int AVG_COUNT = 10;
    float accum[Config::Sensor::NUM_CHANNELS] = {0};

    for (int i = 0; i < AVG_COUNT; i++) {
      if (!measure(temp))
        return false;
      for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
        accum[ch] += temp.raw[ch];
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
      calib_.grayRef[ch] = accum[ch] / AVG_COUNT;
    }
    calib_.hasGray = true;
    calib_.calibTimestamp = millis();
    Serial.println("[Sensor] Gray reference captured");
    return true;
  }

  // Optional Step 3: White reference (e.g., barium sulfate plate)
  bool captureWhiteReference() {
    SpectralData temp;
    constexpr int AVG_COUNT = 10;
    float accum[Config::Sensor::NUM_CHANNELS] = {0};

    for (int i = 0; i < AVG_COUNT; i++) {
      if (!measure(temp))
        return false;
      for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
        accum[ch] += temp.raw[ch];
      }
      vTaskDelay(pdMS_TO_TICKS(50));
    }

    for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
      calib_.whiteRef[ch] = accum[ch] / AVG_COUNT;
    }
    calib_.hasWhite = true;
    Serial.println("[Sensor] White reference captured");
    return true;
  }

  const CalibrationData &getCalibration() const { return calib_; }
  void setCalibration(const CalibrationData &cal) { calib_ = cal; }
  bool isInitialized() const { return initialized_; }

  // ── Gain control ──────────────────────────────────────────
  static constexpr int GAIN_COUNT = 13;
  static constexpr sfe_as7343_again_t kGainTable[GAIN_COUNT] = {
      AGAIN_0_5, AGAIN_1,   AGAIN_2,   AGAIN_4,  AGAIN_8,   AGAIN_16, AGAIN_32,
      AGAIN_64,  AGAIN_128, AGAIN_256, AGAIN_512, AGAIN_1024, AGAIN_2048};
  static constexpr const char *kGainLabels[GAIN_COUNT] = {
      "0.5x", "1x",   "2x",   "4x",   "8x",    "16x",  "32x",
      "64x",  "128x", "256x", "512x", "1024x", "2048x"};

  int getGainIndex() const { return gainIndex_; }
  const char *getGainLabel() const { return kGainLabels[gainIndex_]; }

  void setGainIndex(int idx) {
    gainIndex_ = idx % GAIN_COUNT;
    if (gainIndex_ < 0)
      gainIndex_ += GAIN_COUNT;
    sensor_.setAgain(kGainTable[gainIndex_]);
    Serial.printf("[Sensor] Gain set to %s\n", kGainLabels[gainIndex_]);
  }

private:
  SensorManager() : initialized_(false), mutex_(nullptr), gainIndex_(5) {
    memset(&calib_, 0, sizeof(calib_));
  }

  // ── Apply calibration to raw data ───────────────────────
  void applyCalibration(SpectralData &data) {
    for (int ch = 0; ch < Config::Sensor::NUM_CHANNELS; ch++) {
      float val = static_cast<float>(data.raw[ch]);

      // Subtract dark reference
      if (calib_.hasDark) {
        val -= calib_.darkRef[ch];
        if (val < 0)
          val = 0;
      }

      // Normalize to gray reference (reflectance-relative)
      if (calib_.hasGray) {
        float grayNet = calib_.grayRef[ch] - calib_.darkRef[ch];
        if (grayNet > 0) {
          // Scale so that gray card = 0.18 reflectance
          val = (val / grayNet) * CalibrationData::GRAY_REFLECTANCE;
        }
      }

      data.calibrated[ch] = val;
    }
  }

  // ── Spectral → CIE XYZ conversion ──────────────────────
  // Using AS7343's CIE-like channels (FZ, FY, FXL) as approximations
  // HYPOTHESIS: Direct use of FZ/FY/FXL as Z/Y/X is a first-order
  // approximation. For higher accuracy, a full spectral integration
  // with CIE 1931 observer functions applied to F1-F8 would be needed.
  void spectralToXYZ(SpectralData &data) {
    // Use the CIE-approximation channels
    // FXL ≈ X, FY ≈ Y, FZ ≈ Z
    data.cie_X = data.calibrated[7]; // FXL
    data.cie_Y = data.calibrated[5]; // FY
    data.cie_Z = data.calibrated[2]; // FZ

    // Normalize (D65 white point)
    // If calibrated with gray card, values are already relative
    if (!calib_.hasGray) {
      // Without calibration, normalize to max for visualization
      float maxVal = fmax(fmax(data.cie_X, data.cie_Y), data.cie_Z);
      if (maxVal > 0) {
        data.cie_X /= maxVal;
        data.cie_Y /= maxVal;
        data.cie_Z /= maxVal;
      }
    }
  }

  // ── CIE XYZ → sRGB conversion ──────────────────────────
  void xyzToSRGB(SpectralData &data) {
    // XYZ to linear sRGB matrix (D65)
    float r_lin =
        data.cie_X * 3.2406f + data.cie_Y * -1.5372f + data.cie_Z * -0.4986f;
    float g_lin =
        data.cie_X * -0.9689f + data.cie_Y * 1.8758f + data.cie_Z * 0.0415f;
    float b_lin =
        data.cie_X * 0.0557f + data.cie_Y * -0.2040f + data.cie_Z * 1.0570f;

    // Clamp to [0, 1]
    r_lin = fmax(0.0f, fmin(1.0f, r_lin));
    g_lin = fmax(0.0f, fmin(1.0f, g_lin));
    b_lin = fmax(0.0f, fmin(1.0f, b_lin));

    // sRGB gamma correction
    auto gammaCorrect = [](float c) -> float {
      if (c <= 0.0031308f)
        return 12.92f * c;
      else
        return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
    };

    data.r = static_cast<uint8_t>(gammaCorrect(r_lin) * 255.0f + 0.5f);
    data.g = static_cast<uint8_t>(gammaCorrect(g_lin) * 255.0f + 0.5f);
    data.b = static_cast<uint8_t>(gammaCorrect(b_lin) * 255.0f + 0.5f);
  }

  SfeAS7343ArdI2C sensor_;
  CalibrationData calib_;
  SemaphoreHandle_t mutex_;
  bool initialized_;
  int gainIndex_; // Index into kGainTable (default 5 = AGAIN_16)
};
