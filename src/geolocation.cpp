#include "geolocation.hpp"
#include "ipgeolocation_io.hpp"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <string>

#define GITHUB_POSIX_TZ_DB                                                     \
  "https://raw.githubusercontent.com/nayarsystems/posix_tz_db/master/"         \
  "zones.json"
#define TAG "geolocation"
#define ARRAY_LENGTH(array) (sizeof((array)) / sizeof((array)[0]))
#define MAX_HTTP_OUTPUT_BUFFER_CALLOC 26 * 1024

int Geolocation::update_geoloc() {
  IpGeolocationIoIpGeoParams *geoloc = new IpGeolocationIoIpGeoParams(
      nullptr, nullptr, "city,country_name,time_zone,latitude,longitude",
      nullptr, nullptr);
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
  return return_value;
}

int Geolocation::download_posix_tz() {
  esp_http_client_config_t config = {};
  char *output_buffer = NULL;
  const std::string url = GITHUB_POSIX_TZ_DB;
  config.url = url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_http_client_set_header(client, "accept", "application/json");
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
    output_buffer = (char *)calloc(MAX_HTTP_OUTPUT_BUFFER_CALLOC, 1);
    int total_read = 0;
    int read = 0;
    do {
      read = esp_http_client_read_response(client, output_buffer + total_read,
                                           MAX_HTTP_OUTPUT_BUFFER_CALLOC -
                                               total_read);
      total_read += read;
    } while (read > 0);
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
    free(output_buffer);
  }

  esp_http_client_close(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return status_code;
}