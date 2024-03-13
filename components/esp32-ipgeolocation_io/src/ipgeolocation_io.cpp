#include "ipgeolocation_io.hpp"

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <sstream>

#ifndef CONFIG_IP_GEOLOCATION_IO_API_KEY
#error Please set GEOLOCATION_IO_API_KEY in menu config
#define CONFIG_IPGEOLOCATION_IO_API_KEY ""
#endif
#define API_KEY CONFIG_IP_GEOLOCATION_IO_API_KEY

#define WEB_URL "https://api.ipgeolocation.io"
#define GEOLOC "/ipgeo"
#define TAG "ipgeolocation"

#define MAX_HTTP_OUTPUT_BUFFER_CALLOC 26 * 1024

IpGeolocationIoIpGeoParams::IpGeolocationIoIpGeoParams(const char *ip,
                                                       const char *lang,
                                                       const char *fields,
                                                       const char *excludes,
                                                       const char *include)
    : _ip(ip), _lang(lang), _fields(fields), _excludes(excludes),
      _include(include) {}

std::string IpGeolocationIoIpGeoParams::get_str_parameter() {
  std::stringstream ss;
  if (_ip) {
    ss << "&ip=" << _ip;
  }
  if (_lang) {
    ss << "&lang=" << _lang;
  }
  if (_fields) {
    ss << "&fields=%s" << _fields;
  }
  if (_excludes) {
    ss << "&excludes=" << _excludes;
  }
  if (_include) {
    ss << "&include=" << _include;
  }
  return ss.str();
}

int IpGeolocationIo::get_location(IpGeolocationIoParams *params,
                                  cJSON **output) {
  ESP_LOGI(TAG, "CAll update  IP geoloc");
  return _https_with_hostname_params(GEOLOC, params, output);
}

int IpGeolocationIo::_https_with_hostname_params(const char *path,
                                                 IpGeolocationIoParams *params,
                                                 cJSON **output) {
  esp_http_client_config_t config = {};
  char *output_buffer = nullptr;
  const std::string url = WEB_URL + std::string(path) + "?apiKey=" API_KEY +
                          params->get_str_parameter();
  config.url = url.c_str();
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.transport_type = HTTP_TRANSPORT_OVER_SSL;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_http_client_set_header(client, "accept", "application/json");
  int data_len = 0;
  char *data = nullptr;
  esp_err_t err = esp_http_client_open(client, data_len);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
  } else if (data != nullptr &&
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
    if (output) {
      *output = cJSON_Parse(output_buffer);
    }
    free(output_buffer);
  }

  esp_http_client_close(client);
  const int status_code = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  return status_code;
}