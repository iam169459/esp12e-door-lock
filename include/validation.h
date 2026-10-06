#pragma once

#include <Arduino.h>

bool isValidUid(const String &uid);
bool isValidHolderName(const String &name);
bool isValidSsid(const String &ssid);
bool isValidPassword(const String &pwd);
bool isValidUrl(const String &url);
bool isValidUnlockMs(uint16_t ms);