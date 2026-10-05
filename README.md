# ESP12E RFID Door Lock

A PlatformIO project for an ESP-12E door lock controller with an MFRC522 RFID reader, relay output, Wi‑Fi management, web dashboard, and GitHub OTA updates.

## Features

- MFRC522 RFID access control
- Relay-driven door lock output
- Web UI for status, Wi‑Fi scan, card manager, settings, and OTA updates
- Wi-Fi station connection configured in device settings
- Per-device random setup access point password
- Per-device random admin password with Digest authentication
- Google Sheets activity logging support
- OTA firmware update from GitHub Releases

## Security

- First boot generates a unique admin password and setup AP password. Both are printed to the serial console; record them securely.
- The username is `admin`. The web UI and all API routes require HTTP Digest authentication.
- Change the admin password in Settings; use at least 16 characters.
- The embedded server uses HTTP, not HTTPS. Digest authentication avoids sending the password directly, but page content and commands are not encrypted. Keep the lock on a trusted LAN or VPN, and do not forward its web port to the internet.
- RC522 UID-only cards can be cloned. For high-assurance access, use a cryptographic card and compatible reader.

If configured Wi-Fi is unavailable, the device starts `DoorLock-Setup` using its unique generated AP password.

## Hardware wiring

### MFRC522 to ESP-12E / NodeMCU

| RC522 pin | ESP-12E / NodeMCU pin |
| --- | --- |
| VCC | 3.3V |
| GND | GND |
| RST | D2 (GPIO4) |
| SDA / SS | D8 (GPIO15) |
| SCK | D5 (GPIO14) |
| MOSI | D7 (GPIO13) |
| MISO | D6 (GPIO12) |
| IRQ | Not connected |

### Relay to ESP-12E / NodeMCU

| Relay | ESP-12E / NodeMCU |
| --- | --- |
| VCC | 5V or VIN |
| GND | GND |
| IN | D1 (GPIO5) |

## PlatformIO build and upload

```bash
cd /Users/Apple/Documents/PlatformIO/Projects/esp12e-door-lock
~/.platformio/penv/bin/platformio run -e esp12e
~/.platformio/penv/bin/platformio run -t upload
```

## Web UI

Open the device’s web page after boot, usually on the assigned local IP. The UI includes:

- Dashboard
- Wi‑Fi scan
- Card manager
- Settings
- OTA update

## OTA update

This project uses the GitHub release binary URL:

```text
https://github.com/iam169459/esp12e-door-lock/releases/latest/download/firmware.bin
```

The device will download and install the firmware from the latest GitHub release asset.

## Release notes

### v1.0.0

- Initial ESP12E RFID door lock project
- MFRC522 access control
- Relay lock output
- Web UI and card management
- Wi‑Fi scan and configuration
- GitHub release OTA support

## Repository

- GitHub: https://github.com/iam169459/esp12e-door-lock
