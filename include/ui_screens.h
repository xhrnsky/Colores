#pragma once
// ============================================================
// ui_screens.h – Screen rendering for each application state
//
// Each screen is a stateless render function that takes
// the current state data and draws to the display canvas.
// No business logic here – pure presentation.
// ============================================================

#include "config.h"
#include "display_manager.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include <vector>

namespace Screens {

// ── Boot / Splash Screen ────────────────────────────────────
inline void drawBoot(DisplayManager &disp, float progress, const char *status) {
  auto &c = disp.canvas();
  disp.clear();

  // Logo area
  c.setTextColor(Config::UI::COLOR_ACCENT);
  c.setTextSize(3);
  c.drawString("COLOR", 70, 30);
  c.setTextColor(TFT_WHITE);
  c.drawString("PICKER", 70, 60);

  // Version
  c.setTextSize(1);
  c.setTextColor(0x7BEF); // gray
  c.drawString("v1.0.0 | ESP32-C6 | AS7343", 60, 95);

  // Progress bar
  disp.drawProgressBar(40, 120, 240, 16, progress, Config::UI::COLOR_ACCENT);

  // Status text
  c.setTextColor(TFT_WHITE);
  c.setTextSize(1);
  c.drawString(status, 40, 145);

  disp.flush();
}

// ── Main Menu ───────────────────────────────────────────────
inline void drawMainMenu(DisplayManager &disp, int selectedIndex) {
  disp.clear();
  disp.drawHeader("Color Picker");

  const char *items[] = {"Pick Color", "Saved Colors", "Measure",
                         "Measurements", "Calibration"};

  for (int i = 0; i < 5; i++) {
    disp.drawMenuItem(i, items[i], i == selectedIndex);
  }

  // Draw decorative icons (using simple shapes)
  auto &c = disp.canvas();

  // Pick Color icon - eyedropper
  int iconX = Config::LCD::WIDTH - 60;
  c.fillCircle(iconX, 50, 8, Config::UI::COLOR_ACCENT);

  // Status bar
  disp.drawStatusBar("Scroll: Navigate | Press: Select");

  disp.flush();
}

// ── Pick Color – Live Measurement ───────────────────────────
inline void drawPickColor(DisplayManager &disp, const SpectralData &data,
                          bool measuring, const char *statusMsg) {
  disp.clear();
  disp.drawHeader("Pick Color");

  auto &c = disp.canvas();

  if (measuring) {
    // Measuring animation
    c.setTextColor(Config::UI::COLOR_ACCENT);
    c.setTextSize(2);
    c.drawString("Measuring...", 90, 70);

    // Animated dots
    static int dots = 0;
    dots = (dots + 1) % 4;
    for (int i = 0; i < dots; i++) {
      c.fillCircle(120 + i * 20, 100, 4, Config::UI::COLOR_ACCENT);
    }
  } else if (data.valid) {
    // ── Color swatch ────────────────────────────────────
    disp.drawColorSwatch(10, 38, 80, 60, data.toRGB565());

    // RGB values
    c.setTextColor(TFT_WHITE);
    c.setTextSize(1);
    char buf[32];

    snprintf(buf, sizeof(buf), "R: %d", data.r);
    c.drawString(buf, 100, 40);
    snprintf(buf, sizeof(buf), "G: %d", data.g);
    c.drawString(buf, 100, 55);
    snprintf(buf, sizeof(buf), "B: %d", data.b);
    c.drawString(buf, 100, 70);

    // HEX value
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", data.r, data.g, data.b);
    c.setTextSize(2);
    c.setTextColor(Config::UI::COLOR_ACCENT);
    c.drawString(hex, 100, 85);

    // ── Spectral bar chart ──────────────────────────────
    disp.drawSpectralBars(data.calibrated, 12, 180, 38, 130, 60);

    // Channel labels
    c.setTextSize(1);
    c.setTextColor(0x7BEF);
    c.drawString("F1 F2 FZ F3 F4 FY F5 XL F6 F7 F8 NR", 180, 100);

    // CIE XYZ
    snprintf(buf, sizeof(buf), "X:%.3f Y:%.3f Z:%.3f", data.cie_X, data.cie_Y,
             data.cie_Z);
    c.setTextColor(0x7BEF);
    c.drawString(buf, 10, 110);
  }

  // Status / action bar
  if (statusMsg) {
    disp.drawStatusBar(statusMsg);
  } else {
    disp.drawStatusBar("Press: Measure | Long: Back");
  }

  disp.flush();
}

// ── Pick Result – Save/Discard ──────────────────────────────
inline void drawPickResult(DisplayManager &disp, const SpectralData &data,
                           int selectedAction) {
  disp.clear();
  disp.drawHeader("Result");

  auto &c = disp.canvas();

  // Color swatch (larger)
  disp.drawColorSwatch(10, 38, 100, 70, data.toRGB565());

  // Color info
  c.setTextColor(TFT_WHITE);
  c.setTextSize(2);
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", data.r, data.g, data.b);
  c.drawString(hex, 120, 45);

  c.setTextSize(1);
  char buf[64];
  snprintf(buf, sizeof(buf), "RGB(%d, %d, %d)", data.r, data.g, data.b);
  c.drawString(buf, 120, 70);

  // Action buttons
  const char *actions[] = {"Save Color", "Discard", "Measure Again"};
  for (int i = 0; i < 3; i++) {
    int y = 115 + i * 18;
    uint16_t bg = (i == selectedAction) ? Config::UI::COLOR_SELECTED
                                        : Config::UI::COLOR_BG;
    uint16_t fg = (i == selectedAction) ? TFT_WHITE : 0xB596;

    c.fillRect(120, y, 190, 16, bg);
    c.setTextColor(fg, bg);
    c.drawString(actions[i], 128, y + 3);
  }

  disp.drawStatusBar("Scroll: Select | Press: Confirm");
  disp.flush();
}

// ── Saved Colors List ───────────────────────────────────────
inline void drawSavedColorsList(DisplayManager &disp,
                                const std::vector<SavedColor> &colors,
                                int selectedIndex, int scrollOffset) {
  disp.clear();

  char headerBuf[32];
  snprintf(headerBuf, sizeof(headerBuf), "Saved Colors (%d)",
           (int)colors.size());
  disp.drawHeader(headerBuf);

  auto &c = disp.canvas();

  if (colors.empty()) {
    c.setTextColor(0x7BEF);
    c.setTextSize(2);
    c.drawString("No colors saved", 60, 70);
    c.setTextSize(1);
    c.drawString("Go to Pick Color to start", 70, 100);
  } else {
    // Visible items (max 4-5 on screen)
    int visibleItems = 5;
    int startIdx = scrollOffset;
    int endIdx = min(startIdx + visibleItems, (int)colors.size());

    for (int i = startIdx; i < endIdx; i++) {
      int row = i - startIdx;
      int y = Config::UI::HEADER_HEIGHT + (row * Config::UI::MENU_ITEM_HEIGHT);
      bool sel = (i == selectedIndex);

      uint16_t bg = sel ? Config::UI::COLOR_SELECTED : Config::UI::COLOR_BG;
      c.fillRect(0, y, Config::LCD::WIDTH, Config::UI::MENU_ITEM_HEIGHT, bg);

      // Color swatch
      uint16_t swatch = ((colors[i].r & 0xF8) << 8) |
                        ((colors[i].g & 0xFC) << 3) | (colors[i].b >> 3);
      c.fillRoundRect(8, y + 4, 20, 20, 3, swatch);
      c.drawRoundRect(8, y + 4, 20, 20, 3, TFT_WHITE);

      // Color info
      c.setTextColor(sel ? TFT_WHITE : Config::UI::COLOR_FG, bg);
      c.setTextSize(1);

      char buf[48];
      snprintf(buf, sizeof(buf), "%s  R:%d G:%d B:%d", colors[i].hex,
               colors[i].r, colors[i].g, colors[i].b);
      c.drawString(buf, 36, y + 8);
    }

    // Scroll indicator
    if ((int)colors.size() > visibleItems) {
      int barHeight = Config::LCD::HEIGHT - Config::UI::HEADER_HEIGHT - 20;
      int thumbHeight = max(10, barHeight * visibleItems / (int)colors.size());
      int thumbY = Config::UI::HEADER_HEIGHT +
                   (barHeight - thumbHeight) * scrollOffset /
                       max(1, (int)colors.size() - visibleItems);

      c.fillRect(Config::LCD::WIDTH - 4, Config::UI::HEADER_HEIGHT, 4,
                 barHeight, 0x2104);
      c.fillRect(Config::LCD::WIDTH - 4, thumbY, 4, thumbHeight,
                 Config::UI::COLOR_ACCENT);
    }
  }

  disp.drawStatusBar("Press: View | Long: Back");
  disp.flush();
}

// ── Saved Color Detail ──────────────────────────────────────
inline void drawSavedColorDetail(DisplayManager &disp, const SavedColor &color,
                                 int selectedAction) {
  disp.clear();
  disp.drawHeader("Color Detail");

  auto &c = disp.canvas();

  // Large color swatch
  uint16_t swatch =
      ((color.r & 0xF8) << 8) | ((color.g & 0xFC) << 3) | (color.b >> 3);
  disp.drawColorSwatch(10, 38, 80, 60, swatch);

  // Info
  c.setTextColor(TFT_WHITE);
  c.setTextSize(2);
  c.drawString(color.hex, 100, 42);

  c.setTextSize(1);
  char buf[64];
  snprintf(buf, sizeof(buf), "RGB(%d, %d, %d)", color.r, color.g, color.b);
  c.drawString(buf, 100, 65);

  snprintf(buf, sizeof(buf), "Saved: %lu", color.timestamp);
  c.setTextColor(0x7BEF);
  c.drawString(buf, 100, 80);

  // Spectral data
  float spectral[12];
  for (int i = 0; i < 12; i++) {
    spectral[i] = static_cast<float>(color.raw[i]);
  }
  disp.drawSpectralBars(spectral, 12, 10, 105, 200, 35);

  // Actions
  const char *actions[] = {"Back", "Delete"};
  for (int i = 0; i < 2; i++) {
    int ax = 220 + i * 0;
    int ay = 110 + i * 22;
    uint16_t abg = (i == selectedAction) ? Config::UI::COLOR_SELECTED : 0x2104;
    uint16_t cfg2 = (i == selectedAction) ? TFT_WHITE
                                          : (i == 1 ? Config::UI::COLOR_ERROR
                                                    : Config::UI::COLOR_FG);

    c.fillRoundRect(220, ay, 90, 20, 3, abg);
    c.setTextColor(cfg2, abg);
    c.drawString(actions[i], 235, ay + 5);
  }

  disp.drawStatusBar("Scroll: Action | Press: Confirm");
  disp.flush();
}

// ── Calibration Menu ────────────────────────────────────────
inline void drawCalibrationMenu(DisplayManager &disp,
                                const CalibrationData &cal, int selectedIndex,
                                const char *gainLabel) {
  disp.clear();
  disp.drawHeader("Calibration");

  auto &c = disp.canvas();

  // Build gain item label with current value
  char gainItem[32];
  snprintf(gainItem, sizeof(gainItem), "Sensor Gain: %s", gainLabel);

  const char *items[] = {"Calibrate", gainItem, "Reset Calibration"};

  for (int i = 0; i < 3; i++) {
    int y = Config::UI::HEADER_HEIGHT + (i * Config::UI::MENU_ITEM_HEIGHT);
    bool sel = (i == selectedIndex);

    uint16_t bg = sel ? Config::UI::COLOR_SELECTED : Config::UI::COLOR_BG;
    c.fillRect(0, y, Config::LCD::WIDTH, Config::UI::MENU_ITEM_HEIGHT, bg);

    // Text
    c.setTextColor(sel ? TFT_WHITE : Config::UI::COLOR_FG, bg);
    c.setTextSize(1);
    c.drawString(items[i], 24, y + 8);
  }

  // Current calibration status
  int statusY =
      Config::UI::HEADER_HEIGHT + 3 * Config::UI::MENU_ITEM_HEIGHT + 4;
  c.setTextSize(1);
  c.setTextColor(0x7BEF);
  c.drawString("Status:", 10, statusY);

  const char *labels[] = {"Dark", "Gray", "White"};
  bool done[] = {cal.hasDark, cal.hasGray, cal.hasWhite};

  for (int i = 0; i < 3; i++) {
    int y = statusY + 14 + i * 12;
    uint16_t dotColor = done[i] ? Config::UI::COLOR_SUCCESS : 0x7BEF;
    c.fillCircle(18, y + 3, 3, dotColor);
    c.setTextColor(TFT_WHITE);
    c.drawString(labels[i], 28, y);
    c.setTextColor(done[i] ? Config::UI::COLOR_SUCCESS : 0x7BEF);
    c.drawString(done[i] ? "OK" : "--", 80, y);
  }

  disp.drawStatusBar("Press: Select | Long: Back");
  disp.flush();
}

// ── Calibration Capture Screen ──────────────────────────────
inline void drawCalibCapture(DisplayManager &disp, const char *type,
                             const char *instruction, bool capturing,
                             int step, int totalSteps) {
  disp.clear();

  char header[48];
  snprintf(header, sizeof(header), "Step %d/%d: %s", step, totalSteps, type);
  disp.drawHeader(header);

  auto &c = disp.canvas();

  if (capturing) {
    c.setTextColor(Config::UI::COLOR_WARNING);
    c.setTextSize(2);
    c.drawString("Capturing...", 80, 55);

    float progress = 0.5f;
    disp.drawProgressBar(40, 85, 240, 20, progress, Config::UI::COLOR_ACCENT);

    c.setTextSize(1);
    c.setTextColor(TFT_WHITE);
    c.drawString("Hold steady - averaging 10 samples", 40, 115);
  } else {
    c.setTextColor(TFT_WHITE);
    c.setTextSize(1);

    // Instruction
    c.drawString(instruction, 20, 50);

    c.setTextSize(2);
    c.setTextColor(Config::UI::COLOR_ACCENT);
    c.drawString("Press to capture", 50, 90);
  }

  disp.drawStatusBar("Press: Capture | Long: Cancel");
  disp.flush();
}

// ── Calibration Complete ────────────────────────────────────
inline void drawCalibComplete(DisplayManager &disp,
                              const CalibrationData &cal) {
  disp.clear();
  disp.drawHeader("Calibration Complete", Config::UI::COLOR_SUCCESS);

  auto &c = disp.canvas();

  c.setTextColor(Config::UI::COLOR_SUCCESS);
  c.setTextSize(2);
  c.drawString("Success!", 110, 45);

  c.setTextSize(1);
  c.setTextColor(TFT_WHITE);

  char buf[64];
  snprintf(buf, sizeof(buf), "Dark ref:  %s", cal.hasDark ? "OK" : "Missing");
  c.drawString(buf, 40, 75);
  snprintf(buf, sizeof(buf), "Gray ref:  %s", cal.hasGray ? "OK" : "Missing");
  c.drawString(buf, 40, 90);
  snprintf(buf, sizeof(buf), "White ref: %s", cal.hasWhite ? "OK" : "N/A");
  c.drawString(buf, 40, 105);

  c.setTextColor(0x7BEF);
  c.drawString("Calibration saved to SD card", 50, 130);

  disp.drawStatusBar("Press: Return to menu");
  disp.flush();
}

// ── Error Screen ────────────────────────────────────────────
inline void drawError(DisplayManager &disp, const char *title,
                      const char *message) {
  disp.clear();
  disp.drawHeader(title, Config::UI::COLOR_ERROR);

  auto &c = disp.canvas();

  // Error icon (X)
  c.drawLine(140, 50, 180, 90, Config::UI::COLOR_ERROR);
  c.drawLine(180, 50, 140, 90, Config::UI::COLOR_ERROR);

  c.setTextColor(TFT_WHITE);
  c.setTextSize(1);
  c.drawString(message, 20, 110);

  disp.drawStatusBar("Press: Acknowledge");
  disp.flush();
}

// ── Measure – Active Ruler ──────────────────────────────────
inline void drawMeasure(DisplayManager &disp, int16_t offset) {
  disp.clear();
  disp.drawHeader("Measure");

  auto &c = disp.canvas();

  const int centerX = Config::LCD::WIDTH / 2;
  const int areaTop = Config::UI::HEADER_HEIGHT;
  const int areaBottom = Config::LCD::HEIGHT - 20;
  const int centerY = areaTop + (areaBottom - areaTop) / 2;

  // Measurement lines (orange, full height) - always visible
  int leftX = centerX - offset;
  int rightX = centerX + offset;
  c.drawFastVLine(leftX, areaTop, areaBottom - areaTop, Config::UI::COLOR_WARNING);
  c.drawFastVLine(rightX, areaTop, areaBottom - areaTop, Config::UI::COLOR_WARNING);

  // Horizontal connector between lines
  if (offset > 0) {
    c.drawFastHLine(leftX, centerY, rightX - leftX, 0x4208);
  }

  // Crosshair (cyan, small) - drawn on top
  c.drawFastHLine(centerX - 5, centerY, 11, Config::UI::COLOR_ACCENT);
  c.drawFastVLine(centerX, centerY - 5, 11, Config::UI::COLOR_ACCENT);

  // Display current value
  uint16_t totalPx = offset * 2;
  float mm = totalPx * Config::Measure::PIXEL_PITCH_MM;

  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f mm", mm);
  c.setTextColor(Config::UI::COLOR_ACCENT);
  c.setTextSize(2);
  int textW = c.textWidth(buf);
  c.drawString(buf, centerX - textW / 2, centerY + 14);

  snprintf(buf, sizeof(buf), "(%d px)", totalPx);
  c.setTextColor(0x7BEF);
  c.setTextSize(1);
  textW = c.textWidth(buf);
  c.drawString(buf, centerX - textW / 2, centerY + 32);

  disp.drawStatusBar("Scroll: Adjust | Press: OK | Long: Back");
  disp.flush();
}

// ── Measure Result – Save/Discard ───────────────────────────
inline void drawMeasureResult(DisplayManager &disp, float mm, uint16_t px,
                              int selectedAction) {
  disp.clear();
  disp.drawHeader("Result");

  auto &c = disp.canvas();

  // Large mm value
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f mm", mm);
  c.setTextColor(Config::UI::COLOR_ACCENT);
  c.setTextSize(3);
  int textW = c.textWidth(buf);
  c.drawString(buf, Config::LCD::WIDTH / 2 - textW / 2, 42);

  // Pixel value
  snprintf(buf, sizeof(buf), "%d px", px);
  c.setTextColor(0x7BEF);
  c.setTextSize(1);
  textW = c.textWidth(buf);
  c.drawString(buf, Config::LCD::WIDTH / 2 - textW / 2, 72);

  // Visual range bar
  int barY = 90;
  int barLeft = Config::LCD::WIDTH / 2 - px / 2;
  int barRight = Config::LCD::WIDTH / 2 + px / 2;
  // Clamp to screen
  if (barLeft < 10) barLeft = 10;
  if (barRight > Config::LCD::WIDTH - 10) barRight = Config::LCD::WIDTH - 10;
  c.drawFastHLine(barLeft, barY, barRight - barLeft, Config::UI::COLOR_WARNING);
  c.drawFastVLine(barLeft, barY - 4, 9, Config::UI::COLOR_WARNING);
  c.drawFastVLine(barRight, barY - 4, 9, Config::UI::COLOR_WARNING);

  // Action buttons
  const char *actions[] = {"Save", "Discard", "Measure Again"};
  for (int i = 0; i < 3; i++) {
    int y = 105 + i * 18;
    uint16_t bg = (i == selectedAction) ? Config::UI::COLOR_SELECTED
                                        : Config::UI::COLOR_BG;
    uint16_t fg = (i == selectedAction) ? TFT_WHITE : 0xB596;

    c.fillRect(100, y, 120, 16, bg);
    c.setTextColor(fg, bg);
    c.setTextSize(1);
    c.drawString(actions[i], 108, y + 3);
  }

  disp.drawStatusBar("Scroll: Select | Press: Confirm");
  disp.flush();
}

// ── Measurements List ───────────────────────────────────────
inline void drawMeasurementsList(DisplayManager &disp,
                                 const std::vector<SavedMeasurement> &measurements,
                                 int selectedIndex, int scrollOffset) {
  disp.clear();

  char headerBuf[32];
  snprintf(headerBuf, sizeof(headerBuf), "Measurements (%d)",
           (int)measurements.size());
  disp.drawHeader(headerBuf);

  auto &c = disp.canvas();

  if (measurements.empty()) {
    c.setTextColor(0x7BEF);
    c.setTextSize(2);
    c.drawString("No measurements", 60, 60);
    c.setTextSize(1);
    c.drawString("Go to Measure to start", 80, 90);
  } else {
    int visibleItems = 5;
    int startIdx = scrollOffset;
    int endIdx = min(startIdx + visibleItems, (int)measurements.size());

    for (int i = startIdx; i < endIdx; i++) {
      int row = i - startIdx;
      int y = Config::UI::HEADER_HEIGHT + (row * Config::UI::MENU_ITEM_HEIGHT);
      bool sel = (i == selectedIndex);

      uint16_t bg = sel ? Config::UI::COLOR_SELECTED : Config::UI::COLOR_BG;
      c.fillRect(0, y, Config::LCD::WIDTH, Config::UI::MENU_ITEM_HEIGHT, bg);

      c.setTextColor(sel ? TFT_WHITE : Config::UI::COLOR_FG, bg);
      c.setTextSize(1);

      char buf[48];
      // Format: value in mm + timestamp as seconds
      unsigned long ts = measurements[i].timestamp;
      unsigned long sec = ts / 1000;
      unsigned long h = (sec / 3600) % 24;
      unsigned long m = (sec / 60) % 60;
      unsigned long s = sec % 60;
      snprintf(buf, sizeof(buf), "%.1f mm          %02lu:%02lu:%02lu",
               measurements[i].value_mm, h, m, s);
      c.drawString(buf, 16, y + 8);
    }

    // Scroll indicator
    if ((int)measurements.size() > visibleItems) {
      int barHeight = Config::LCD::HEIGHT - Config::UI::HEADER_HEIGHT - 20;
      int thumbHeight =
          max(10, barHeight * visibleItems / (int)measurements.size());
      int thumbY = Config::UI::HEADER_HEIGHT +
                   (barHeight - thumbHeight) * scrollOffset /
                       max(1, (int)measurements.size() - visibleItems);

      c.fillRect(Config::LCD::WIDTH - 4, Config::UI::HEADER_HEIGHT, 4,
                 barHeight, 0x2104);
      c.fillRect(Config::LCD::WIDTH - 4, thumbY, 4, thumbHeight,
                 Config::UI::COLOR_ACCENT);
    }
  }

  disp.drawStatusBar("Press: View | Long: Back");
  disp.flush();
}

// ── Measurement Detail ──────────────────────────────────────
inline void drawMeasurementDetail(DisplayManager &disp,
                                  const SavedMeasurement &m,
                                  int selectedAction) {
  disp.clear();
  disp.drawHeader("Measurement Detail");

  auto &c = disp.canvas();

  // Large value
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f mm", m.value_mm);
  c.setTextColor(Config::UI::COLOR_ACCENT);
  c.setTextSize(3);
  int textW = c.textWidth(buf);
  c.drawString(buf, Config::LCD::WIDTH / 2 - textW / 2, 45);

  // Pixel value
  snprintf(buf, sizeof(buf), "%d px", m.value_px);
  c.setTextColor(0x7BEF);
  c.setTextSize(1);
  textW = c.textWidth(buf);
  c.drawString(buf, Config::LCD::WIDTH / 2 - textW / 2, 78);

  // Timestamp
  unsigned long sec = m.timestamp / 1000;
  unsigned long h = (sec / 3600) % 24;
  unsigned long min = (sec / 60) % 60;
  unsigned long s = sec % 60;
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, min, s);
  c.setTextColor(0x7BEF);
  textW = c.textWidth(buf);
  c.drawString(buf, Config::LCD::WIDTH / 2 - textW / 2, 93);

  // Visual range bar
  int barY = 110;
  int halfPx = m.value_px / 2;
  int barLeft = Config::LCD::WIDTH / 2 - halfPx;
  int barRight = Config::LCD::WIDTH / 2 + halfPx;
  if (barLeft < 10) barLeft = 10;
  if (barRight > Config::LCD::WIDTH - 10) barRight = Config::LCD::WIDTH - 10;
  c.drawFastHLine(barLeft, barY, barRight - barLeft, Config::UI::COLOR_WARNING);
  c.drawFastVLine(barLeft, barY - 4, 9, Config::UI::COLOR_WARNING);
  c.drawFastVLine(barRight, barY - 4, 9, Config::UI::COLOR_WARNING);

  // Actions
  const char *actions[] = {"Back", "Delete"};
  for (int i = 0; i < 2; i++) {
    int ay = 125 + i * 20;
    uint16_t abg = (i == selectedAction) ? Config::UI::COLOR_SELECTED : 0x2104;
    uint16_t afg = (i == selectedAction) ? TFT_WHITE
                                         : (i == 1 ? Config::UI::COLOR_ERROR
                                                   : Config::UI::COLOR_FG);

    c.fillRoundRect(Config::LCD::WIDTH / 2 - 45, ay, 90, 18, 3, abg);
    c.setTextColor(afg, abg);
    c.setTextSize(1);
    int tw = c.textWidth(actions[i]);
    c.drawString(actions[i], Config::LCD::WIDTH / 2 - tw / 2, ay + 4);
  }

  disp.drawStatusBar("Scroll: Action | Press: Confirm");
  disp.flush();
}

} // namespace Screens
