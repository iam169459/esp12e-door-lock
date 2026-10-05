#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <MFRC522.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <Updater.h>

// Wiring for NodeMCU / ESP-12E:
// RC522 RST -> D2 (GPIO4)
// RC522 SDA/SS -> D8 (GPIO15)
// RC522 SCK -> D5 (GPIO14)
// RC522 MOSI -> D7 (GPIO13)
// RC522 MISO -> D6 (GPIO12)
// Relay IN -> D1 (GPIO5)
constexpr uint8_t RST_PIN = D2;
constexpr uint8_t SS_PIN = D8;
constexpr uint8_t RELAY_PIN = D1;
constexpr uint8_t DOOR_SENSOR_PIN = D0;   // optional door switch
constexpr uint8_t BUZZER_PIN = D4;        // optional buzzer
constexpr uint8_t LED_PIN = D3;           // optional status LED

const char *DEFAULT_WIFI_SSID = "purple";
const char *DEFAULT_WIFI_PASSWORD = "refat123";
const char *AP_SSID = "DoorLock-Setup";
const char *AP_PASSWORD = "12345678";
const char *SETTINGS_FILE = "/settings.json";
const char *TAGS_FILE = "/tags.json";
const char *FIRMWARE_VERSION = "1.0.0";

struct DeviceSettings
{
    String wifiSSID = DEFAULT_WIFI_SSID;
    String wifiPassword = DEFAULT_WIFI_PASSWORD;
    String googleSheetUrl = "";
    bool googleLoggingEnabled = false;
    String githubFirmwareUrl = "https://github.com/your-user/your-repo/releases/latest/download/firmware.bin";
    uint16_t unlockMs = 3000;
};

DeviceSettings settings;
String authorizedTags[32];
uint8_t authorizedTagCount = 0;
String lastUid = "";
String lastStatus = "idle";
bool relayOpen = false;
bool doorOpen = false;
ESP8266WebServer server(80);
MFRC522 mfrc522(SS_PIN, RST_PIN);

String readFile(const String &path)
{
    if (!SPIFFS.exists(path))
    {
        return "";
    }

    File file = SPIFFS.open(path, "r");
    if (!file)
    {
        return "";
    }

    String value = file.readString();
    file.close();
    return value;
}

bool writeFile(const String &path, const String &value)
{
    File file = SPIFFS.open(path, "w");
    if (!file)
    {
        return false;
    }

    file.print(value);
    file.close();
    return true;
}

String uidToString(const byte *uid, byte length)
{
    String result;
    for (byte i = 0; i < length; ++i)
    {
        if (uid[i] < 0x10)
        {
            result += "0";
        }
        result += String(uid[i], HEX);
    }
    result.toUpperCase();
    return result;
}

void loadSettings()
{
    String data = readFile(SETTINGS_FILE);
    if (data.length() == 0)
    {
        settings.wifiSSID = DEFAULT_WIFI_SSID;
        settings.wifiPassword = DEFAULT_WIFI_PASSWORD;
        settings.googleSheetUrl = "";
        settings.googleLoggingEnabled = false;
        settings.githubFirmwareUrl = "https://github.com/your-user/your-repo/releases/latest/download/firmware.bin";
        settings.unlockMs = 3000;
        return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, data);
    if (err)
    {
        return;
    }

    if (doc.containsKey("wifiSSID"))
    {
        settings.wifiSSID = doc["wifiSSID"].as<String>();
    }
    if (doc.containsKey("wifiPassword"))
    {
        settings.wifiPassword = doc["wifiPassword"].as<String>();
    }
    if (doc.containsKey("googleSheetUrl"))
    {
        settings.googleSheetUrl = doc["googleSheetUrl"].as<String>();
    }
    if (doc.containsKey("googleLoggingEnabled"))
    {
        settings.googleLoggingEnabled = doc["googleLoggingEnabled"].as<bool>();
    }
    if (doc.containsKey("githubFirmwareUrl"))
    {
        settings.githubFirmwareUrl = doc["githubFirmwareUrl"].as<String>();
    }
    if (doc.containsKey("unlockMs"))
    {
        settings.unlockMs = doc["unlockMs"].as<uint16_t>();
    }
}

void saveSettings()
{
    DynamicJsonDocument doc(512);
    doc["wifiSSID"] = settings.wifiSSID;
    doc["wifiPassword"] = settings.wifiPassword;
    doc["googleSheetUrl"] = settings.googleSheetUrl;
    doc["googleLoggingEnabled"] = settings.googleLoggingEnabled;
    doc["githubFirmwareUrl"] = settings.githubFirmwareUrl;
    doc["unlockMs"] = settings.unlockMs;

    String out;
    serializeJson(doc, out);
    writeFile(SETTINGS_FILE, out);
}

void loadAuthorizedTags()
{
    String data = readFile(TAGS_FILE);
    authorizedTagCount = 0;
    if (data.length() == 0)
    {
        return;
    }

    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, data);
    if (err)
    {
        return;
    }

    if (!doc.is<JsonArray>())
    {
        return;
    }

    JsonArray array = doc.as<JsonArray>();
    for (JsonVariant value : array)
    {
        if (authorizedTagCount < 32)
        {
            authorizedTags[authorizedTagCount++] = value.as<String>();
        }
    }
}

void saveAuthorizedTags()
{
    DynamicJsonDocument doc(512);
    JsonArray array = doc.to<JsonArray>();
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        array.add(authorizedTags[i]);
    }

    String out;
    serializeJson(doc, out);
    writeFile(TAGS_FILE, out);
}

bool isAuthorized(const String &uid)
{
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].equalsIgnoreCase(uid))
        {
            return true;
        }
    }
    return false;
}

bool addAuthorizedTag(const String &uid)
{
    if (uid.length() == 0)
    {
        return false;
    }

    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].equalsIgnoreCase(uid))
        {
            return false;
        }
    }

    if (authorizedTagCount >= 32)
    {
        return false;
    }

    authorizedTags[authorizedTagCount++] = uid;
    saveAuthorizedTags();
    return true;
}

bool removeAuthorizedTag(const String &uid)
{
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].equalsIgnoreCase(uid))
        {
            for (uint8_t j = i; j < authorizedTagCount - 1; ++j)
            {
                authorizedTags[j] = authorizedTags[j + 1];
            }
            authorizedTagCount--;
            saveAuthorizedTags();
            return true;
        }
    }
    return false;
}

void setRelay(bool open)
{
    relayOpen = open;
    digitalWrite(RELAY_PIN, open ? LOW : HIGH);
    digitalWrite(LED_PIN, open ? HIGH : LOW);
}

void unlockDoor()
{
    setRelay(true);
    lastStatus = "unlocked";
    delay(settings.unlockMs);
    setRelay(false);
    lastStatus = "locked";
}

bool postToGoogleSheet(const String &eventType, const String &uid, const String &note)
{
    if (!settings.googleLoggingEnabled || settings.googleSheetUrl.length() < 10)
    {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, settings.googleSheetUrl))
    {
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    String payload = "{\"event\":\"" + eventType + "\",\"uid\":\"" + uid + "\",\"note\":\"" + note + "\",\"time\":\"" + String(millis()) + "\"}";

    int httpCode = http.POST(payload);
    http.end();
    return httpCode >= 200 && httpCode < 300;
}

bool installFirmwareFromUrl(const String &url)
{
    if (!url.startsWith("http"))
    {
        return false;
    }

    HTTPClient http;
    WiFiClient client;
    if (!http.begin(client, url))
    {
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    size_t length = http.getSize();
    if (length == 0)
    {
        http.end();
        return false;
    }

    if (!Update.begin(length))
    {
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != length)
    {
        Update.end(false);
        return false;
    }

    if (!Update.end(true))
    {
        return false;
    }

    return true;
}

String buildStatusJson()
{
    DynamicJsonDocument doc(512);
    doc["status"] = lastStatus;
    doc["relayOpen"] = relayOpen;
    doc["doorOpen"] = doorOpen;
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["tagCount"] = authorizedTagCount;
    doc["lastUid"] = lastUid;
    JsonArray tags = doc.createNestedArray("tags");
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        tags.add(authorizedTags[i]);
    }
    String output;
    serializeJson(doc, output);
    return output;
}

String buildWifiScanJson()
{
    DynamicJsonDocument doc(1024);
    JsonArray networks = doc.to<JsonArray>();
    int found = WiFi.scanNetworks(false, true);
    for (int i = 0; i < found; ++i)
    {
        JsonObject item = networks.createNestedObject();
        item["ssid"] = WiFi.SSID(i);
        item["rssi"] = WiFi.RSSI(i);
        item["enc"] = WiFi.encryptionType(i) == ENC_TYPE_NONE ? "OPEN" : "SECURED";
    }
    String output;
    serializeJson(networks, output);
    return output;
}

bool readCardUidIfPresent(String &uidOut)
{
    if (!mfrc522.PICC_IsNewCardPresent())
    {
        return false;
    }

    if (!mfrc522.PICC_ReadCardSerial())
    {
        return false;
    }

    uidOut = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return true;
}

void handleRoot()
{
    String page = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>ESP12E Door Lock</title>
  <style>
    body { font-family: Arial, sans-serif; background: #111827; color: #f3f4f6; margin: 0; padding: 20px; }
    .wrap { max-width: 1000px; margin: 0 auto; }
    .card { background: #1f2937; border-radius: 12px; padding: 16px; margin-bottom: 20px; box-shadow: 0 10px 25px rgba(0,0,0,0.2); }
    h1, h2, h3 { margin-top: 0; }
    label { display: block; margin: 8px 0 6px; }
    input, button, select { width: 100%; box-sizing: border-box; padding: 10px; border-radius: 8px; border: none; margin-bottom: 10px; }
    button { background: #22c55e; color: white; font-weight: bold; cursor: pointer; }
    button.secondary { background: #3b82f6; }
    button.warn { background: #ef4444; }
    .tabs { display: flex; gap: 8px; flex-wrap: wrap; margin-bottom: 18px; }
    .tab { background: #374151; color: white; border-radius: 8px; padding: 10px 16px; cursor: pointer; }
    .tab.active { background: #22c55e; }
    .hidden { display: none; }
    .status { display: flex; justify-content: space-between; flex-wrap: wrap; gap: 12px; }
    table { width: 100%; border-collapse: collapse; }
    th, td { text-align: left; padding: 8px; border-bottom: 1px solid #374151; }
    .scanResult { margin-top: 8px; font-weight: bold; }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="tabs">
      <div class="tab active" onclick="showTab('dashboard')">Dashboard</div>
      <div class="tab" onclick="showTab('wifi')">Wi-Fi Scan</div>
      <div class="tab" onclick="showTab('cards')">Card Manager</div>
      <div class="tab" onclick="showTab('settings')">Settings</div>
      <div class="tab" onclick="showTab('ota')">OTA</div>
    </div>

    <div id="dashboard" class="card">
      <h1>Door Lock Control</h1>
      <div class="status">
        <div><strong>Door:</strong> <span id="doorState">--</span></div>
        <div><strong>Relay:</strong> <span id="relayState">--</span></div>
        <div><strong>IP:</strong> <span id="ipAddr">--</span></div>
        <div><strong>Last UID:</strong> <span id="lastUid">--</span></div>
      </div>
      <br>
      <button onclick="unlockDoor()">Unlock Door</button>
      <button class="secondary" onclick="refreshStatus()">Refresh</button>
      <button class="warn" onclick="restartDevice()">Restart</button>
    </div>

    <div id="wifi" class="card hidden">
      <h2>Wi-Fi Scan</h2>
      <button class="secondary" onclick="wifiScan()">Scan Wi-Fi Networks</button>
      <table>
        <thead><tr><th>SSID</th><th>RSSI</th><th>Type</th></tr></thead>
        <tbody id="wifiTable"></tbody>
      </table>
    </div>

    <div id="cards" class="card hidden">
      <h2>Card Manager</h2>
      <button class="secondary" onclick="scanAndAddCard()">Scan Card and Add</button>
      <div id="scanStatus" class="scanResult">Waiting for card...</div>
      <form method="POST" action="/api/add-tag">
        <label>RFID UID</label>
        <input name="uid" id="manualUid" placeholder="e.g. 01A2B3C4" />
        <button type="submit">Add Card Manually</button>
      </form>
      <table>
        <thead><tr><th>UID</th><th>Action</th></tr></thead>
        <tbody id="tagTable"></tbody>
      </table>
    </div>

    <div id="settings" class="card hidden">
      <h2>Wi-Fi Setup</h2>
      <form method="POST" action="/api/settings">
        <label>SSID</label>
        <input name="wifiSsid" value="purple" />
        <label>Password</label>
        <input name="wifiPassword" type="password" value="refat123" />
        <label>Unlock time (ms)</label>
        <input name="unlockMs" type="number" value="3000" />
        <label>GitHub OTA URL</label>
        <input name="githubFirmwareUrl" type="url" value="https://github.com/your-user/your-repo/releases/latest/download/firmware.bin" />
        <button type="submit">Save Settings</button>
      </form>

      <h3>Google Sheets Logging</h3>
      <form method="POST" action="/api/settings">
        <label>Google Apps Script URL</label>
        <input name="googleSheetUrl" type="url" placeholder="https://script.google.com/.../exec" />
        <label><input name="googleLoggingEnabled" type="checkbox" /> Enable Google logging</label>
        <button type="submit">Save Logging</button>
      </form>
    </div>

    <div id="ota" class="card hidden">
      <h2>GitHub OTA</h2>
      <form method="POST" action="/api/ota">
        <label>Firmware URL</label>
        <input name="url" type="url" placeholder="https://github.com/.../releases/latest/download/firmware.bin" />
        <button type="submit">Update Firmware</button>
      </form>
    </div>
  </div>

  <script>
    function showTab(tabName) {
      const tabs = document.querySelectorAll('.tab');
      tabs.forEach(tab => tab.classList.remove('active'));
      const activeTab = Array.from(tabs).find(tab => tab.textContent.trim() === tabName || tab.onclick && tab.onclick.toString().includes(tabName));
      if (activeTab) activeTab.classList.add('active');

      document.getElementById('dashboard').classList.add('hidden');
      document.getElementById('wifi').classList.add('hidden');
      document.getElementById('cards').classList.add('hidden');
      document.getElementById('settings').classList.add('hidden');
      document.getElementById('ota').classList.add('hidden');
      document.getElementById(tabName).classList.remove('hidden');
    }

    async function refreshStatus() {
      const res = await fetch('/api/status');
      const data = await res.json();
      document.getElementById('doorState').textContent = data.doorOpen ? 'Open' : 'Closed';
      document.getElementById('relayState').textContent = data.relayOpen ? 'Open' : 'Closed';
      document.getElementById('ipAddr').textContent = data.ip || '--';
      document.getElementById('lastUid').textContent = data.lastUid || '--';
      const rows = data.tags.map(tag => '<tr><td>' + tag + '</td><td><button class="warn" onclick="removeTag(\'' + tag + '\')">Remove</button></td></tr>').join('');
      document.getElementById('tagTable').innerHTML = rows;
    }

    async function wifiScan() {
      const res = await fetch('/api/wifi-scan');
      const data = await res.json();
      const rows = data.map(net => '<tr><td>' + net.ssid + '</td><td>' + net.rssi + '</td><td>' + net.enc + '</td></tr>').join('');
      document.getElementById('wifiTable').innerHTML = rows || '<tr><td colspan="3">No networks found</td></tr>';
    }

    async function scanAndAddCard() {
      const statusEl = document.getElementById('scanStatus');
      statusEl.textContent = 'Hold card near reader...';
      const res = await fetch('/api/scan-tag');
      const data = await res.json();
      if (data.ok) {
        document.getElementById('manualUid').value = data.uid;
        statusEl.textContent = 'Card detected: ' + data.uid;
        await fetch('/api/add-tag', { method: 'POST', body: new URLSearchParams({ uid: data.uid }) });
        refreshStatus();
      } else {
        statusEl.textContent = 'No card detected';
      }
    }

    async function unlockDoor() {
      await fetch('/api/unlock', { method: 'POST' });
      refreshStatus();
    }

    async function removeTag(uid) {
      const formData = new FormData();
      formData.append('uid', uid);
      await fetch('/api/remove-tag', { method: 'POST', body: formData });
      refreshStatus();
    }

    async function restartDevice() {
      await fetch('/api/restart', { method: 'POST' });
    }
    refreshStatus();
    wifiScan();
  </script>
</body>
</html>
)HTML";
    server.send(200, "text/html", page);
}

void handleStatus()
{
    String response = buildStatusJson();
    server.send(200, "application/json", response);
}

void handleSettings()
{
    if (server.hasArg("wifiSsid"))
    {
        settings.wifiSSID = server.arg("wifiSsid");
    }
    if (server.hasArg("wifiPassword"))
    {
        settings.wifiPassword = server.arg("wifiPassword");
    }
    if (server.hasArg("googleSheetUrl"))
    {
        settings.googleSheetUrl = server.arg("googleSheetUrl");
    }
    if (server.hasArg("githubFirmwareUrl"))
    {
        settings.githubFirmwareUrl = server.arg("githubFirmwareUrl");
    }
    if (server.hasArg("unlockMs"))
    {
        settings.unlockMs = server.arg("unlockMs").toInt();
    }
    settings.googleLoggingEnabled = server.hasArg("googleLoggingEnabled");
    saveSettings();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Saved");
}

void handleAddTag()
{
    String uid = server.arg("uid");
    uid.trim();
    uid.toUpperCase();
    if (uid.length() > 0)
    {
        addAuthorizedTag(uid);
    }
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Added tag");
}

void handleRemoveTag()
{
    String uid = server.arg("uid");
    uid.trim();
    uid.toUpperCase();
    removeAuthorizedTag(uid);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleUnlock()
{
    unlockDoor();
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleRestart()
{
    server.send(200, "text/plain", "Restarting");
    delay(200);
    ESP.restart();
}

void handleOta()
{
    String url = server.hasArg("url") ? server.arg("url") : settings.githubFirmwareUrl;
    url.trim();
    if (url.length() == 0)
    {
        server.send(400, "text/plain", "Missing URL");
        return;
    }

    settings.githubFirmwareUrl = url;
    saveSettings();

    bool success = installFirmwareFromUrl(url);
    if (success)
    {
        server.send(200, "text/plain", "Firmware updated. Rebooting...");
        delay(2000);
        ESP.restart();
        return;
    }

    server.send(500, "text/plain", "OTA failed");
}

void connectToWifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(settings.wifiSSID.c_str(), settings.wifiPassword.c_str());

    for (int i = 0; i < 40; ++i)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            return;
        }
        delay(250);
    }

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
}

void setupWifi()
{
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    connectToWifi();
}

void handleRfidRead()
{
    if (!mfrc522.PICC_IsNewCardPresent())
    {
        return;
    }

    if (!mfrc522.PICC_ReadCardSerial())
    {
        return;
    }

    String uid = uidToString(mfrc522.uid.uidByte, mfrc522.uid.size);
    lastUid = uid;
    bool allowed = isAuthorized(uid);

    if (allowed)
    {
        lastStatus = "access granted";
        tone(BUZZER_PIN, 2000, 200);
        unlockDoor();
        postToGoogleSheet("access", uid, "granted");
        Serial.printf("Access granted: %s\n", uid.c_str());
    }
    else
    {
        lastStatus = "access denied";
        tone(BUZZER_PIN, 500, 300);
        postToGoogleSheet("access", uid, "denied");
        Serial.printf("Access denied: %s\n", uid.c_str());
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
}

void setup()
{
    pinMode(RELAY_PIN, OUTPUT);
    pinMode(DOOR_SENSOR_PIN, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);

    setRelay(false);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(115200);
    SPI.begin();
    pinMode(SS_PIN, OUTPUT);
    digitalWrite(SS_PIN, HIGH);
    mfrc522.PCD_Init();

    if (!SPIFFS.begin())
    {
        SPIFFS.format();
    }

    loadSettings();
    loadAuthorizedTags();
    setupWifi();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/wifi-scan", HTTP_GET, []() {
        server.send(200, "application/json", buildWifiScanJson());
    });
    server.on("/api/scan-tag", HTTP_GET, []() {
        String uid;
        if (!readCardUidIfPresent(uid))
        {
            unsigned long start = millis();
            while (millis() - start < 15000)
            {
                if (readCardUidIfPresent(uid))
                {
                    break;
                }
                delay(50);
            }
        }

        if (uid.length() == 0)
        {
            server.send(408, "application/json", "{\"ok\":false,\"error\":\"No card detected\"}");
            return;
        }

        server.send(200, "application/json", "{\"ok\":true,\"uid\":\"" + uid + "\"}");
    });
    server.on("/api/settings", HTTP_POST, handleSettings);
    server.on("/api/add-tag", HTTP_POST, handleAddTag);
    server.on("/api/remove-tag", HTTP_POST, handleRemoveTag);
    server.on("/api/unlock", HTTP_POST, handleUnlock);
    server.on("/api/restart", HTTP_POST, handleRestart);
    server.on("/api/ota", HTTP_POST, handleOta);
    server.begin();

    Serial.println("Door lock ready");
    Serial.print("WiFi: ");
    Serial.println(settings.wifiSSID);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

void loop()
{
    server.handleClient();
    handleRfidRead();

    bool sensorState = digitalRead(DOOR_SENSOR_PIN) == LOW;
    if (sensorState != doorOpen)
    {
        doorOpen = sensorState;
        lastStatus = doorOpen ? "door open" : "door closed";
        postToGoogleSheet("door", lastUid, doorOpen ? "open" : "closed");
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        connectToWifi();
    }

    delay(50);
}
