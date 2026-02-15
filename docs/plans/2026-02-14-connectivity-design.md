# Connectivity: WiFi + BLE Color Sharing

## Overview
Add WiFi web server and BLE GATT to share measured colors with PC/mobile.

## Communication Channels

### WiFi Web Server
- ESPAsyncWebServer serves SPA dashboard
- AP + STA with auto fallback
- mDNS: `espc6.local`
- WebSocket live stream (2-5 updates/s)
- REST API for CRUD + device control
- PIN authentication with session cookie
- Web files on LittleFS (gzip compressed)

### BLE GATT
- Custom service UUID `0000ff00-...`
- Characteristics: Live Color (notify), Saved Colors (read), Control (write), Status (read/notify), Count (read)
- Web Bluetooth API compatible
- MTU 247 bytes

## REST API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/` | Dashboard HTML |
| POST | `/api/login` | PIN auth |
| GET | `/api/colors` | Saved colors JSON |
| DELETE | `/api/colors/:id` | Delete color |
| GET | `/api/colors/csv` | Download CSV |
| GET | `/api/measurements` | Measurements JSON |
| DELETE | `/api/measurements/:id` | Delete measurement |
| GET | `/api/measurements/csv` | Download CSV |
| GET | `/api/status` | Device status |
| POST | `/api/measure` | Trigger measurement |
| POST | `/api/settings` | Change gain/rotation |
| POST | `/api/calibrate` | Calibration step |

## WebSocket (`/ws`)
- Server pushes: `{"type":"live","rgb":[r,g,b],"hex":"#RRGGBB","channels":[...14]}`
- Client sends: `{"cmd":"measure"}`, `{"cmd":"setGain","value":16}`
- Max 3 concurrent connections

## Architecture

### New module: `connectivity_manager.h`
- Header-only, consistent with existing codebase
- New FreeRTOS task `taskConnectivity` (priority 1, stack 6KB)
- Reads last measurement from SensorManager, pushes via WS + BLE notify
- Remote commands → EventQueue → AppController

### New event types
- REMOTE_MEASURE, REMOTE_SET_GAIN, REMOTE_CALIBRATE
- WIFI_CONNECTED, WIFI_DISCONNECTED, BLE_CONNECTED, BLE_DISCONNECTED

### Settings UI additions
- WiFi On/Off, mode (AP/STA/Auto), SSID display
- BLE On/Off
- PIN change (via web interface)
- Status icons in header (WiFi + BLE)

### Storage
- `/connectivity.json` on SD card (WiFi SSID, password, PIN, BLE enable)

## Dependencies
- `mathieucarbou/ESPAsyncWebServer@^3.6.0`
- LittleFS (ESP32 core)
- BLE (ESP32 core)

## Implementation Order
1. LittleFS + web server base (AP mode, PIN auth)
2. REST API endpoints
3. WebSocket live stream
4. STA mode + mDNS + auto fallback
5. Web dashboard frontend (SPA)
6. BLE GATT server
7. Web Bluetooth page
8. Remote control (web/BLE → EventQueue)
9. Settings UI on display
10. Connectivity config persistence
