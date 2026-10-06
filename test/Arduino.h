#pragma once

#include <string>
#include <cctype>

class String
{
public:
    String() = default;
    String(const char *str) : data(str) {}
    String(const std::string &str) : data(str) {}
    String(size_t count, char c) : data(count, c) {}
    String(const String &other) = default;
    String &operator=(const String &other) = default;

    size_t length() const { return data.length(); }
    bool startsWith(const char *prefix) const { return data.rfind(prefix, 0) == 0; }
    bool startsWith(const String &prefix) const { return data.rfind(prefix.data, 0) == 0; }
    char operator[](size_t i) const { return data[i]; }
    char &operator[](size_t i) { return data[i]; }
    String substring(size_t start, size_t end = std::string::npos) const { return String(data.substr(start, end - start)); }
    void trim() { /* simplified */ }
    void toUpperCase() { for (auto &c : data) c = std::toupper(c); }
    void remove(size_t pos, size_t len = std::string::npos) { data.erase(pos, len); }
    bool equalsIgnoreCase(const String &other) const { return strcasecmp(data.c_str(), other.data.c_str()) == 0; }
    String operator+(const String &other) const { return String(data + other.data); }
    String &operator+=(const String &other) { data += other.data; return *this; }
    bool operator==(const String &other) const { return data == other.data; }
    bool operator!=(const String &other) const { return data != other.data; }
    const char *c_str() const { return data.c_str(); }
    operator const char *() const { return data.c_str(); }

private:
    std::string data;
};