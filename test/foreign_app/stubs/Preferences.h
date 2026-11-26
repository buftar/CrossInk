#pragma once
#include <cstdio>
#include <map>
#include <string>

// Minimal Arduino String / Preferences for host tests: one shared NVS map.
struct String {
  std::string s;
  String(const char* v = "") : s(v) {}
  bool isEmpty() const { return s.empty(); }
  const char* c_str() const { return s.c_str(); }
};
inline std::map<std::string, std::string> fakeNvs;  // "namespace/key" -> value
inline std::string fakeNvsOpened;
class Preferences {
 public:
  bool begin(const char* ns, bool) { fakeNvsOpened = ns_ = ns; return true; }
  String getString(const char* key, const char* fallback) {
    auto it = fakeNvs.find(ns_ + "/" + key);
    return String(it == fakeNvs.end() ? fallback : it->second.c_str());
  }
  void end() {}
 private:
  std::string ns_;
};
