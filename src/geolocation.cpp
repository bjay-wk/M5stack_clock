#include "geolocation.hpp"
#include "ipgeolocation_io.hpp"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <nvs.h>
#include <string>

#define GITHUB_POSIX_TZ_DB                                                     \
  "https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/"         \
  "zones.json"

#define TAG "geolocation"
#define ARRAY_LENGTH(array) (sizeof((array)) / sizeof((array)[0]))
#define MAX_HTTP_OUTPUT_BUFFER_CALLOC 2 * 1024

#define NVS_NAMESPACE "GEO"
#define NVS_TZ "TZ"
#define NVS_POSIX_TZ "TZ_POSIX"
#define NVS_CITY "CITY"
#define NVS_COUNTRY "COUNTRY"
#define NVS_LATITUDE "LAT"
#define NVS_LONGITUDE "LONG"
#define NVS_IP "IP"

int get_public_ip6(esp_ip6_addr_t *ip);

Geolocation::Geolocation()
    : _latitude{0.0}, _longitude{0.0}, _city{""}, _country{""}, _tz{""},
      _posix_tz{""}, _ip_set{false} {
  restore_data();
}

int Geolocation::update_geoloc() {
  ESP_LOGI(TAG, "Updating");
  esp_ip6_addr_t ip;
  int response_code = get_public_ip6(&ip);
  if (response_code == 200) {
    if (_ip_set && !memcmp(&ip, &_public_ip, sizeof(ip))) {
      ESP_LOGI(TAG, "public ip did not change, canceling update");
      return 200;
    }
    memcpy(&_public_ip, &ip, sizeof(ip));
    _ip_set = true;
  }
  IpGeolocationIoIpGeoParams *geoloc = new IpGeolocationIoIpGeoParams(
      nullptr, nullptr, "city,country_name,time_zone,latitude,longitude",
      "country_name_official", nullptr);
  cJSON *data = NULL;
  int return_value = IpGeolocationIo::get_location(geoloc, &data);

  if (return_value == 200 && data) {
    char *tmp_str = cJSON_GetObjectItemCaseSensitive(data, "city")->valuestring;
    strcpy(_country, tmp_str ? tmp_str : "");
    tmp_str =
        cJSON_GetObjectItemCaseSensitive(data, "country_name")->valuestring;
    strcpy(_city, tmp_str ? tmp_str : "");
    cJSON *timezone = cJSON_GetObjectItemCaseSensitive(data, "time_zone");
    tmp_str = cJSON_GetObjectItemCaseSensitive(timezone, "name")->valuestring;
    strcpy(_tz, tmp_str ? tmp_str : "");

    tmp_str = cJSON_GetObjectItemCaseSensitive(data, "latitude")->valuestring;
    _latitude = tmp_str ? std::stof(tmp_str) : 0.0;
    tmp_str = cJSON_GetObjectItemCaseSensitive(data, "longitude")->valuestring;
    _longitude = tmp_str ? std::stof(tmp_str) : 0.0;

    cJSON_Delete(data);
  }
  download_posix_tz();
  save_data();
  return return_value;
}

int http_get(esp_http_client_config_t *config, esp_http_client_handle_t client,
             char *output_buffer) {
  int data_len = 0;
  char *data = NULL;
  esp_err_t err = esp_http_client_open(client, data_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
  } else if (data != NULL &&
             esp_http_client_write(client, data, data_len) < 0) {
    ESP_LOGE(TAG, "Write failed");
  } else if (esp_http_client_fetch_headers(client) < 0) {
    ESP_LOGE(TAG, "HTTP client fetch headers failed");
  } else {
    int total_read = 0;
    int read = 0;
    do {
      read = esp_http_client_read_response(client, output_buffer + total_read,
                                           MAX_HTTP_OUTPUT_BUFFER_CALLOC -
                                               total_read);
      total_read += read;
    } while (read > 0);
  }
  esp_http_client_close(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return status_code;
}

int Geolocation::download_posix_tz() {
  esp_http_client_config_t config = {};
  char *output_buffer = (char *)calloc(MAX_HTTP_OUTPUT_BUFFER_CALLOC, 1);
  config.url = GITHUB_POSIX_TZ_DB;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_http_client_set_header(client, "accept", "application/json");

  int code = http_get(&config, client, output_buffer);
  if (code == 200) {
    cJSON *output = cJSON_Parse(output_buffer);
    if (output) {
      auto tz_long_json = cJSON_GetObjectItemCaseSensitive(output, _tz);
      if (tz_long_json && tz_long_json->valuestring) {
        strcpy(_posix_tz, tz_long_json->valuestring);
      } else {
        strcpy(_posix_tz, _tz);
      }
      cJSON_Delete(output);
    }
  }
  free(output_buffer);
  return code;
}

int get_public_ip6(esp_ip6_addr_t *ip) {
  esp_http_client_config_t config = {};
  char output_buffer[40] = {0};
  config.url = "http://myexternalip.com/raw";
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_GET);

  int code = http_get(&config, client, output_buffer);
  if (code == 200) {
    esp_netif_str_to_ip6(output_buffer, ip);
  }
  return code;
}

esp_err_t save_string(nvs_handle_t handle, const char *key, const char *value) {
  esp_err_t err = nvs_set_str(handle, key, value);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "%s: %s", key, esp_err_to_name(err));
  }
  return err;
}

esp_err_t save_float(nvs_handle_t handle, const char *key, float value) {
  int32_t *casted_value = reinterpret_cast<int32_t *>(&value);
  esp_err_t err = nvs_set_i32(handle, key, *casted_value);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "%s: %s", key, esp_err_to_name(err));
  }
  return err;
}

void Geolocation::save_data() {
  nvs_handle_t handle;
  ESP_LOGI(TAG, "Saving data");
  auto err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }
  if (!_ip_set) {
    nvs_erase_key(handle, NVS_IP);
  }
  save_string(handle, NVS_CITY, _city);
  save_string(handle, NVS_COUNTRY, _country);
  save_string(handle, NVS_TZ, _tz);
  save_string(handle, NVS_POSIX_TZ, _posix_tz);
  save_float(handle, NVS_LONGITUDE, _longitude);
  save_float(handle, NVS_LATITUDE, _latitude);
  err = nvs_set_blob(handle, NVS_IP, &_public_ip, sizeof(_public_ip));
  if (ESP_OK != err) {
    ESP_LOGE(TAG, NVS_IP ": %s", esp_err_to_name(err));
  }

  err = nvs_commit(handle);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "commit: %s", esp_err_to_name(err));
  }
  nvs_close(handle);
}

void load_string(nvs_handle_t handle, const char *key, char *value) {
  size_t size = 0;
  esp_err_t err = nvs_get_str(handle, key, NULL, &size);
  if (err == ESP_OK) {
    err = nvs_get_str(handle, key, value, &size);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s: %s", key, esp_err_to_name(err));
  }
}

void load_float(nvs_handle_t handle, const char *key, float *value) {
  int32_t temp_value = 0;
  esp_err_t err = nvs_get_i32(handle, key, &temp_value);
  if (err == ESP_OK) {
    float *tmp_value_float = reinterpret_cast<float *>(&temp_value);
    *value = *tmp_value_float;
  } else {
    ESP_LOGE(TAG, "%s: %s", key, esp_err_to_name(err));
  }
}

void Geolocation::restore_data() {
  ESP_LOGI(TAG, "Restore Data");
  nvs_handle_t handle;
  auto err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }
  load_string(handle, NVS_CITY, _city);
  load_string(handle, NVS_COUNTRY, _country);
  load_string(handle, NVS_TZ, _tz);
  load_string(handle, NVS_POSIX_TZ, _posix_tz);
  load_float(handle, NVS_LONGITUDE, &_longitude);
  load_float(handle, NVS_LATITUDE, &_latitude);
  size_t size = sizeof(_public_ip);
  err = nvs_get_blob(handle, NVS_IP, &_public_ip, &size);
  if (err == ESP_OK) {
    _ip_set = true;
  } else {
    ESP_LOGE(TAG, "%s: %s", NVS_IP, esp_err_to_name(err));
  }
  nvs_close(handle);
}