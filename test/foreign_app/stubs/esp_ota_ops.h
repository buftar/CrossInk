#pragma once
#include <esp_app_format.h>
#include <esp_partition.h>
const esp_app_desc_t* esp_ota_get_app_description();
esp_err_t esp_ota_get_partition_description(const esp_partition_t* partition, esp_app_desc_t* desc);
