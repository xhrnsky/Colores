#pragma once
// ============================================================
// connectivity_manager.h – WiFi + BLE connectivity
//
// Provides:
//   - WiFi AP+STA with auto fallback
//   - AsyncWebServer with REST API + WebSocket live stream
//   - BLE GATT server with Web Bluetooth API support
//   - PIN-based authentication
//   - mDNS discovery (espc6.local)
// ============================================================

#include "config.h"
#include "events.h"
#include "sensor_manager.h"
#include "storage_manager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <ESPAsyncWebServer.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

// ── Connectivity Configuration (persisted to SD) ────────────
struct ConnectivityConfig {
  bool wifiEnabled = true;
  bool bleEnabled = true;
  char wifiSsid[33] = "";       // STA mode SSID
  char wifiPassword[65] = "";   // STA mode password
  char pin[9] = "1234";         // Web access PIN
  uint8_t wifiMode = 2;         // 0=AP, 1=STA, 2=Auto (AP+STA fallback)
};

// ── BLE Callbacks ───────────────────────────────────────────
class ConnectivityManager; // Forward declaration

class BleServerCallbacks : public BLEServerCallbacks {
public:
  void onConnect(BLEServer *server) override {
    bleConnected_ = true;
    EventQueue::send(EventType::BLE_CLIENT_CONNECTED);
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(BLEServer *server) override {
    bleConnected_ = false;
    EventQueue::send(EventType::BLE_CLIENT_DISCONNECTED);
    Serial.println("[BLE] Client disconnected");
    // Restart advertising
    server->startAdvertising();
  }
  bool isConnected() const { return bleConnected_; }

private:
  bool bleConnected_ = false;
};

class BleControlCallbacks : public BLECharacteristicCallbacks {
public:
  void onWrite(BLECharacteristic *characteristic) override {
    String value = characteristic->getValue();
    if (value.length() == 0)
      return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, value);
    if (err)
      return;

    const char *cmd = doc["cmd"];
    if (!cmd)
      return;

    if (strcmp(cmd, "measure") == 0) {
      EventQueue::send(EventType::REMOTE_MEASURE);
    } else if (strcmp(cmd, "setGain") == 0) {
      int val = doc["value"] | -1;
      if (val >= 0)
        EventQueue::send(EventType::REMOTE_SET_GAIN, val);
    } else if (strcmp(cmd, "calibrate") == 0) {
      int step = doc["step"] | -1;
      if (step >= 0)
        EventQueue::send(EventType::REMOTE_CALIBRATE, step);
    }
  }
};

// ── Connectivity Manager ────────────────────────────────────
class ConnectivityManager {
public:
  static ConnectivityManager &instance() {
    static ConnectivityManager inst;
    return inst;
  }

  bool init() {
    // Load config from SD (only if SD is available)
    if (StorageManager::instance().isInitialized()) {
      loadConfig();
    } else {
      Serial.println("[Conn] SD not available, using default config");
    }

    // Initialize LittleFS for web files
    if (!LittleFS.begin(true)) {
      Serial.println("[Conn] LittleFS mount failed");
    } else {
      Serial.println("[Conn] LittleFS mounted");
      // Check if web files exist
      File f = LittleFS.open("/www/index.html", "r");
      if (f) {
        Serial.printf("[Conn] Web files found (%d bytes)\n", f.size());
        f.close();
      } else {
        Serial.println("[Conn] WARNING: /www/index.html not found in LittleFS");
        Serial.println("[Conn] Run 'pio run -t uploadfs' to upload web files");
      }
    }

    // Initialize BLE FIRST – before WiFi+WebServer.
    // BLE (NimBLE) needs contiguous heap for its memory pools.
    // WiFi+AsyncWebServer fragments the heap, making BLE init fail.
    if (config_.bleEnabled) {
      initBLE();
    }

    // ESP32-C6 shared radio needs settling time between BLE and WiFi init
    if (config_.wifiEnabled && config_.bleEnabled) {
      Serial.printf("[Conn] Waiting for radio settling, free heap: %d\n",
                    ESP.getFreeHeap());
      vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (config_.wifiEnabled) {
      uint32_t heapBefore = ESP.getFreeHeap();
      if (heapBefore < 80000) {
        Serial.printf("[Conn] Not enough heap for WiFi (%d < 80000), skipping\n",
                      heapBefore);
        config_.wifiEnabled = false;
      } else {
        initWiFi();
        initWebServer();
      }
    }

    initialized_ = true;
    Serial.printf("[Conn] Init complete. Free heap: %d\n", ESP.getFreeHeap());
    return true;
  }

  // Called periodically from connectivity task
  void update() {
    if (!initialized_)
      return;

    uint32_t now = millis();

    // Push live data via WebSocket
    if (config_.wifiEnabled && (now - lastWsPush_ >= Config::Connectivity::WS_INTERVAL_MS)) {
      pushWebSocketData();
      lastWsPush_ = now;
    }

    // Push live data via BLE notify
    if (config_.bleEnabled && bleServerCallbacks_.isConnected() &&
        (now - lastBlePush_ >= Config::Connectivity::WS_INTERVAL_MS)) {
      pushBLEData();
      lastBlePush_ = now;
    }

    // WebSocket cleanup
    if (config_.wifiEnabled) {
      ws_.cleanupClients(Config::Connectivity::WS_MAX_CLIENTS);
    }
  }

  // Set latest measurement for broadcasting
  void setLiveData(const SpectralData &data) {
    liveData_ = data;
    hasLiveData_ = true;
  }

  // Getters for UI status display
  bool isWiFiConnected() const { return wifiConnected_; }
  bool isAPMode() const { return apMode_; }
  bool isBLEConnected() const { return bleServerCallbacks_.isConnected(); }
  bool isWiFiEnabled() const { return config_.wifiEnabled; }
  bool isBLEEnabled() const { return config_.bleEnabled; }
  int wsClientCount() const { return ws_.count(); }
  String getIPAddress() const {
    if (apMode_)
      return WiFi.softAPIP().toString();
    return WiFi.localIP().toString();
  }
  const ConnectivityConfig &getConfig() const { return config_; }

  // Config setters
  void setWiFiEnabled(bool enabled) {
    config_.wifiEnabled = enabled;
    saveConfig();
  }
  void setBLEEnabled(bool enabled) {
    config_.bleEnabled = enabled;
    saveConfig();
  }
  void setWiFiCredentials(const char *ssid, const char *password) {
    strncpy(config_.wifiSsid, ssid, sizeof(config_.wifiSsid) - 1);
    strncpy(config_.wifiPassword, password, sizeof(config_.wifiPassword) - 1);
    saveConfig();
  }
  void setPin(const char *newPin) {
    strncpy(config_.pin, newPin, sizeof(config_.pin) - 1);
    saveConfig();
  }

private:
  ConnectivityManager()
      : server_(Config::Connectivity::HTTP_PORT), ws_("/ws") {}

  // ── WiFi Init ──────────────────────────────────────────────
  void initWiFi() {
    bool staConnected = false;

    Serial.printf("[WiFi] Free heap before WiFi init: %d\n", ESP.getFreeHeap());

    // Clean slate – ESP32-C6 needs longer settling after mode changes
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (config_.wifiMode >= 1 && strlen(config_.wifiSsid) > 0) {
      // Try STA mode first
      Serial.printf("[WiFi] Connecting to %s...\n", config_.wifiSsid);
      WiFi.mode(WIFI_STA);
      vTaskDelay(pdMS_TO_TICKS(500));
      WiFi.setSleep(false);
      WiFi.begin(config_.wifiSsid, config_.wifiPassword);

      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
        attempts++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        staConnected = true;
        wifiConnected_ = true;
        apMode_ = false;
        Serial.printf("\n[WiFi] STA connected, IP: %s\n",
                      WiFi.localIP().toString().c_str());
        EventQueue::send(EventType::WIFI_CONNECTED);
      } else {
        Serial.println("\n[WiFi] STA connection failed");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }

    // Fall back to AP mode if STA failed or mode is AP-only
    if (!staConnected) {
      Serial.println("[WiFi] Starting AP mode...");
      WiFi.mode(WIFI_AP);
      vTaskDelay(pdMS_TO_TICKS(1000));

      // Disable power save AFTER mode is set – critical for ESP32-C6
      WiFi.setSleep(false);

      // Explicit AP IP configuration – must be before softAP()
      IPAddress apIP(192, 168, 4, 1);
      IPAddress gateway(192, 168, 4, 1);
      IPAddress subnet(255, 255, 255, 0);
      WiFi.softAPConfig(apIP, gateway, subnet);
      vTaskDelay(pdMS_TO_TICKS(200));

      bool apOk = WiFi.softAP(Config::Connectivity::AP_SSID,
                               Config::Connectivity::AP_PASSWORD,
                               1,  // channel 1
                               0,  // not hidden
                               4); // max 4 connections

      // ESP32-C6 needs longer delay for DHCP server to start
      vTaskDelay(pdMS_TO_TICKS(2000));

      if (apOk && WiFi.softAPIP() != IPAddress(0, 0, 0, 0)) {
        apMode_ = true;
        wifiConnected_ = true;
        Serial.printf("[WiFi] AP started: %s, IP: %s\n",
                      Config::Connectivity::AP_SSID,
                      WiFi.softAPIP().toString().c_str());
        EventQueue::send(EventType::WIFI_CONNECTED);
      } else {
        Serial.printf("[WiFi] ERROR: AP start failed! apOk=%d IP=%s\n",
                      apOk, WiFi.softAPIP().toString().c_str());
        wifiConnected_ = false;
      }
    }

    // Start mDNS
    if (wifiConnected_ && MDNS.begin(Config::Connectivity::MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", Config::Connectivity::HTTP_PORT);
      Serial.printf("[WiFi] mDNS: %s.local\n",
                    Config::Connectivity::MDNS_HOSTNAME);
    }

    Serial.printf("[WiFi] Free heap after WiFi init: %d\n", ESP.getFreeHeap());
  }

  // ── Web Server Init ────────────────────────────────────────
  void initWebServer() {
    // WebSocket handler
    ws_.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client,
                       AwsEventType type, void *arg, uint8_t *data,
                       size_t len) { onWsEvent(server, client, type, arg, data, len); });
    server_.addHandler(&ws_);

    // --- Authentication ---
    server_.on("/api/login", HTTP_POST,
               [this](AsyncWebServerRequest *request) { handleLogin(request); });

    // --- REST API ---
    server_.on("/api/status", HTTP_GET,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handleStatus(request);
               });

    server_.on("/api/colors", HTTP_GET,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handleGetColors(request);
               });

    server_.on("/api/colors/csv", HTTP_GET,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handleDownloadColorsCsv(request);
               });

    server_.on("/api/measurements", HTTP_GET,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handleGetMeasurements(request);
               });

    server_.on("/api/measurements/csv", HTTP_GET,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handleDownloadMeasurementsCsv(request);
               });

    server_.on("/api/measure", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 EventQueue::send(EventType::REMOTE_MEASURE);
                 request->send(200, "application/json", "{\"ok\":true}");
               });

    server_.on("/api/settings", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handlePostSettings(request);
               });

    server_.on("/api/calibrate", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handlePostCalibrate(request);
               });

    // Delete color by index (query param ?id=N)
    server_.on("/api/colors/delete", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 if (request->hasParam("id")) {
                   int id = request->getParam("id")->value().toInt();
                   EventQueue::send(EventType::REMOTE_DELETE_COLOR, id);
                   request->send(200, "application/json", "{\"ok\":true}");
                 } else {
                   request->send(400, "application/json",
                                 "{\"error\":\"missing id\"}");
                 }
               });

    // Delete measurement by index
    server_.on("/api/measurements/delete", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 if (request->hasParam("id")) {
                   int id = request->getParam("id")->value().toInt();
                   EventQueue::send(EventType::REMOTE_DELETE_MEASUREMENT, id);
                   request->send(200, "application/json", "{\"ok\":true}");
                 } else {
                   request->send(400, "application/json",
                                 "{\"error\":\"missing id\"}");
                 }
               });

    // WiFi config endpoint
    server_.on("/api/wifi", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handlePostWifi(request);
               });

    // PIN change endpoint
    server_.on("/api/pin", HTTP_POST,
               [this](AsyncWebServerRequest *request) {
                 if (!checkAuth(request))
                   return;
                 handlePostPin(request);
               });

    // Serve static files from LittleFS
    server_.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

    // Fallback: if no LittleFS files, show basic status page
    server_.onNotFound([this](AsyncWebServerRequest *request) {
      if (request->url() == "/" || request->url() == "/index.html") {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                      "<title>ESPC6</title></head><body style='font-family:sans-serif;"
                      "background:#111;color:#eee;padding:40px;text-align:center'>"
                      "<h1 style='color:#07ff'>ESPC6 Color Picker</h1>"
                      "<p>Web UI not uploaded to LittleFS.</p>"
                      "<p>Run: <code style='color:#0f0'>pio run -t uploadfs</code></p>"
                      "<p style='color:#888'>IP: " + getIPAddress() +
                      " | Heap: " + String(ESP.getFreeHeap()) + " B</p>"
                      "</body></html>";
        request->send(200, "text/html", html);
      } else {
        request->send(404, "text/plain", "Not Found");
      }
    });

    // CORS headers for Web Bluetooth companion page
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                         "GET, POST, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                         "Content-Type, Authorization");

    server_.begin();
    Serial.println("[Web] Server started on port 80");
  }

  // ── BLE Init ───────────────────────────────────────────────
  void initBLE() {
    Serial.printf("[BLE] Starting init, free heap: %d\n", ESP.getFreeHeap());

    // ESP32-C6 BT stack needs sufficient heap for controller + host init
    if (ESP.getFreeHeap() < 70000) {
      Serial.printf("[BLE] ERROR: Not enough heap (%d < 70000), skipping BLE\n",
                    ESP.getFreeHeap());
      config_.bleEnabled = false;
      return;
    }

    // Small delay to let memory settle
    vTaskDelay(pdMS_TO_TICKS(500));

    BLEDevice::init(Config::Connectivity::BLE_DEVICE_NAME);
    Serial.printf("[BLE] Device initialized, free heap: %d\n",
                  ESP.getFreeHeap());

    bleServer_ = BLEDevice::createServer();
    if (!bleServer_) {
      Serial.println("[BLE] ERROR: Failed to create server");
      config_.bleEnabled = false;
      return;
    }
    bleServer_->setCallbacks(&bleServerCallbacks_);

    BLEService *service =
        bleServer_->createService(Config::Connectivity::BLE_SERVICE_UUID);
    if (!service) {
      Serial.println("[BLE] ERROR: Failed to create service");
      config_.bleEnabled = false;
      return;
    }

    // Live color characteristic (notify)
    // Note: NimBLE (ESP32-C6) auto-creates the CCCD (2902) descriptor
    // for characteristics with NOTIFY/INDICATE property
    bleLiveChar_ = service->createCharacteristic(
        Config::Connectivity::BLE_CHAR_LIVE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    // Saved colors characteristic (read)
    bleSavedChar_ = service->createCharacteristic(
        Config::Connectivity::BLE_CHAR_SAVED_UUID,
        BLECharacteristic::PROPERTY_READ);

    // Control characteristic (write)
    bleControlChar_ = service->createCharacteristic(
        Config::Connectivity::BLE_CHAR_CONTROL_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    bleControlChar_->setCallbacks(&bleControlCallbacks_);

    // Status characteristic (read + notify)
    bleStatusChar_ = service->createCharacteristic(
        Config::Connectivity::BLE_CHAR_STATUS_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);

    service->start();

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(Config::Connectivity::BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06); // helps with iOS/Android discovery
    advertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.printf("[BLE] GATT server started, free heap: %d\n",
                  ESP.getFreeHeap());
  }

  // ── Authentication ─────────────────────────────────────────
  bool checkAuth(AsyncWebServerRequest *request) {
    if (request->hasHeader("Authorization")) {
      String auth = request->getHeader("Authorization")->value();
      if (auth.startsWith("Bearer ")) {
        String token = auth.substring(7);
        if (token == sessionToken_ && sessionToken_.length() > 0) {
          return true;
        }
      }
    }
    // Also check query param for simple access
    if (request->hasParam("token")) {
      String token = request->getParam("token")->value();
      if (token == sessionToken_ && sessionToken_.length() > 0) {
        return true;
      }
    }
    request->send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
  }

  void handleLogin(AsyncWebServerRequest *request) {
    if (request->hasParam("pin")) {
      String pin = request->getParam("pin")->value();
      if (pin == config_.pin) {
        // Generate simple session token
        sessionToken_ = String(esp_random(), HEX) + String(esp_random(), HEX);
        String response = "{\"token\":\"" + sessionToken_ + "\"}";
        request->send(200, "application/json", response);
        Serial.println("[Web] Login successful");
        return;
      }
    }
    request->send(403, "application/json", "{\"error\":\"invalid pin\"}");
  }

  // ── REST API Handlers ──────────────────────────────────────
  void handleStatus(AsyncWebServerRequest *request) {
    JsonDocument doc;
    auto &sensor = SensorManager::instance();
    doc["gain"] = sensor.getGainLabel();
    doc["gainIndex"] = sensor.getGainIndex();
    auto &cal = sensor.getCalibration();
    doc["calibDark"] = cal.hasDark;
    doc["calibGray"] = cal.hasGray;
    doc["calibWhite"] = cal.hasWhite;
    doc["wifiMode"] = apMode_ ? "AP" : "STA";
    doc["ip"] = getIPAddress();
    doc["bleConnected"] = bleServerCallbacks_.isConnected();
    doc["wsClients"] = ws_.count();
    doc["freeHeap"] = ESP.getFreeHeap();

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  }

  void handleGetColors(AsyncWebServerRequest *request) {
    std::vector<SavedColor> colors;
    StorageManager::instance().loadColors(colors);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (auto &c : colors) {
      JsonObject obj = arr.add<JsonObject>();
      obj["i"] = c.index;
      obj["r"] = c.r;
      obj["g"] = c.g;
      obj["b"] = c.b;
      obj["hex"] = c.hex;
      obj["ts"] = c.timestamp;
      JsonArray raw = obj["raw"].to<JsonArray>();
      for (int j = 0; j < Config::Sensor::NUM_CHANNELS; j++) {
        raw.add(c.raw[j]);
      }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  }

  void handleDownloadColorsCsv(AsyncWebServerRequest *request) {
    if (SD.exists(Config::Storage::COLORS_FILE)) {
      request->send(SD, Config::Storage::COLORS_FILE, "text/csv");
    } else {
      request->send(404, "application/json", "{\"error\":\"file not found\"}");
    }
  }

  void handleGetMeasurements(AsyncWebServerRequest *request) {
    std::vector<SavedMeasurement> measurements;
    StorageManager::instance().loadMeasurements(measurements);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    for (auto &m : measurements) {
      JsonObject obj = arr.add<JsonObject>();
      obj["i"] = m.index;
      obj["mm"] = m.value_mm;
      obj["px"] = m.value_px;
      obj["ts"] = m.timestamp;
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  }

  void handleDownloadMeasurementsCsv(AsyncWebServerRequest *request) {
    if (SD.exists(Config::Measure::DATA_FILE)) {
      request->send(SD, Config::Measure::DATA_FILE, "text/csv");
    } else {
      request->send(404, "application/json", "{\"error\":\"file not found\"}");
    }
  }

  void handlePostSettings(AsyncWebServerRequest *request) {
    if (request->hasParam("gain")) {
      int gain = request->getParam("gain")->value().toInt();
      EventQueue::send(EventType::REMOTE_SET_GAIN, gain);
    }
    if (request->hasParam("rotation")) {
      int rot = request->getParam("rotation")->value().toInt();
      EventQueue::send(EventType::REMOTE_SET_ROTATION, rot);
    }
    request->send(200, "application/json", "{\"ok\":true}");
  }

  void handlePostCalibrate(AsyncWebServerRequest *request) {
    if (request->hasParam("step")) {
      int step = request->getParam("step")->value().toInt();
      EventQueue::send(EventType::REMOTE_CALIBRATE, step);
      request->send(200, "application/json", "{\"ok\":true}");
    } else {
      request->send(400, "application/json",
                    "{\"error\":\"missing step param\"}");
    }
  }

  void handlePostWifi(AsyncWebServerRequest *request) {
    if (request->hasParam("ssid") && request->hasParam("password")) {
      String ssid = request->getParam("ssid")->value();
      String password = request->getParam("password")->value();
      setWiFiCredentials(ssid.c_str(), password.c_str());
      request->send(200, "application/json",
                    "{\"ok\":true,\"msg\":\"Restart to apply\"}");
    } else {
      request->send(400, "application/json",
                    "{\"error\":\"missing ssid/password\"}");
    }
  }

  void handlePostPin(AsyncWebServerRequest *request) {
    if (request->hasParam("newPin")) {
      String newPin = request->getParam("newPin")->value();
      if (newPin.length() >= 4 && newPin.length() <= 8) {
        setPin(newPin.c_str());
        request->send(200, "application/json", "{\"ok\":true}");
      } else {
        request->send(400, "application/json",
                      "{\"error\":\"PIN must be 4-8 chars\"}");
      }
    } else {
      request->send(400, "application/json",
                    "{\"error\":\"missing newPin\"}");
    }
  }

  // ── WebSocket ──────────────────────────────────────────────
  void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                 AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("[WS] Client #%u connected\n", client->id());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("[WS] Client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA: {
      AwsFrameInfo *info = (AwsFrameInfo *)arg;
      if (info->final && info->index == 0 && info->len == len &&
          info->opcode == WS_TEXT) {
        // Parse command from client
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data, len);
        if (!err) {
          const char *cmd = doc["cmd"];
          if (cmd) {
            if (strcmp(cmd, "measure") == 0) {
              EventQueue::send(EventType::REMOTE_MEASURE);
            } else if (strcmp(cmd, "setGain") == 0) {
              int val = doc["value"] | -1;
              if (val >= 0)
                EventQueue::send(EventType::REMOTE_SET_GAIN, val);
            } else if (strcmp(cmd, "calibrate") == 0) {
              int step = doc["step"] | -1;
              if (step >= 0)
                EventQueue::send(EventType::REMOTE_CALIBRATE, step);
            }
          }
        }
      }
    } break;
    default:
      break;
    }
  }

  void pushWebSocketData() {
    if (ws_.count() == 0 || !hasLiveData_)
      return;

    JsonDocument doc;
    doc["type"] = "live";
    JsonArray rgb = doc["rgb"].to<JsonArray>();
    rgb.add(liveData_.r);
    rgb.add(liveData_.g);
    rgb.add(liveData_.b);

    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", liveData_.r, liveData_.g,
             liveData_.b);
    doc["hex"] = hex;

    JsonArray channels = doc["ch"].to<JsonArray>();
    for (int i = 0; i < Config::Sensor::NUM_CHANNELS; i++) {
      channels.add(liveData_.calibrated[i]);
    }

    doc["x"] = liveData_.cie_X;
    doc["y"] = liveData_.cie_Y;
    doc["z"] = liveData_.cie_Z;

    String msg;
    serializeJson(doc, msg);
    ws_.textAll(msg);
  }

  // ── BLE Data Push ──────────────────────────────────────────
  void pushBLEData() {
    if (!hasLiveData_ || !bleLiveChar_)
      return;

    // Pack color data into compact JSON for Web Bluetooth
    JsonDocument doc;
    doc["r"] = liveData_.r;
    doc["g"] = liveData_.g;
    doc["b"] = liveData_.b;
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02X%02X%02X", liveData_.r, liveData_.g,
             liveData_.b);
    doc["hex"] = hex;

    JsonArray ch = doc["ch"].to<JsonArray>();
    for (int i = 0; i < Config::Sensor::NUM_CHANNELS; i++) {
      ch.add((int)(liveData_.calibrated[i] * 1000)); // Scale for integer
    }

    String msg;
    serializeJson(doc, msg);
    bleLiveChar_->setValue(msg.c_str());
    bleLiveChar_->notify();
  }

  // ── Config Persistence ─────────────────────────────────────
  void loadConfig() {
    if (!StorageManager::instance().isInitialized())
      return;

    if (!SD.exists(Config::Connectivity::CONFIG_FILE))
      return;

    File f = SD.open(Config::Connectivity::CONFIG_FILE, FILE_READ);
    if (!f)
      return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
      return;

    config_.wifiEnabled = doc["wifiEnabled"] | true;
    config_.bleEnabled = doc["bleEnabled"] | true;
    config_.wifiMode = doc["wifiMode"] | 2;

    const char *ssid = doc["wifiSsid"];
    if (ssid)
      strncpy(config_.wifiSsid, ssid, sizeof(config_.wifiSsid) - 1);

    const char *pass = doc["wifiPassword"];
    if (pass)
      strncpy(config_.wifiPassword, pass, sizeof(config_.wifiPassword) - 1);

    const char *pin = doc["pin"];
    if (pin)
      strncpy(config_.pin, pin, sizeof(config_.pin) - 1);

    Serial.println("[Conn] Config loaded from SD");
  }

  void saveConfig() {
    if (!StorageManager::instance().isInitialized()) {
      Serial.println("[Conn] SD not available, config not saved");
      return;
    }

    JsonDocument doc;
    doc["wifiEnabled"] = config_.wifiEnabled;
    doc["bleEnabled"] = config_.bleEnabled;
    doc["wifiMode"] = config_.wifiMode;
    doc["wifiSsid"] = config_.wifiSsid;
    doc["wifiPassword"] = config_.wifiPassword;
    doc["pin"] = config_.pin;

    File f = SD.open(Config::Connectivity::CONFIG_FILE, FILE_WRITE);
    if (!f)
      return;
    serializeJsonPretty(doc, f);
    f.close();
    Serial.println("[Conn] Config saved to SD");
  }

  // ── State ──────────────────────────────────────────────────
  AsyncWebServer server_;
  AsyncWebSocket ws_;

  BLEServer *bleServer_ = nullptr;
  BLECharacteristic *bleLiveChar_ = nullptr;
  BLECharacteristic *bleSavedChar_ = nullptr;
  BLECharacteristic *bleControlChar_ = nullptr;
  BLECharacteristic *bleStatusChar_ = nullptr;
  BleServerCallbacks bleServerCallbacks_;
  BleControlCallbacks bleControlCallbacks_;

  ConnectivityConfig config_;
  String sessionToken_;

  SpectralData liveData_;
  bool hasLiveData_ = false;

  bool initialized_ = false;
  bool wifiConnected_ = false;
  bool apMode_ = false;

  uint32_t lastWsPush_ = 0;
  uint32_t lastBlePush_ = 0;
};
