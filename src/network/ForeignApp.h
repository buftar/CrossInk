#pragma once

#include <esp_partition.h>

#include <string>

// Dual-boot: kept out of OtaBootSwitch.cpp so that file stays free of
// Preferences/esp_ota_ops and its host unit test (test/ota_boot_switch) still builds.
namespace ota_boot {

// Check whether the given OTA partition holds a "foreign" app — i.e. a valid
// app image whose identity differs from our own.  Returns an empty string when
// the slot is empty, corrupted, or holds our own app.  When non-empty the
// returned string is the display name of the foreign app (from ota_names NVS
// or the app-descriptor project_name as fallback).
//
// Zero heap cost on the empty/own-app path (stack-only reads).
std::string getForeignAppName(const esp_partition_t* target);

}  // namespace ota_boot
