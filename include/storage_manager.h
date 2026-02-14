#pragma once
// ============================================================
// storage_manager.h – microSD storage abstraction
//
// Data format decisions:
//   Colors → CSV: Append-only, low memory, spreadsheet-compatible
//   Calibration → JSON: Structured, infrequently written, ArduinoJson
//
// CSV format:
//   timestamp,r,g,b,hex,F1,F2,FZ,F3,F4,FY,F5,FXL,F6,F7,F8,NIR
// ============================================================

#include "config.h"
#include "sensor_manager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <vector>

// ── Saved Color Entry ───────────────────────────────────────
struct SavedColor {
  uint32_t timestamp; // epoch or millis
  uint8_t r, g, b;
  char hex[8]; // "#RRGGBB\0"
  uint16_t raw[Config::Sensor::NUM_CHANNELS];
  float calibrated[Config::Sensor::NUM_CHANNELS];
  int index; // position in file (for deletion)
};

// ── Saved Measurement Entry ─────────────────────────────────
struct SavedMeasurement {
  float value_mm;
  uint16_t value_px;
  uint32_t timestamp;
  int index; // position in file (for deletion)
};

// ── Storage Manager ─────────────────────────────────────────
class StorageManager {
public:
  static StorageManager &instance() {
    static StorageManager inst;
    return inst;
  }

  bool init() {
    // Configure SPI for SD card
    // IMPORTANT: SPI bus is shared with LCD
    // LovyanGFX handles bus arbitration via bus_shared=true
    SPI.begin(Config::SD::SCLK, Config::SD::MISO, Config::SD::MOSI,
              Config::SD::CS);

    if (!SD.begin(Config::SD::CS, SPI, 4000000)) {
      Serial.println("[Storage] SD card mount failed");
      return false;
    }

    // Verify card is readable
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[Storage] SD card mounted, size: %llu MB\n", cardSize);

    // Create colors file with header if it doesn't exist
    if (!SD.exists(Config::Storage::COLORS_FILE)) {
      File f = SD.open(Config::Storage::COLORS_FILE, FILE_WRITE);
      if (f) {
        f.println("timestamp,r,g,b,hex,F1,F2,FZ,F3,F4,FY,F5,FXL,F6,F7,F8,NIR,"
                  "Clear,FD");
        f.close();
      }
    }

    // Create measurements file with header if it doesn't exist
    if (!SD.exists(Config::Measure::DATA_FILE)) {
      File f = SD.open(Config::Measure::DATA_FILE, FILE_WRITE);
      if (f) {
        f.println("timestamp,mm,px");
        f.close();
      }
    }

    initialized_ = true;
    return true;
  }

  // ── Save a color measurement ────────────────────────────
  bool saveColor(const SpectralData &data) {
    if (!initialized_)
      return false;

    File f = SD.open(Config::Storage::COLORS_FILE, FILE_APPEND);
    if (!f) {
      Serial.println("[Storage] Failed to open colors file for append");
      return false;
    }

    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", data.r, data.g, data.b);

    // Write CSV line
    f.printf("%lu,%d,%d,%d,%s", data.timestamp, data.r, data.g, data.b, hex);

    for (int i = 0; i < Config::Sensor::NUM_CHANNELS; i++) {
      f.printf(",%u", data.raw[i]);
    }
    f.println();
    f.close();

    Serial.printf("[Storage] Color saved: %s\n", hex);
    return true;
  }

  // ── Load all saved colors ───────────────────────────────
  // Returns count of loaded colors, fills vector
  int loadColors(std::vector<SavedColor> &colors) {
    colors.clear();
    if (!initialized_)
      return 0;

    File f = SD.open(Config::Storage::COLORS_FILE, FILE_READ);
    if (!f)
      return 0;

    // Skip header line
    f.readStringUntil('\n');

    int index = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0)
        continue;

      SavedColor color;
      if (parseCsvLine(line, color)) {
        color.index = index++;
        colors.push_back(color);
      }

      if (index >= Config::Storage::MAX_SAVED_COLORS)
        break;
    }
    f.close();

    Serial.printf("[Storage] Loaded %d colors\n", colors.size());
    return colors.size();
  }

  // ── Delete a color by index ─────────────────────────────
  // Rewrites the file excluding the specified line
  bool deleteColor(int lineIndex) {
    if (!initialized_)
      return false;

    File src = SD.open(Config::Storage::COLORS_FILE, FILE_READ);
    if (!src)
      return false;

    // Read all lines
    std::vector<String> lines;
    while (src.available()) {
      lines.push_back(src.readStringUntil('\n'));
    }
    src.close();

    // Validate index (line 0 is header, data starts at line 1)
    int dataLine = lineIndex + 1;
    if (dataLine < 1 || dataLine >= static_cast<int>(lines.size())) {
      return false;
    }

    // Rewrite file without the deleted line
    File dst = SD.open(Config::Storage::COLORS_FILE, FILE_WRITE);
    if (!dst)
      return false;

    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
      if (i == dataLine)
        continue;
      lines[i].trim();
      if (lines[i].length() > 0) {
        dst.println(lines[i]);
      }
    }
    dst.close();

    Serial.printf("[Storage] Deleted color at index %d\n", lineIndex);
    return true;
  }

  // ── Save calibration data (JSON) ────────────────────────
  bool saveCalibration(const CalibrationData &cal) {
    if (!initialized_)
      return false;

    JsonDocument doc;

    doc["hasDark"] = cal.hasDark;
    doc["hasGray"] = cal.hasGray;
    doc["hasWhite"] = cal.hasWhite;
    doc["timestamp"] = cal.calibTimestamp;

    JsonArray dark = doc["darkRef"].to<JsonArray>();
    JsonArray gray = doc["grayRef"].to<JsonArray>();
    JsonArray white = doc["whiteRef"].to<JsonArray>();

    for (int i = 0; i < Config::Sensor::NUM_CHANNELS; i++) {
      dark.add(cal.darkRef[i]);
      gray.add(cal.grayRef[i]);
      white.add(cal.whiteRef[i]);
    }

    File f = SD.open(Config::Storage::CALIB_FILE, FILE_WRITE);
    if (!f)
      return false;

    serializeJsonPretty(doc, f);
    f.close();

    Serial.println("[Storage] Calibration saved");
    return true;
  }

  // ── Load calibration data ───────────────────────────────
  bool loadCalibration(CalibrationData &cal) {
    if (!initialized_)
      return false;

    File f = SD.open(Config::Storage::CALIB_FILE, FILE_READ);
    if (!f)
      return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
      Serial.printf("[Storage] JSON parse error: %s\n", err.c_str());
      return false;
    }

    cal.hasDark = doc["hasDark"] | false;
    cal.hasGray = doc["hasGray"] | false;
    cal.hasWhite = doc["hasWhite"] | false;
    cal.calibTimestamp = doc["timestamp"] | 0;

    JsonArray dark = doc["darkRef"];
    JsonArray gray = doc["grayRef"];
    JsonArray white = doc["whiteRef"];

    for (int i = 0; i < Config::Sensor::NUM_CHANNELS; i++) {
      cal.darkRef[i] = dark[i] | 0.0f;
      cal.grayRef[i] = gray[i] | 0.0f;
      cal.whiteRef[i] = white[i] | 0.0f;
    }

    Serial.println("[Storage] Calibration loaded");
    return true;
  }

  // ── Save a measurement ─────────────────────────────────
  bool saveMeasurement(float mm, uint16_t px) {
    if (!initialized_)
      return false;

    File f = SD.open(Config::Measure::DATA_FILE, FILE_APPEND);
    if (!f) {
      Serial.println("[Storage] Failed to open measurements file for append");
      return false;
    }

    f.printf("%lu,%.2f,%u\n", (unsigned long)millis(), mm, px);
    f.close();

    Serial.printf("[Storage] Measurement saved: %.2f mm\n", mm);
    return true;
  }

  // ── Load all saved measurements ──────────────────────────
  int loadMeasurements(std::vector<SavedMeasurement> &measurements) {
    measurements.clear();
    if (!initialized_)
      return 0;

    File f = SD.open(Config::Measure::DATA_FILE, FILE_READ);
    if (!f)
      return 0;

    // Skip header line
    f.readStringUntil('\n');

    int index = 0;
    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0)
        continue;

      SavedMeasurement m;
      if (parseMeasurementLine(line, m)) {
        m.index = index++;
        measurements.push_back(m);
      }

      if (index >= Config::Measure::MAX_SAVED_MEASUREMENTS)
        break;
    }
    f.close();

    Serial.printf("[Storage] Loaded %d measurements\n",
                  (int)measurements.size());
    return measurements.size();
  }

  // ── Delete a measurement by index ────────────────────────
  bool deleteMeasurement(int lineIndex) {
    if (!initialized_)
      return false;

    File src = SD.open(Config::Measure::DATA_FILE, FILE_READ);
    if (!src)
      return false;

    std::vector<String> lines;
    while (src.available()) {
      lines.push_back(src.readStringUntil('\n'));
    }
    src.close();

    int dataLine = lineIndex + 1; // line 0 is header
    if (dataLine < 1 || dataLine >= static_cast<int>(lines.size()))
      return false;

    File dst = SD.open(Config::Measure::DATA_FILE, FILE_WRITE);
    if (!dst)
      return false;

    for (int i = 0; i < static_cast<int>(lines.size()); i++) {
      if (i == dataLine)
        continue;
      lines[i].trim();
      if (lines[i].length() > 0) {
        dst.println(lines[i]);
      }
    }
    dst.close();

    Serial.printf("[Storage] Deleted measurement at index %d\n", lineIndex);
    return true;
  }

  bool isInitialized() const { return initialized_; }

private:
  StorageManager() : initialized_(false) {}

  bool parseCsvLine(const String &line, SavedColor &color) {
    // Parse: timestamp,r,g,b,hex,F1,...,FD
    int pos = 0;
    int field = 0;
    int start = 0;

    while (pos <= static_cast<int>(line.length()) && field < 18) {
      if (pos == static_cast<int>(line.length()) || line[pos] == ',') {
        String val = line.substring(start, pos);

        switch (field) {
        case 0:
          color.timestamp = val.toInt();
          break;
        case 1:
          color.r = val.toInt();
          break;
        case 2:
          color.g = val.toInt();
          break;
        case 3:
          color.b = val.toInt();
          break;
        case 4:
          strncpy(color.hex, val.c_str(), sizeof(color.hex) - 1);
          break;
        default:
          if (field - 5 < Config::Sensor::NUM_CHANNELS) {
            color.raw[field - 5] = val.toInt();
          }
          break;
        }
        field++;
        start = pos + 1;
      }
      pos++;
    }
    return field >= 5; // At minimum need timestamp, RGB, hex
  }

  bool parseMeasurementLine(const String &line, SavedMeasurement &m) {
    // Parse: timestamp,mm,px
    int c1 = line.indexOf(',');
    if (c1 < 0)
      return false;
    int c2 = line.indexOf(',', c1 + 1);
    if (c2 < 0)
      return false;

    m.timestamp = line.substring(0, c1).toInt();
    m.value_mm = line.substring(c1 + 1, c2).toFloat();
    m.value_px = line.substring(c2 + 1).toInt();
    return true;
  }

  bool initialized_;
};
