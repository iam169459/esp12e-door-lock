#include "validation.h"

bool isValidUid(const String &uid)
{
    if (uid.length() < 4 || uid.length() > 20 || (uid.length() % 2) != 0) return false;
    for (size_t i = 0; i < uid.length(); ++i)
    {
        char c = uid[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

bool isValidHolderName(const String &name)
{
    return name.length() > 0 && name.length() <= 32;
}

bool isValidSsid(const String &ssid)
{
    return ssid.length() > 0 && ssid.length() <= 32;
}

bool isValidPassword(const String &pwd)
{
    return pwd.length() >= 8 && pwd.length() <= 63;
}

bool isValidUrl(const String &url)
{
    return url.length() >= 10 && url.length() <= 256 && (url.startsWith("http://") || url.startsWith("https://"));
}

bool isValidUnlockMs(uint16_t ms)
{
    return ms >= 500 && ms <= 30000;
}