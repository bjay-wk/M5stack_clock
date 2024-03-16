#include "sntp.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include <string.h>

#define CONFIG_SNTP_TIME_SERVER "pool.ntp.org"

static const char *TAG = "SNTP";

static void update_sntp_time(void);

void settimezone(const char *timezone) {
  setenv("TZ", timezone, 1);
  tzset();
}

void get_time(const char *format, char *strftime_buf, size_t maxsize,
              int add_day) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  timeinfo.tm_mday += add_day;
  timeinfo.tm_wday += add_day;
  strftime(strftime_buf, maxsize, format, &timeinfo);
}

void check_and_update_ntp_time(void) {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year < (2016 - 1900)) {
    ESP_LOGI(TAG, "Time is not set yet. Getting time over NTP.");
    update_sntp_time();
  }
}

static void update_sntp_time(void) {
  ESP_LOGI(TAG, "Initializing and starting SNTP");
  esp_sntp_config_t config =
      ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_SNTP_TIME_SERVER);
  esp_netif_sntp_init(&config);
  time_t now = 0;
  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(tm));
  int retry = 0;
  const int retry_count = 15;
  while (esp_netif_sntp_sync_wait(2000 / portTICK_PERIOD_MS) ==
             ESP_ERR_TIMEOUT &&
         ++retry < retry_count) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry,
             retry_count);
  }
  time(&now);
  localtime_r(&now, &timeinfo);

  esp_netif_sntp_deinit();
}