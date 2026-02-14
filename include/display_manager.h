#pragma once
// ============================================================
// display_manager.h – ST7789 display abstraction via LovyanGFX
//
// Library choice: LovyanGFX over TFT_eSPI
// Justification:
//   1. Better ESP32-C6 (RISC-V) support
//   2. DMA-based SPI transfers for flicker-free rendering
//   3. Sprite (off-screen buffer) support for smooth UI
//   4. Cleaner C++ API
//   5. Active maintenance with ESP32-C6 compatibility
// ============================================================

#define LGFX_USE_V1
#include "config.h"
#include <Arduino.h>
#include <LovyanGFX.hpp>

// ── LovyanGFX Hardware Configuration ────────────────────────
class LGFX_ColorPicker : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
  lgfx::Light_PWM backlight_;

public:
  LGFX_ColorPicker() {
    // SPI bus configuration
    {
      auto cfg = bus_.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = Config::LCD::SPI_FREQ;
      cfg.freq_read = 16000000;
      cfg.pin_sclk = Config::LCD::SCLK;
      cfg.pin_mosi = Config::LCD::MOSI;
      cfg.pin_miso = Config::SD::MISO; // Include MISO for bus sharing
      cfg.pin_dc = Config::LCD::DC;
      bus_.config(cfg);
      panel_.setBus(&bus_);
    }

    // Panel configuration
    {
      auto cfg = panel_.config();
      cfg.pin_cs = Config::LCD::CS;
      cfg.pin_rst = Config::LCD::RST;
      cfg.pin_busy = -1;
      cfg.panel_width = 172;
      cfg.panel_height = 320;
      cfg.offset_x = 34; // ST7789 offset for 172px panel
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true; // ST7789 typically needs inversion
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = true; // SPI shared with SD card
      panel_.config(cfg);
    }

    // Backlight configuration
    {
      auto cfg = backlight_.config();
      cfg.pin_bl = Config::LCD::BL;
      cfg.invert = false;
      cfg.freq = Config::LCD::BL_PWM_FREQ;
      cfg.pwm_channel = Config::LCD::BL_PWM_CHANNEL;
      backlight_.config(cfg);
      panel_.setLight(&backlight_);
    }

    setPanel(&panel_);
  }
};

// ── Display Manager ─────────────────────────────────────────
class DisplayManager {
public:
  static DisplayManager &instance() {
    static DisplayManager inst;
    return inst;
  }

  bool init() {
    lcd_.init();
    lcd_.setRotation(Config::LCD::ROTATION);
    lcd_.setBrightness(Config::LCD::BL_DEFAULT);
    lcd_.fillScreen(TFT_BLACK);

    // Create sprite (full-screen back buffer) for flicker-free rendering
    sprite_.createSprite(Config::LCD::WIDTH, Config::LCD::HEIGHT);
    sprite_.setSwapBytes(true);

    initialized_ = true;
    Serial.println("[Display] ST7789 initialized (320x172 landscape)");
    return true;
  }

  // Access the sprite for double-buffered drawing
  LGFX_Sprite &canvas() { return sprite_; }

  // Push sprite to display (call after drawing a complete frame)
  void flush() { sprite_.pushSprite(&lcd_, 0, 0); }

  // Direct LCD access (for special cases)
  LGFX_ColorPicker &lcd() { return lcd_; }

  void setBrightness(uint8_t level) { lcd_.setBrightness(level); }

  bool isInitialized() const { return initialized_; }

  // ── Drawing helpers ─────────────────────────────────────

  void clear(uint16_t color = TFT_BLACK) { sprite_.fillScreen(color); }

  void drawHeader(const char *title,
                  uint16_t bgColor = Config::UI::COLOR_HEADER_BG) {
    sprite_.fillRect(0, 0, Config::LCD::WIDTH, Config::UI::HEADER_HEIGHT,
                     bgColor);
    sprite_.setTextColor(TFT_WHITE, bgColor);
    sprite_.setTextSize(Config::UI::FONT_SIZE_TITLE);
    sprite_.drawString(title, Config::UI::PADDING, 7);
  }

  void drawMenuItem(int index, const char *text, bool selected,
                    int startY = Config::UI::HEADER_HEIGHT) {
    int y = startY + (index * Config::UI::MENU_ITEM_HEIGHT);
    uint16_t bg = selected ? Config::UI::COLOR_SELECTED : Config::UI::COLOR_BG;
    uint16_t fg = selected ? TFT_WHITE : Config::UI::COLOR_FG;

    sprite_.fillRect(0, y, Config::LCD::WIDTH, Config::UI::MENU_ITEM_HEIGHT,
                     bg);
    sprite_.setTextColor(fg, bg);
    sprite_.setTextSize(Config::UI::FONT_SIZE_TITLE);

    if (selected) {
      sprite_.drawString("> ", Config::UI::PADDING, y + 6);
      sprite_.drawString(text, Config::UI::PADDING + 16, y + 6);
    } else {
      sprite_.drawString(text, Config::UI::PADDING + 16, y + 6);
    }
  }

  // Draw a filled color swatch
  void drawColorSwatch(int x, int y, int w, int h, uint16_t color565) {
    sprite_.fillRoundRect(x, y, w, h, 4, color565);
    sprite_.drawRoundRect(x, y, w, h, 4, TFT_WHITE);
  }

  // Draw spectral bar chart
  void drawSpectralBars(const float *values, int count, int x, int y, int w,
                        int h) {
    float maxVal = 0;
    for (int i = 0; i < count; i++) {
      if (values[i] > maxVal)
        maxVal = values[i];
    }
    if (maxVal <= 0)
      maxVal = 1;

    int barWidth = (w - (count - 1) * 2) / count;

    // Spectral colors for visual representation
    static const uint16_t spectralColors[] = {
        0x780F, // F1 - violet
        0x001F, // F2 - blue
        0x001F, // FZ - blue
        0x07FF, // F3 - cyan
        0x07E0, // F4 - green
        0x07E0, // FY - green
        0xFFE0, // F5 - yellow
        0xFD20, // FXL - orange
        0xFC60, // F6 - orange
        0xF800, // F7 - red
        0xF800, // F8 - deep red
        0x8000, // NIR - dark red
    };

    for (int i = 0; i < count && i < 12; i++) {
      int barH = static_cast<int>((values[i] / maxVal) * h);
      int bx = x + i * (barWidth + 2);
      int by = y + h - barH;

      sprite_.fillRect(bx, by, barWidth, barH, spectralColors[i]);
    }
  }

  // Draw progress bar
  void drawProgressBar(int x, int y, int w, int h, float progress,
                       uint16_t color) {
    sprite_.drawRect(x, y, w, h, TFT_WHITE);
    int fillW = static_cast<int>(progress * (w - 2));
    sprite_.fillRect(x + 1, y + 1, fillW, h - 2, color);
  }

  // Status bar at bottom
  void drawStatusBar(const char *text,
                     uint16_t color = Config::UI::COLOR_ACCENT) {
    int y = Config::LCD::HEIGHT - 20;
    sprite_.fillRect(0, y, Config::LCD::WIDTH, 20, Config::UI::COLOR_HEADER_BG);
    sprite_.setTextColor(color, Config::UI::COLOR_HEADER_BG);
    sprite_.setTextSize(1);
    sprite_.drawString(text, Config::UI::PADDING, y + 6);
  }

private:
  DisplayManager() : initialized_(false) {}

  LGFX_ColorPicker lcd_;
  LGFX_Sprite sprite_{&lcd_};
  bool initialized_;
};
