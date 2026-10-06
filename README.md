# ESP12E RFID Door Lock

A PlatformIO project for an ESP-12E door lock controller with an MFRC522 RFID reader, relay output, Wi‑Fi management, web dashboard, and GitHub OTA updates.

## Features

- MFRC522 RFID access control (supports 4-byte and 7-byte UIDs)
- Relay-driven door lock output with configurable unlock duration
- Web UI for status, Wi‑Fi scan, card manager, settings, and OTA updates
- Wi-Fi station connection configured in device settings
- Per-device random setup access point password
- Per-device random admin password with HTTP Digest authentication
- Google Sheets activity logging support (access granted/denied, door open/close)
- OTA firmware update from GitHub Releases with certificate pinning option
- Non-blocking WiFi reconnection with automatic AP fallback
- Input validation on all API endpoints
- Unit tests for validation logic

## Security

- First boot generates a unique admin password and setup AP password. Both are printed to the serial console (masked); record them securely.
- The username is `admin`. The web UI and all API routes require HTTP Digest authentication.
- Change the admin password in Settings; use at least 16 characters.
- The embedded server uses HTTP, not HTTPS. Digest authentication avoids sending the password directly, but page content and commands are not encrypted. Keep the lock on a trusted LAN or VPN, and do not forward its web port to the internet.
- RC522 UID-only cards can be cloned. For high-assurance access, use a cryptographic card and compatible reader.
- Optional SSL certificate verification for OTA and GitHub API (compile-time flag `VERIFY_SSL_CERTS`)
- Input validation on all API endpoints prevents injection attacks

If configured Wi-Fi is unavailable, the device starts `DoorLock-Setup` using its unique generated AP password.

## Hardware Wiring

### MFRC522 to ESP-12E / NodeMCU

| RC522 pin | ESP-12E / NodeMCU pin | GPIO |
| --- | --- | --- |
| VCC | 3.3V | - |
| GND | GND | - |
| RST | D2 | GPIO4 |
| SDA / SS | D8 | GPIO15 |
| SCK | D5 | GPIO14 |
| MOSI | D7 | GPIO13 |
| MISO | D6 | GPIO12 |
| IRQ | Not connected | - |

### Relay to ESP-12E / NodeMCU

| Relay | ESP-12E / NodeMCU | GPIO |
| --- | --- | --- |
| VCC | 5V or VIN | - |
| GND | GND | - |
| IN | D1 | GPIO5 |

### Optional Components

| Component | Pin | GPIO |
| --- | --- | --- |
| Door Sensor (reed switch) | D0 | GPIO16 |
| Buzzer | D4 | GPIO2 |
| Status LED | D3 | GPIO0 |

## PlatformIO Build and Upload

```bash
cd /path/to/esp12e-door-lock
~/.platformio/penv/bin/platformio run -e esp12e
~/.platformio/penv/bin/platformio run -t upload
```

### Build Options

Set `VERIFY_SSL_CERTS=true` to enable SSL certificate fingerprint verification for OTA and GitHub API calls:

```bash
pio run -e esp12e -D VERIFY_SSL_CERTS=true
```

Set `FIRMWARE_VERSION` to override version at build time:

```bash
pio run -e esp12e -D FIRMWARE_VERSION=v1.2.3
```

## Web UI

Open the device's web page after boot (http://<device-ip>/). The UI includes:

- **Dashboard** - Lock status, IP address, last card read, unlock button
- **Wi-Fi Scan** - Scan and display nearby networks
- **Card Manager** - Add/remove/rename authorized RFID cards
- **Settings** - WiFi, unlock duration, admin password, Google Sheets logging
- **OTA Update** - Check for and install firmware updates from GitHub

## API Reference

All endpoints require HTTP Digest authentication (username: `admin`, password: configured admin password).

### GET /api/status
Returns current device status.

**Response:**
```json
{
  "status": "locked",
  "relayOpen": false,
  "doorOpen": false,
  "ip": "192.168.1.42",
  "ssid": "MyNetwork",
  "firmwareVersion": "1.0.0",
  "unlockCooldownMs": 0,
  "tagCount": 5,
  "lastUid": "01A2B3C4"
}
```

### GET /api/cards?offset=0&limit=25
Returns paginated list of authorized cards.

**Query Parameters:**
- `offset` (int, default 0): Starting index
- `limit` (int, default 25, max 25): Number of cards per page

**Response:**
```json
{
  "total": 5,
  "offset": 0,
  "cards": [
    {"uid": "01A2B3C4", "name": "John Doe"},
    {"uid": "5D6E7F80", "name": "Jane Smith"}
  ]
}
```

### GET /api/update-check?refresh=1
Checks for firmware updates on GitHub.

**Query Parameters:**
- `refresh` (bool, default false): Force refresh bypassing cache

**Response:**
```json
{
  "ok": true,
  "currentVersion": "1.0.0",
  "latestVersion": "1.1.0",
  "updateAvailable": true,
  "releaseUrl": "https://github.com/iam169459/esp12e-door-lock/releases/tag/v1.1.0"
}
```

### GET /api/settings
Returns current device settings (passwords omitted).

**Response:**
```json
{
  "wifiSSID": "MyNetwork",
  "googleSheetUrl": "https://script.google.com/.../exec",
  "googleLoggingEnabled": true,
  "unlockMs": 3000
}
```

### GET /api/wifi-scan
Scans for nearby Wi-Fi networks.

**Response:**
```json
[
  {"ssid": "Network1", "rssi": -45, "enc": "SECURED"},
  {"ssid": "Network2", "rssi": -67, "enc": "OPEN"}
]
```

### GET /api/scan-tag
Waits up to 15 seconds for an RFID card scan.

**Response:**
```json
{"ok": true, "uid": "01A2B3C4"}
```
Or on timeout:
```json
{"ok": false, "error": "No card detected"}
```

### POST /api/settings
Updates device settings.

**Form Parameters:**
- `wifiSsid` (string, 1-32 chars): Wi-Fi SSID
- `wifiPassword` (string, 8-63 chars, optional): Wi-Fi password (omit to keep current)
- `googleSheetUrl` (string, valid HTTP/HTTPS URL, optional): Google Apps Script URL
- `googleLoggingEnabled` (bool, optional): Enable Google Sheets logging
- `unlockMs` (int, 500-30000): Unlock duration in milliseconds
- `adminPassword` (string, 16-64 chars, optional): New admin password

**Response:**
```json
{"ok": true}
```
Or if admin password changed:
```json
{"ok": true, "authChanged": true}
```

### POST /api/add-tag
Adds or updates an authorized card.

**Form Parameters:**
- `uid` (string, 4-20 hex chars, even length): Card UID
- `name` (string, 1-32 chars): Card holder name

**Response:**
```json
{"ok": true}
```

### POST /api/remove-tag
Removes an authorized card.

**Form Parameters:**
- `uid` (string): Card UID

**Response:**
```json
{"ok": true}
```

### POST /api/unlock
Triggers door unlock (respects cooldown).

**Response:**
```json
{"ok": true}
```
Or on cooldown:
```json
{"ok": false, "error": "unlock_cooldown", "retryAfterMs": 2500}
```

### POST /api/restart
Restarts the device.

**Response:**
```
Restarting
```

### POST /api/ota
Triggers OTA firmware update from GitHub.

**Response:**
```
Firmware updated. Rebooting...
```
Or on failure:
```
OTA failed: <reason>
```

### POST /api/sheets-test
Tests Google Sheets connection.

**Response:**
```json
{"ok": true, "httpCode": 200, "message": "Apps Script redirected the response; verify the test row exists"}
```

## Google Sheets Logging

1. Create a Google Sheet
2. Open Extensions > Apps Script
3. Paste the code from `google-apps-script/Code.gs`
4. Set `SPREADSHEET_ID` to your sheet ID (from URL)
5. Deploy > New deployment > Web App
6. Execute as: Me, Who has access: Anyone
7. Copy the deployment URL to device settings

Events logged:
- `access` - Card granted/denied with UID and holder name
- `door` - Door open/close events
- `test` - Manual connection tests

## OTA Update

The device checks GitHub Releases for updates. The firmware binary must be attached to the release as `firmware.bin`.

Release URL format:
```
https://github.com/iam169459/esp12e-door-lock/releases/latest/download/firmware.bin
```

Enable certificate verification (recommended for production):
```bash
pio run -e esp12e -D VERIFY_SSL_CERTS=true
```

Update fingerprints in `src/main.cpp` when GitHub rotates certificates.

## Testing

Run unit tests (native platform):
```bash
pio test -e native
```

Tests cover input validation functions:
- UID format validation
- Holder name validation
- SSID validation
- Password validation
- URL validation
- Unlock duration validation

## Configuration

Settings are stored in SPIFFS:
- `/settings.json` - Device settings (WiFi, unlock duration, Google Sheets, passwords)
- `/tags.json` - Authorized RFID cards (JSON Lines format)

### Settings Schema
```json
{
  "wifiSSID": "string",
  "wifiPassword": "string",
  "googleSheetUrl": "string",
  "adminPassword": "string",
  "apPassword": "string",
  "googleLoggingEnabled": "boolean",
  "unlockMs": "integer"
}
```

### Tags Format (JSON Lines)
```json
{"uid": "01A2B3C4", "name": "John Doe"}
{"uid": "5D6E7F80", "name": "Jane Smith"}
```

## Development

### Project Structure
```
esp12e-door-lock/
├── src/
│   ├── main.cpp          # Main firmware
│   └── validation.cpp    # Input validation functions
├── include/
│   └── validation.h      # Validation function declarations
├── test/
│   ├── test_validation.cpp
│   └── Arduino.h         # Arduino mock for native tests
├── scripts/
│   └── set_version.py    # Version injection script
├── google-apps-script/
│   └── Code.gs           # Google Apps Script for logging
├── .github/workflows/
│   ├── ci.yml            # CI pipeline
│   └── release-firmware.yml
├── platformio.ini
└── README.md
```

### Code Quality
- Input validation on all API endpoints
- Non-blocking WiFi state machine
- Certificate pinning support for HTTPS
- Masked password logging
- SPIFFS write verification
- Proper millis() rollover handling

## Release Notes

### v1.1.0 (Current)
- Fixed SPIFFS auto-format data loss bug
- Fixed RFID duplicate card handling race condition
- Fixed blocking WiFi reconnection in main loop
- Added input validation for all API endpoints
- Added SSL certificate fingerprint verification option
- Added masked password logging
- Added SPIFFS write verification
- Added unit tests for validation functions
- Enhanced CI/CD with linting, static analysis, and version validation
- Refactored WiFi connection to non-blocking state machine

### v1.0.0
- Initial ESP12E RFID door lock project
- MFRC522 access control
- Relay lock output
- Web UI and card management
- Wi‑Fi scan and configuration
- GitHub release OTA support

## Repository

- GitHub: https://github.com/iam169459/esp12e-door-lock
- Issues: https://github.com/iam169459/esp12e-door-lock/issues

## License

MIT License - see LICENSE file for details.