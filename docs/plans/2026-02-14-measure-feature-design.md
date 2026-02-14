# Measure Feature Design

## Overview

Add a digital caliper/ruler to the Color Picker device. The user places an object on the display, adjusts two symmetric vertical lines to match the object's edges using the rotary encoder, and reads the real-world dimension in mm.

## Display Specifications

- ST7789, 1.47", 320x172 px (landscape)
- Active area: 32.35 x 17.39 mm (Waveshare spec)
- Pixel pitch: ~0.101 mm/px (32.35 / 320)

## Menu Structure

Main menu expands from 3 to 5 items:

1. Pick Color
2. Saved Colors
3. Measure
4. Measurements (history)
5. Calibration

## New States

```
MEASURE              – active ruler screen
MEASURE_RESULT       – result with Save/Discard/Measure Again
MEASUREMENTS_LIST    – saved measurements list
MEASUREMENT_DETAIL   – single measurement detail with Back/Delete
```

### Navigation

```
MAIN_MENU ──press──> MEASURE
MEASURE ──press──> MEASURE_RESULT
MEASURE ──long──> MAIN_MENU
MEASURE_RESULT ──Save──> MEASURE
MEASURE_RESULT ──Discard──> MEASURE
MEASURE_RESULT ──Again──> MEASURE
MAIN_MENU ──press──> MEASUREMENTS_LIST
MEASUREMENTS_LIST ──press──> MEASUREMENT_DETAIL
MEASUREMENTS_LIST ──long──> MAIN_MENU
MEASUREMENT_DETAIL ──Back──> MEASUREMENTS_LIST
MEASUREMENT_DETAIL ──Delete──> MEASUREMENTS_LIST
MEASUREMENT_DETAIL ──long──> MEASUREMENTS_LIST
```

## Data

```cpp
struct SavedMeasurement {
    float value_mm;
    uint16_t value_px;
    uint32_t timestamp;
    int index;          // for deletion
};
```

Storage: `/measurements.csv` on SD card, format `timestamp,mm,px`, append-only (same approach as `colors.csv`).

## Configuration Constants

```cpp
namespace Config::Measure {
    constexpr float PIXEL_PITCH_MM = 0.10109f;   // 32.35mm / 320px
    constexpr int16_t MAX_OFFSET_PX = 155;
    constexpr float MAX_RANGE_MM = MAX_OFFSET_PX * 2 * PIXEL_PITCH_MM;

    // Encoder acceleration
    constexpr uint32_t ACCEL_SLOW_MS = 150;      // > 150ms between clicks = 1px step
    constexpr uint32_t ACCEL_MED_MS = 80;        // 80-150ms = 3px step
    constexpr uint8_t STEP_SLOW = 1;             // ~0.1mm
    constexpr uint8_t STEP_MED = 3;              // ~0.3mm
    constexpr uint8_t STEP_FAST = 8;             // ~0.8mm

    constexpr const char* DATA_FILE = "/measurements.csv";
}
```

## Screen Designs

### MEASURE Screen

```
+--[Header: "Measure"]-----------------------------+
|                                                    |
|         |              +              |             |
|         |              |              |             |
|         |         -----+-----         |             |
|         |              |              |             |
|         |              +              |             |
|                                                    |
|                   (124 px)                         |
|                  12.5 mm                           |
|                                                    |
+--[Scroll: Adjust | Press: OK | Long: Back]--------+
```

- Center crosshair at display center (X=160, Y=91): 10px horizontal + 10px vertical lines, cyan
- Two vertical measurement lines: full height between header and status bar, orange (`COLOR_WARNING`), 1px wide
- Lines positioned symmetrically: `centerX - offset` and `centerX + offset`
- Current value displayed below crosshair: mm in font size 2 (cyan), px in font size 1 (gray)
- Offset range: 0 to MAX_OFFSET_PX (155)

### MEASURE_RESULT Screen

```
+--[Header: "Result"]-----------------------+
|                                            |
|              12.5 mm          (font 3)     |
|              124 px           (font 1)     |
|                                            |
|         <|--- visual bar ---|>             |
|                                            |
|              > Save                        |
|                Discard                     |
|                Measure Again               |
|                                            |
+--[Scroll: Select | Press: Confirm]--------+
```

- Large mm value centered, cyan, font size 3
- Pixel value below in gray
- Visual horizontal bar with markers showing the span
- 3 actions navigated by encoder, same pattern as Pick Result

### MEASUREMENTS_LIST Screen

Same pattern as Saved Colors List:
- Header: "Measurements (N)"
- Scrollable list, max 5 visible items
- Each row: `"14.2 mm    12:35:04"`
- Scroll indicator on right if > 5 items
- Empty state: "No measurements saved"

### MEASUREMENT_DETAIL Screen

Same pattern as Saved Color Detail:
- Header: "Measurement Detail"
- Large value: "14.2 mm" (font size 3, cyan)
- Below: "142 px" + formatted timestamp
- Visual range bar
- 2 actions: Back, Delete (red)

## Encoder Acceleration

Track time between ENCODER_CW/CCW events in the MEASURE state:

```
dt > 150ms  → step = 1px  (~0.1mm)
80ms < dt <= 150ms → step = 3px  (~0.3mm)
dt <= 80ms  → step = 8px  (~0.8mm)
```

Store `lastEncoderEventTime_` in AppController to compute delta.

## Files to Modify

1. **config.h** – add `Config::Measure` namespace
2. **state_machine.h** – add 4 new `AppState` values, update `getParentState()`
3. **storage_manager.h** – add `SavedMeasurement` struct, `saveMeasurement()`, `loadMeasurements()`, `deleteMeasurement()`
4. **ui_screens.h** – add `drawMeasure()`, `drawMeasureResult()`, `drawMeasurementsList()`, `drawMeasurementDetail()`
5. **app_controller.h** – add event handlers, state variables, menu expansion from 3 to 5 items, encoder acceleration logic
