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
#ifndef APP_VERSION
#define APP_VERSION "1.0.0"
#endif
const char *FIRMWARE_VERSION = APP_VERSION;
const char *REPO_OTA_URL = "https://github.com/iam169459/esp12e-door-lock/releases/latest/download/firmware.bin";
const char *GITHUB_RELEASE_API = "https://api.github.com/repos/iam169459/esp12e-door-lock/releases/latest";
constexpr unsigned long RFID_REPEAT_GUARD_MS = 750;
constexpr unsigned long RFID_SCAN_POLL_MS = 10;
constexpr unsigned long RELEASE_CHECK_INTERVAL_MS = 15UL * 60UL * 1000UL;

struct DeviceSettings
{
    String wifiSSID = DEFAULT_WIFI_SSID;
    String wifiPassword = DEFAULT_WIFI_PASSWORD;
    String googleSheetUrl = "";
    bool googleLoggingEnabled = false;
    uint16_t unlockMs = 3000;
};

DeviceSettings settings;
struct AuthorizedTag
{
    String uid;
    String holderName;
};

AuthorizedTag authorizedTags[32];
uint8_t authorizedTagCount = 0;
String lastUid = "";
String lastStatus = "idle";
String lastProcessedRfidUid = "";
unsigned long lastProcessedRfidAt = 0;
bool relayOpen = false;
bool hasUnlockedSinceBoot = false;
unsigned long relayActivatedAt = 0;
bool doorOpen = false;
bool updateCheckCached = false;
bool updateAvailable = false;
unsigned long lastUpdateCheckAt = 0;
String latestFirmwareVersion = "";
String latestReleasePage = "";
String otaFailureReason = "";
int lastSheetsHttpCode = 0;
String lastSheetsError = "";
ESP8266WebServer server(80);
MFRC522 mfrc522(SS_PIN, RST_PIN);
void handleSheetsTest();

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
        settings.unlockMs = 3000;
        return;
    }

    DynamicJsonDocument doc(3072);
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
        if (authorizedTagCount >= 32)
        {
            break;
        }

        String uid;
        String holderName;
        if (value.is<JsonObject>())
        {
            JsonObject tag = value.as<JsonObject>();
            uid = tag["uid"] | "";
            holderName = tag["name"] | "";
        }
        else
        {
            uid = value.as<String>();
        }

        uid.trim();
        uid.toUpperCase();
        holderName.trim();
        if (holderName.length() > 32)
        {
            holderName = holderName.substring(0, 32);
        }
        if (uid.length() > 0)
        {
            authorizedTags[authorizedTagCount].uid = uid;
            authorizedTags[authorizedTagCount].holderName = holderName;
            authorizedTagCount++;
        }
    }
}

void saveAuthorizedTags()
{
    DynamicJsonDocument doc(3072);
    JsonArray array = doc.to<JsonArray>();
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        JsonObject tag = array.createNestedObject();
        tag["uid"] = authorizedTags[i].uid;
        tag["name"] = authorizedTags[i].holderName;
    }

    String out;
    serializeJson(doc, out);
    writeFile(TAGS_FILE, out);
}

bool isAuthorized(const String &uid)
{
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].uid.equalsIgnoreCase(uid))
        {
            return true;
        }
    }
    return false;
}

String holderNameFor(const String &uid)
{
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].uid.equalsIgnoreCase(uid))
        {
            return authorizedTags[i].holderName.length() > 0 ? authorizedTags[i].holderName : "Unassigned";
        }
    }
    return "Unassigned";
}

bool addAuthorizedTag(const String &uid, String holderName)
{
    String normalizedUid = uid;
    normalizedUid.trim();
    normalizedUid.toUpperCase();
    holderName.trim();
    if (normalizedUid.length() == 0 || holderName.length() == 0)
    {
        return false;
    }
    if (holderName.length() > 32)
    {
        holderName = holderName.substring(0, 32);
    }

    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].uid.equalsIgnoreCase(normalizedUid))
        {
            authorizedTags[i].holderName = holderName;
            saveAuthorizedTags();
            return true;
        }
    }

    if (authorizedTagCount >= 32)
    {
        return false;
    }

    authorizedTags[authorizedTagCount].uid = normalizedUid;
    authorizedTags[authorizedTagCount].holderName = holderName;
    authorizedTagCount++;
    saveAuthorizedTags();
    return true;
}

bool removeAuthorizedTag(const String &uid)
{
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        if (authorizedTags[i].uid.equalsIgnoreCase(uid))
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
    if (relayOpen)
    {
        return;
    }

    setRelay(true);
    relayActivatedAt = millis();
    hasUnlockedSinceBoot = true;
    lastStatus = "unlocked";
}

void updateRelay()
{
    if (relayOpen && millis() - relayActivatedAt >= settings.unlockMs)
    {
    setRelay(false);
    lastStatus = "locked";
    }
}

bool postToGoogleSheet(const String &eventType, const String &uid, const String &note, bool testRequest = false)
{
    lastSheetsHttpCode = 0;
    lastSheetsError = "";
    if ((!settings.googleLoggingEnabled && !testRequest) || settings.googleSheetUrl.length() < 10)
    {
        lastSheetsError = "Logging is disabled or the Apps Script URL is missing";
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(testRequest ? 20000 : 4000);
    if (!http.begin(client, settings.googleSheetUrl))
    {
        lastSheetsError = "Could not open the Apps Script URL";
        return false;
    }

    http.addHeader("Content-Type", "application/json");

    String payload = "{\"event\":\"" + eventType + "\",\"uid\":\"" + uid + "\",\"note\":\"" + note + "\",\"time\":\"" + String(millis()) + "\"}";

    int httpCode = http.POST(payload);
    lastSheetsHttpCode = httpCode;
    if (httpCode >= 200 && httpCode < 300)
    {
        http.end();
        return true;
    }

    String redirectLocation = http.getLocation();
    if (httpCode >= 300 && httpCode < 400 && redirectLocation.length() > 0)
    {
        if (redirectLocation.startsWith("https://script.googleusercontent.com/"))
        {
            http.end();
            return true;
        }

        http.end();
        WiFiClientSecure redirectClient;
        redirectClient.setInsecure();
        HTTPClient redirect;
        redirect.setTimeout(testRequest ? 20000 : 4000);
        redirect.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!redirect.begin(redirectClient, redirectLocation))
        {
            lastSheetsError = "Could not open the Apps Script response URL";
            return false;
        }

        int redirectCode = redirect.GET();
        lastSheetsHttpCode = redirectCode;
        bool succeeded = redirectCode >= 200 && redirectCode < 300;
        if (!succeeded)
        {
            lastSheetsError = "Apps Script response HTTP " + String(redirectCode) + ": " + redirect.errorToString(redirectCode);
        }
        redirect.end();
        return succeeded;
    }

    lastSheetsError = "Apps Script HTTP " + String(httpCode) + ": " + http.errorToString(httpCode);
    http.end();
    return false;
}

bool installFirmwareFromUrl(const String &url)
{
    otaFailureReason = "";
    if (!url.startsWith("http"))
    {
        otaFailureReason = "Invalid firmware URL";
        return false;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, url))
    {
        otaFailureReason = "Could not open firmware URL";
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        otaFailureReason = "Firmware download HTTP " + String(code) + ": " + http.errorToString(code);
        http.end();
        return false;
    }

    size_t length = http.getSize();
    if (length == 0 || length == static_cast<size_t>(-1))
    {
        otaFailureReason = "Firmware download has no valid content length";
        http.end();
        return false;
    }

    if (!Update.begin(length))
    {
        otaFailureReason = "Updater begin failed: " + Update.getErrorString();
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    unsigned long streamWaitStarted = millis();
    while (!stream->available() && stream->connected() && millis() - streamWaitStarted < 10000UL)
    {
        delay(10);
        yield();
    }
    if (!stream->available())
    {
        otaFailureReason = "Firmware stream did not become readable";
        Update.end(false);
        http.end();
        return false;
    }

    size_t written = Update.writeStream(*stream);
    http.end();

    if (written != length)
    {
        otaFailureReason = "Firmware stream incomplete: " + String(written) + "/" + String(length) + "; " + Update.getErrorString();
        Update.end(false);
        return false;
    }

    if (!Update.end(true))
    {
        otaFailureReason = "Updater finalize failed: " + Update.getErrorString();
        return false;
    }

    return true;
}

bool fetchLatestRelease()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(client, GITHUB_RELEASE_API))
    {
        return false;
    }

    http.addHeader("Accept", "application/vnd.github+json");
    http.addHeader("User-Agent", "ESP12E-Door-Lock");
    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    DynamicJsonDocument filter(128);
    filter["tag_name"] = true;
    filter["html_url"] = true;
    DynamicJsonDocument release(384);
    DeserializationError error = deserializeJson(release, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (error || !release["tag_name"].is<const char *>())
    {
        return false;
    }

    latestFirmwareVersion = release["tag_name"].as<String>();
    latestReleasePage = release["html_url"].as<String>();
    String normalizedVersion = latestFirmwareVersion;
    if (normalizedVersion.startsWith("v") || normalizedVersion.startsWith("V"))
    {
        normalizedVersion.remove(0, 1);
    }
    updateAvailable = normalizedVersion != FIRMWARE_VERSION;
    lastUpdateCheckAt = millis();
    updateCheckCached = true;
    return true;
}

void handleUpdateCheck()
{
    bool forceRefresh = server.hasArg("refresh") && server.arg("refresh") == "1";
    if (forceRefresh || !updateCheckCached || millis() - lastUpdateCheckAt >= RELEASE_CHECK_INTERVAL_MS)
    {
        if (!fetchLatestRelease())
        {
            server.send(503, "application/json", "{\"ok\":false,\"error\":\"Unable to check GitHub releases\"}");
            return;
        }
    }

    DynamicJsonDocument response(384);
    response["ok"] = true;
    response["currentVersion"] = FIRMWARE_VERSION;
    response["latestVersion"] = latestFirmwareVersion;
    response["updateAvailable"] = updateAvailable;
    response["releaseUrl"] = latestReleasePage;
    String output;
    serializeJson(response, output);
    server.send(200, "application/json", output);
}

String buildStatusJson()
{
    DynamicJsonDocument doc(3072);
    doc["status"] = lastStatus;
    doc["relayOpen"] = relayOpen;
    doc["doorOpen"] = doorOpen;
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    unsigned long elapsedSinceUnlock = millis() - relayActivatedAt;
    unsigned long unlockCooldownMs = settings.unlockMs + 1000UL;
    doc["unlockCooldownMs"] = hasUnlockedSinceBoot && elapsedSinceUnlock < unlockCooldownMs ? unlockCooldownMs - elapsedSinceUnlock : 0;
    doc["tagCount"] = authorizedTagCount;
    doc["lastUid"] = lastUid;
    JsonArray tags = doc.createNestedArray("tags");
    for (uint8_t i = 0; i < authorizedTagCount; ++i)
    {
        JsonObject tag = tags.createNestedObject();
        tag["uid"] = authorizedTags[i].uid;
        tag["name"] = authorizedTags[i].holderName;
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

String escapeHtmlAttribute(const String &value)
{
    String escaped = value;
    escaped.replace("&", "&amp;");
    escaped.replace("\"", "&quot;");
    escaped.replace("'", "&#39;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    return escaped;
}

void handleRoot()
{
    static const char page[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>ESP12E Door Lock</title>
  <style>
        :root { color-scheme: dark; --bg: #111916; --surface: #1b2521; --surface-raised: #26332d; --text: #f3f6f2; --muted: #a6b5aa; --line: #39483f; --green: #70d1a2; --amber: #efb263; --red: #ed766e; }
        * { box-sizing: border-box; }
        body { font-family: "Avenir Next", "Trebuchet MS", sans-serif; background: linear-gradient(145deg, #101714, #17201e 58%, #171d25); color: var(--text); margin: 0; min-height: 100vh; padding: 24px; }
        .wrap { max-width: 920px; margin: 0 auto; }
        .topbar { display: flex; align-items: center; justify-content: space-between; gap: 16px; margin-bottom: 22px; }
        .brand { display: flex; align-items: center; gap: 12px; }
        .brand-mark { display: grid; place-items: center; width: 42px; aspect-ratio: 1; border-radius: 8px; background: var(--green); color: #10251b; font-weight: 800; }
        .eyebrow { color: var(--green); font-size: 11px; font-weight: 700; margin: 0 0 4px; }
        h1, h2, h3, p { margin-top: 0; }
        h1 { font-size: 24px; margin-bottom: 0; }
        h2 { font-size: 20px; margin-bottom: 16px; }
        h3 { font-size: 16px; margin: 28px 0 10px; }
        .connection { color: var(--muted); font-size: 13px; }
        .connection::before { content: ""; display: inline-block; width: 8px; height: 8px; margin-right: 8px; border-radius: 50%; background: var(--amber); }
        .connection.online::before { background: var(--green); }
        .tabs { display: flex; gap: 4px; overflow-x: auto; margin-bottom: 16px; border-bottom: 1px solid var(--line); }
        .tab { flex: 0 0 auto; border: 0; border-bottom: 2px solid transparent; background: transparent; color: var(--muted); padding: 12px 15px; font: inherit; cursor: pointer; }
        .tab.active { color: var(--text); border-bottom-color: var(--green); }
        .card { background: var(--surface); border: 1px solid var(--line); border-radius: 8px; padding: 22px; }
        .hidden { display: none !important; }
        .status { display: grid; grid-template-columns: repeat(auto-fit, minmax(145px, 1fr)); gap: 10px; margin-bottom: 20px; }
        .metric { padding: 12px; background: var(--surface-raised); border-radius: 6px; }
        .metric strong { display: block; color: var(--muted); font-size: 12px; font-weight: 600; margin-bottom: 6px; }
        .metric span { overflow-wrap: anywhere; }
        .actions { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
        button { min-height: 42px; border: 1px solid transparent; border-radius: 6px; padding: 10px 14px; background: var(--green); color: #10251b; font: inherit; font-weight: 700; cursor: pointer; }
        button.secondary { background: var(--surface-raised); color: var(--text); border-color: var(--line); }
        button.warn { background: transparent; border-color: var(--red); color: var(--red); }
        button:disabled { opacity: .55; cursor: not-allowed; }
        .actions button { width: auto; margin: 0; }
        .feedback, .muted { color: var(--muted); font-size: 13px; }
        .feedback { min-height: 20px; margin: 12px 0 0; }
        label { display: block; margin: 12px 0 6px; color: var(--muted); font-size: 13px; }
        input { width: 100%; min-height: 42px; border: 1px solid var(--line); border-radius: 6px; padding: 10px 12px; margin-bottom: 8px; background: #111916; color: var(--text); font: inherit; }
        input[type=checkbox] { width: 18px; min-height: 18px; vertical-align: middle; accent-color: var(--green); }
        input[readonly] { color: var(--muted); }
        form { max-width: 560px; }
        .check-row { display: flex; align-items: center; gap: 8px; }
        .check-row label { display: inline; margin: 0; }
        table { width: 100%; border-collapse: collapse; margin-top: 14px; }
        th, td { text-align: left; padding: 10px 8px; border-bottom: 1px solid var(--line); }
        th { color: var(--muted); font-size: 12px; font-weight: 600; }
        td button { min-height: 34px; padding: 6px 10px; }
        .row-actions { display: flex; flex-wrap: wrap; gap: 6px; }
        .scanResult { margin: 12px 0; color: var(--muted); }
        .update-panel { padding: 14px; margin: 14px 0; border: 1px solid var(--line); border-radius: 6px; background: var(--surface-raised); }
        .update-panel.available { border-color: var(--amber); }
        .update-panel p { margin-bottom: 10px; }
        a { color: var(--green); }
        @media (max-width: 560px) { body { padding: 14px; } .topbar { align-items: flex-start; } .card { padding: 16px; } .tabs { margin-left: -4px; margin-right: -4px; } .actions { align-items: stretch; } .actions button { flex: 1 1 100%; } table { font-size: 13px; } }
  </style>
</head>
<body>
  <div class="wrap">
        <header class="topbar">
            <div class="brand"><div class="brand-mark" aria-hidden="true">DL</div><div><p class="eyebrow">ACCESS CONTROL</p><h1>Door Lock</h1></div></div>
            <div id="connectionState" class="connection">Connecting</div>
        </header>
        <nav class="tabs" aria-label="Main navigation">
            <button class="tab active" type="button" data-tab="dashboard" aria-selected="true" onclick="showTab('dashboard', this)">Dashboard</button>
            <button class="tab" type="button" data-tab="wifi" aria-selected="false" onclick="showTab('wifi', this)">Wi-Fi</button>
            <button class="tab" type="button" data-tab="cards" aria-selected="false" onclick="showTab('cards', this)">Cards</button>
            <button class="tab" type="button" data-tab="settings" aria-selected="false" onclick="showTab('settings', this)">Settings</button>
            <button class="tab" type="button" data-tab="ota" aria-selected="false" onclick="showTab('ota', this)">Firmware</button>
        </nav>

        <section id="dashboard" class="card">
            <p class="eyebrow">OVERVIEW</p>
            <h2>Lock status</h2>
            <div class="status">
                <div class="metric"><strong>Door</strong><span id="doorState">--</span></div>
                <div class="metric"><strong>Relay</strong><span id="relayState">--</span></div>
                <div class="metric"><strong>Device address</strong><span id="ipAddr">--</span></div>
                <div class="metric"><strong>Last card</strong><span id="lastUid">--</span></div>
            </div>
            <div class="actions">
                <button id="unlockButton" type="button" onclick="unlockDoor()">Unlock door</button>
                <button class="secondary" type="button" onclick="refreshStatus()">Refresh status</button>
                <button class="warn" type="button" onclick="restartDevice()">Restart device</button>
            </div>
            <p id="unlockFeedback" class="feedback" role="status" aria-live="polite">Ready.</p>
        </section>

        <section id="wifi" class="card hidden">
            <h2>Nearby Wi-Fi</h2>
            <div class="actions"><button id="wifiScanButton" class="secondary" type="button" onclick="wifiScan()">Scan networks</button></div>
            <table aria-label="Available Wi-Fi networks">
                <thead><tr><th>Network</th><th>Signal</th><th>Security</th></tr></thead>
                <tbody id="wifiTable"><tr><td colspan="3">Select scan to search.</td></tr></tbody>
            </table>
        </section>

        <section id="cards" class="card hidden">
            <h2>Card manager</h2>
            <form id="manualCardForm" data-feedback="manualCardFeedback" method="POST" action="/api/add-tag">
                <label for="cardHolderName">Card holder name</label>
                <input id="cardHolderName" name="name" maxlength="32" placeholder="For example, User 1" required />
            <div class="actions"><button id="scanCardButton" class="secondary" type="button" onclick="scanAndAddCard()">Scan and add card</button></div>
            <p id="scanStatus" class="scanResult" role="status" aria-live="polite">Waiting for scan.</p>
                <label for="manualUid">Add a card UID manually</label>
                <input name="uid" id="manualUid" placeholder="For example, 01A2B3C4" required />
                <button type="submit">Add or rename card</button>
                <p id="manualCardFeedback" class="feedback" role="status" aria-live="polite"></p>
            </form>
            <table aria-label="Authorized cards">
                <thead><tr><th>Card holder</th><th>Card UID</th><th>Actions</th></tr></thead>
                <tbody id="tagTable"></tbody>
            </table>
        </section>

        <section id="settings" class="card hidden">
            <h2>Device settings</h2>
            <form id="deviceSettingsForm" data-feedback="deviceSettingsFeedback" method="POST" action="/api/settings">
                <label for="wifiSsid">Wi-Fi network</label>
                <input id="wifiSsid" name="wifiSsid" required />
                <label for="wifiPassword">New Wi-Fi password</label>
                <input id="wifiPassword" name="wifiPassword" type="password" autocomplete="new-password" placeholder="Leave blank to keep current password" />
                <label for="unlockMs">Unlock duration (milliseconds)</label>
                <input id="unlockMs" name="unlockMs" type="number" min="500" max="30000" value="3000" required />
                <button type="submit">Save device settings</button>
                <p id="deviceSettingsFeedback" class="feedback" role="status" aria-live="polite"></p>
            </form>

            <h3>Google Sheets logging</h3>
            <form id="sheetsSettingsForm" data-feedback="sheetsSettingsFeedback" method="POST" action="/api/settings">
                <label for="googleSheetUrl">Apps Script URL</label>
                <input id="googleSheetUrl" name="googleSheetUrl" type="url" placeholder="https://script.google.com/.../exec" />
                <div class="check-row"><input id="googleLoggingEnabled" name="googleLoggingEnabled" type="checkbox" /><label for="googleLoggingEnabled">Enable access logging</label></div>
                <button type="submit">Save logging settings</button>
                <p id="sheetsSettingsFeedback" class="feedback" role="status" aria-live="polite"></p>
                <div class="actions"><button class="secondary" type="button" onclick="testSheetsConnection()">Test connection</button></div>
                <p id="sheetsTestStatus" class="feedback" role="status" aria-live="polite"></p>
            </form>
        </section>

        <section id="ota" class="card hidden">
            <h2>Firmware updates</h2>
            <p class="muted">Installed version: <span id="currentVersion">--</span></p>
            <div id="updatePanel" class="update-panel">
                <p id="updateMessage" role="status" aria-live="polite">Checking for updates...</p>
                <div class="actions">
                      <button id="checkUpdatesButton" class="secondary" type="button" onclick="checkForUpdates(true)">Check now</button>
                      <button id="installUpdateButton" type="button" onclick="installLatestFirmware()" disabled>Install update</button>
                </div>
                <p><a id="releaseLink" class="hidden" href="#" target="_blank" rel="noopener noreferrer">View release notes</a></p>
            </div>
            <label for="firmwareSource">Fixed firmware source</label>
            <input id="firmwareSource" type="url" value="https://github.com/iam169459/esp12e-door-lock/releases/latest/download/firmware.bin" readonly />
        </section>
  </div>

  <script>
        let unlockPending = false;
        let cardScanPending = false;
        let updateCheckPending = false;
        let statusRefreshPending = false;
        let updateAvailable = false;

        function showTab(tabName, button) {
            document.querySelectorAll('.tab').forEach(tab => {
                const active = tab === button;
                tab.classList.toggle('active', active);
                tab.setAttribute('aria-selected', active ? 'true' : 'false');
            });
            document.querySelectorAll('section[id]').forEach(section => section.classList.add('hidden'));
            document.getElementById(tabName).classList.remove('hidden');
            if (tabName === 'wifi') wifiScan();
    }

    async function refreshStatus() {
            if (statusRefreshPending || document.hidden) return;
            statusRefreshPending = true;
            try {
                const response = await fetch('/api/status', { cache: 'no-store' });
                if (!response.ok) throw new Error('Status request failed');
                const data = await response.json();
                document.getElementById('connectionState').textContent = 'Device online';
                document.getElementById('connectionState').classList.add('online');
                document.getElementById('doorState').textContent = data.doorOpen ? 'Open' : 'Closed';
                document.getElementById('relayState').textContent = data.relayOpen ? 'Unlocking' : 'Locked';
                document.getElementById('ipAddr').textContent = data.ip || '--';
                document.getElementById('lastUid').textContent = data.lastUid || 'None';
                document.getElementById('currentVersion').textContent = data.firmwareVersion || '--';
                updateUnlockControl(data);
                renderTags(data.tags || []);
            } catch (error) {
                document.getElementById('connectionState').textContent = 'Device unavailable';
                document.getElementById('connectionState').classList.remove('online');
            } finally {
                statusRefreshPending = false;
            }
        }

        async function loadSettings() {
            try {
                const response = await fetch('/api/settings', { cache: 'no-store' });
                if (!response.ok) throw new Error('Settings unavailable');
                const data = await response.json();
                document.getElementById('wifiSsid').value = data.wifiSSID || '';
                document.getElementById('unlockMs').value = data.unlockMs || 3000;
                document.getElementById('googleSheetUrl').value = data.googleSheetUrl || '';
                document.getElementById('googleLoggingEnabled').checked = Boolean(data.googleLoggingEnabled);
            } catch (error) {
                document.getElementById('unlockFeedback').textContent = 'Could not load saved settings.';
            }
        }

        async function submitWithoutReload(event) {
            event.preventDefault();
            const form = event.currentTarget;
            const button = form.querySelector('button[type="submit"]');
            const feedback = document.getElementById(form.dataset.feedback);
            const idleLabel = button.textContent;
            const formData = new FormData(form);
            const values = new URLSearchParams();
            formData.forEach((value, key) => values.append(key, value));
            button.disabled = true;
            button.textContent = 'Saving...';
            feedback.textContent = '';

            try {
                const response = await fetch(form.action, { method: form.method, body: values });
                if (!response.ok) throw new Error('Could not save changes.');
                if (form.id === 'manualCardForm') {
                    feedback.textContent = 'Card added.';
                    form.reset();
                    await refreshStatus();
                } else {
                    feedback.textContent = 'Changes saved.';
                    await loadSettings();
                }
            } catch (error) {
                feedback.textContent = error.message || 'Could not save changes.';
            } finally {
                button.disabled = false;
                button.textContent = idleLabel;
            }
        }

        function updateUnlockControl(data) {
            const button = document.getElementById('unlockButton');
            const feedback = document.getElementById('unlockFeedback');
            const remainingMs = Number(data.unlockCooldownMs) || 0;
            button.disabled = unlockPending || remainingMs > 0;
            button.textContent = unlockPending ? 'Sending request...' : data.relayOpen ? 'Unlocking...' : remainingMs > 0 ? 'Wait ' + Math.ceil(remainingMs / 1000) + 's' : 'Unlock door';
            if (data.relayOpen) feedback.textContent = 'Unlock pulse active.';
            else if (remainingMs > 0) feedback.textContent = 'Unlock cooldown active.';
            else if (!unlockPending) feedback.textContent = 'Ready.';
        }

        function renderTags(tags) {
            const table = document.getElementById('tagTable');
            const rows = document.createDocumentFragment();
            tags.forEach(tag => {
                const uid = typeof tag === 'string' ? tag : tag.uid;
                const holderName = typeof tag === 'string' ? '' : tag.name;
                const row = document.createElement('tr');
                const holderCell = document.createElement('td');
                holderCell.textContent = holderName || 'Unassigned';
                const uidCell = document.createElement('td');
                uidCell.textContent = uid;
                const actionCell = document.createElement('td');
                actionCell.className = 'row-actions';
                const renameButton = document.createElement('button');
                renameButton.type = 'button';
                renameButton.className = 'secondary';
                renameButton.textContent = 'Rename';
                renameButton.addEventListener('click', () => {
                    document.getElementById('cardHolderName').value = holderName;
                    document.getElementById('manualUid').value = uid;
                    document.getElementById('manualCardFeedback').textContent = 'Update the holder name and save.';
                    document.getElementById('cardHolderName').focus();
                });
                const removeButton = document.createElement('button');
                removeButton.type = 'button';
                removeButton.className = 'warn';
                removeButton.textContent = 'Remove';
                removeButton.addEventListener('click', () => removeTag(uid));
                actionCell.appendChild(renameButton);
                actionCell.appendChild(removeButton);
                row.append(holderCell, uidCell, actionCell);
                rows.appendChild(row);
            });
            if (!tags.length) {
                const row = document.createElement('tr');
                const cell = document.createElement('td');
                cell.colSpan = 3;
                cell.textContent = 'No authorized cards.';
                row.appendChild(cell);
                rows.appendChild(row);
            }
            table.replaceChildren(rows);
    }

    async function wifiScan() {
            const button = document.getElementById('wifiScanButton');
            button.disabled = true;
            button.textContent = 'Scanning...';
            try {
                const response = await fetch('/api/wifi-scan');
                if (!response.ok) throw new Error('Scan failed');
                const networks = await response.json();
                const table = document.getElementById('wifiTable');
                const rows = document.createDocumentFragment();
                networks.forEach(network => {
                    const row = document.createElement('tr');
                    [network.ssid, network.rssi + ' dBm', network.enc].forEach(value => {
                        const cell = document.createElement('td');
                        cell.textContent = value;
                        row.appendChild(cell);
                    });
                    rows.appendChild(row);
                });
                if (!networks.length) {
                    const row = document.createElement('tr');
                    const cell = document.createElement('td');
                    cell.colSpan = 3;
                    cell.textContent = 'No networks found.';
                    row.appendChild(cell);
                    rows.appendChild(row);
                }
                table.replaceChildren(rows);
            } catch (error) {
                document.getElementById('wifiTable').textContent = 'Could not scan for networks.';
            } finally {
                button.disabled = false;
                button.textContent = 'Scan networks';
            }
    }

        async function testSheetsConnection() {
            const status = document.getElementById('sheetsTestStatus');
            status.textContent = 'Sending a test event...';
            try {
                const response = await fetch('/api/sheets-test', { method: 'POST' });
                const data = await response.json();
                if (!response.ok || !data.ok) throw new Error(data.message || 'Connection test failed.');
                status.textContent = data.message + ' (HTTP ' + data.httpCode + ').';
            } catch (error) {
                status.textContent = error.message || 'Could not test the Sheets connection.';
            }
        }

        async function scanAndAddCard() {
            if (cardScanPending) return;
            const holderName = document.getElementById('cardHolderName').value.trim();
            if (!holderName) {
                document.getElementById('scanStatus').textContent = 'Enter the card holder name first.';
                document.getElementById('cardHolderName').focus();
                return;
            }
            cardScanPending = true;
            const button = document.getElementById('scanCardButton');
      const statusEl = document.getElementById('scanStatus');
            button.disabled = true;
            button.textContent = 'Waiting for card...';
            try {
                const response = await fetch('/api/scan-tag');
                const data = await response.json();
                if (!response.ok || !data.ok) throw new Error(data.error || 'No card detected');
                const added = await fetch('/api/add-tag', { method: 'POST', body: new URLSearchParams({ uid: data.uid, name: holderName }) });
                if (!added.ok) throw new Error('Could not add this card');
                statusEl.textContent = 'Card assigned to ' + holderName + ': ' + data.uid;
                await refreshStatus();
            } catch (error) {
                statusEl.textContent = error.message;
            } finally {
                cardScanPending = false;
                button.disabled = false;
                button.textContent = 'Scan and add card';
      }
    }

    async function unlockDoor() {
            if (unlockPending) return;
            unlockPending = true;
            const button = document.getElementById('unlockButton');
            button.disabled = true;
            button.textContent = 'Sending request...';
            try {
                const response = await fetch('/api/unlock', { method: 'POST' });
                const data = await response.json();
                document.getElementById('unlockFeedback').textContent = response.status === 429 ? 'Unlock already active. Please wait.' : data.ok ? 'Unlock request accepted.' : 'Unlock request failed.';
            } catch (error) {
                document.getElementById('unlockFeedback').textContent = 'Could not reach the lock.';
            } finally {
                unlockPending = false;
                await refreshStatus();
            }
        }

        async function checkForUpdates(forceRefresh = false) {
            if (updateCheckPending) return;
            updateCheckPending = true;
            const button = document.getElementById('checkUpdatesButton');
            const panel = document.getElementById('updatePanel');
            const message = document.getElementById('updateMessage');
            button.disabled = true;
            message.textContent = 'Checking GitHub for a release...';
            try {
                const response = await fetch('/api/update-check' + (forceRefresh ? '?refresh=1' : ''), { cache: 'no-store' });
                const data = await response.json();
                if (!response.ok || !data.ok) throw new Error(data.error || 'Update check failed');
                updateAvailable = Boolean(data.updateAvailable);
                panel.classList.toggle('available', updateAvailable);
                message.textContent = updateAvailable ? 'Version ' + data.latestVersion + ' is available.' : 'Firmware is up to date (' + data.currentVersion + ').';
                document.getElementById('installUpdateButton').disabled = !updateAvailable;
                const releaseLink = document.getElementById('releaseLink');
                if (data.releaseUrl && data.releaseUrl.startsWith('https://github.com/iam169459/esp12e-door-lock/releases/')) {
                    releaseLink.href = data.releaseUrl;
                    releaseLink.classList.remove('hidden');
                }
            } catch (error) {
                message.textContent = error.message || 'Could not check for updates.';
            } finally {
                updateCheckPending = false;
                button.disabled = false;
            }
        }

        async function installLatestFirmware() {
            const button = document.getElementById('installUpdateButton');
            button.disabled = true;
            document.getElementById('updateMessage').textContent = 'Downloading firmware...';
            try {
                const response = await fetch('/api/ota', { method: 'POST' });
                if (!response.ok) throw new Error(await response.text());
                document.getElementById('updateMessage').textContent = 'Firmware installed. Device is restarting...';
            } catch (error) {
                document.getElementById('updateMessage').textContent = error.message || 'Update failed.';
                button.disabled = !updateAvailable;
            }
    }

    async function removeTag(uid) {
      const formData = new FormData();
      formData.append('uid', uid);
      await fetch('/api/remove-tag', { method: 'POST', body: formData });
      refreshStatus();
    }

    async function restartDevice() {
            if (confirm('Restart the door lock now?')) await fetch('/api/restart', { method: 'POST' });
    }
        document.querySelectorAll('form[data-feedback]').forEach(form => form.addEventListener('submit', submitWithoutReload));
    refreshStatus();
    loadSettings();
        checkForUpdates();
                setInterval(refreshStatus, 1500);
        setInterval(checkForUpdates, 15 * 60 * 1000);
        document.addEventListener('visibilitychange', () => {
            if (!document.hidden) {
                refreshStatus();
                loadSettings();
            }
        });
  </script>
</body>
</html>
)HTML";
    server.send_P(200, "text/html", page);
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
    if (server.hasArg("wifiPassword") && server.arg("wifiPassword").length() > 0)
    {
        settings.wifiPassword = server.arg("wifiPassword");
    }
    if (server.hasArg("googleSheetUrl"))
    {
        settings.googleSheetUrl = server.arg("googleSheetUrl");
        settings.googleLoggingEnabled = server.hasArg("googleLoggingEnabled");
    }
    if (server.hasArg("unlockMs"))
    {
        long requestedUnlockMs = server.arg("unlockMs").toInt();
        if (requestedUnlockMs < 500)
        {
            requestedUnlockMs = 500;
        }
        if (requestedUnlockMs > 30000)
        {
            requestedUnlockMs = 30000;
        }
        settings.unlockMs = requestedUnlockMs;
    }
    saveSettings();
    server.sendHeader("Location", "/");
    server.send(302, "text/plain", "Saved");
}

void handleSettingsRead()
{
    DynamicJsonDocument doc(384);
    doc["wifiSSID"] = settings.wifiSSID;
    doc["googleSheetUrl"] = settings.googleSheetUrl;
    doc["googleLoggingEnabled"] = settings.googleLoggingEnabled;
    doc["unlockMs"] = settings.unlockMs;
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
}

void handleAddTag()
{
    String uid = server.arg("uid");
    uid.trim();
    uid.toUpperCase();
    String holderName = server.arg("name");
    holderName.trim();
    if (holderName.length() == 0)
    {
        holderName = "User " + String(authorizedTagCount + 1);
    }

    if (!addAuthorizedTag(uid, holderName))
    {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid UID or card limit reached\"}");
        return;
    }

    server.send(200, "application/json", "{\"ok\":true}");
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
    unsigned long now = millis();
    unsigned long retryAfterMs = 0;
    if (relayOpen)
    {
        retryAfterMs = settings.unlockMs - (now - relayActivatedAt);
    }
    else if (hasUnlockedSinceBoot && now - relayActivatedAt < settings.unlockMs + 1000UL)
    {
        retryAfterMs = settings.unlockMs + 1000UL - (now - relayActivatedAt);
    }

    if (retryAfterMs > 0)
    {
        server.send(429, "application/json", "{\"ok\":false,\"error\":\"unlock_cooldown\",\"retryAfterMs\":" + String(retryAfterMs) + "}");
        return;
    }

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
    String url = REPO_OTA_URL;
    bool success = installFirmwareFromUrl(url);
    if (success)
    {
        server.send(200, "text/plain", "Firmware updated. Rebooting...");
        delay(2000);
        ESP.restart();
        return;
    }

    Serial.println("OTA failed: " + otaFailureReason);
    server.send(500, "text/plain", "OTA failed: " + otaFailureReason);
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
    unsigned long now = millis();
    unsigned long repeatGuardMs = settings.unlockMs + 250UL;
    if (repeatGuardMs < RFID_REPEAT_GUARD_MS)
    {
        repeatGuardMs = RFID_REPEAT_GUARD_MS;
    }

    if (uid.equalsIgnoreCase(lastProcessedRfidUid) && now - lastProcessedRfidAt < repeatGuardMs)
    {
        lastProcessedRfidAt = now;
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
    }

    lastProcessedRfidUid = uid;
    lastProcessedRfidAt = now;
    lastUid = uid;
    bool allowed = isAuthorized(uid);

    if (allowed)
    {
        lastStatus = "access granted";
        tone(BUZZER_PIN, 2000, 200);
        unlockDoor();
        postToGoogleSheet("access", uid, "granted: " + holderNameFor(uid));
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
    server.on("/api/update-check", HTTP_GET, handleUpdateCheck);
    server.on("/api/settings", HTTP_GET, handleSettingsRead);
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
                delay(RFID_SCAN_POLL_MS);
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
    server.on("/api/sheets-test", HTTP_POST, handleSheetsTest);
    server.begin();

    Serial.println("Door lock ready");
    Serial.print("WiFi: ");
    Serial.println(settings.wifiSSID);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
}

void loop()
{
    updateRelay();
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

    delay(RFID_SCAN_POLL_MS);
}

void handleSheetsTest()
{
    bool succeeded = postToGoogleSheet("test", "TEST", "Manual connection test", true);
    DynamicJsonDocument response(192);
    response["ok"] = succeeded;
    response["httpCode"] = lastSheetsHttpCode;
    response["message"] = succeeded ? "Apps Script redirected the response; verify the test row exists" : lastSheetsError;
    String output;
    serializeJson(response, output);
    server.send(succeeded ? (lastSheetsHttpCode >= 300 ? 202 : 200) : 502, "application/json", output);
}
