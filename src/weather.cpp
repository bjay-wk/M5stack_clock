#include "weather.hpp"
#include <nvs.h>

#define NVS_NAMESPACE "Weather"
#define NVS_TIME "time"

#define NVS_FORECAST_24 "24"
#define NVS_FORECAST_7 "7"

#define ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof(_arr[0]))

const char TAG[] = "Weather";

Weather::Weather() { restore(); }

void Weather::copy_hourly(const openmeteo_sdk::WeatherApiResponse *output) {
  auto hourly_out = output->hourly()->variables();
  for (unsigned int i = 0; i < hourly_out->size(); i++) {
    auto data = hourly_out->Get(i);
    if (data->variable() == openmeteo_sdk::Variable_precipitation_probability) {
      for (int i = 0; i < data->values()->size(); ++i) {
        forecast24.precipitation_probability[i] = data->values()->Get(i);
      }
    } else if (data->variable() == openmeteo_sdk::Variable_temperature) {
      for (int i = 0; i < data->values()->size(); ++i) {
        ESP_LOGI(TAG, "Temp: %f", data->values()->Get(i));
        forecast24.temperature_2m[i] = data->values()->Get(i);
      }
    } else if (data->variable() == openmeteo_sdk::Variable_weather_code) {
      for (int i = 0; i < data->values()->size(); ++i) {
        forecast24.weather_code[i] =
            static_cast<OM_SDK::WeatherCode>(data->values()->Get(i));
      }
    } else if (data->variable() == openmeteo_sdk::Variable_uv_index) {
      for (int i = 0; i < data->values()->size(); ++i) {
        forecast24.uv_index[i] = data->values()->Get(i);
      }
    } else {
      ESP_LOGE(TAG, "Not Treated hourly %s",
               openmeteo_sdk::EnumNameVariable(data->variable()));
    }
  }
}

void Weather::copy_daily(const openmeteo_sdk::WeatherApiResponse *output) {
  auto daily = output->daily()->variables();
  bool has_scan_one = false;
  for (unsigned int i = 0; i < daily->size(); i++) {
    auto data = daily->Get(i);
    if (data->variable() == openmeteo_sdk::Variable_weather_code) {
      for (int i = 0; i < data->values()->size(); ++i) {
        forecast7.weather_code[i] =
            static_cast<OM_SDK::WeatherCode>(data->values()->Get(i));
      }
    } else if (data->variable() == openmeteo_sdk::Variable_temperature) {
      if (has_scan_one) {
        for (int i = 0; i < data->values()->size(); ++i) {
          forecast7.temperature_2m_max[i] = data->values()->Get(i);
        }
      } else {
        for (int i = 0; i < data->values()->size(); ++i) {
          forecast7.temperature_2m_min[i] = data->values()->Get(i);
        }
        has_scan_one = true;
      }
    } else if (data->variable() ==
               openmeteo_sdk::Variable_precipitation_probability) {
      for (int i = 0; i < data->values()->size(); ++i) {
        forecast7.precipitation_probability_max[i] = data->values()->Get(i);
      }
    } else if (data->variable() == openmeteo_sdk::Variable_uv_index) {
      for (int i = 0; i < data->values()->size(); ++i) {
        forecast7.uv_index_max[i] = data->values()->Get(i);
      }
    } else if (data->variable() == openmeteo_sdk::Variable_sunrise) {
      for (int i = 0; i < data->values_int64()->size(); ++i) {
        forecast7.sunrise[i] = data->values_int64()->Get(i);
      }
    } else if (data->variable() == openmeteo_sdk::Variable_sunset) {
      for (int i = 0; i < data->values_int64()->size(); ++i) {
        forecast7.sunrise[i] = data->values_int64()->Get(i);
      }
    } else {
      ESP_LOGE(TAG, "Not Treated daily %s",
               openmeteo_sdk::EnumNameVariable(data->variable()));
    }
  }
  if (forecast7.temperature_2m_min[0] > forecast7.temperature_2m_max[0]) {
    for (int i = 0; i < ARRAY_SIZE(forecast7.temperature_2m_min); ++i) {
      float tmp = forecast7.temperature_2m_min[i];
      forecast7.temperature_2m_min[i] = forecast7.temperature_2m_max[i];
      forecast7.temperature_2m_max[i] = tmp;
    }
  }
}

void Weather::update_weather(float latitude, float longitude) {
  ESP_LOGI(TAG, "Updating Weather");
  time_t now;
  time(&now);
  if (now < expiry_time) {
    return;
  }
  struct tm *tm = localtime(&now);
  tm->tm_mday += 1;
  tm->tm_hour = 0;
  tm->tm_min = 0;
  expiry_time = mktime(tm);

  OM_SDK::TimeParam hourly[] = {
      OM_SDK::temperature_2m, OM_SDK::precipitation_probability,
      OM_SDK::weather_code,   OM_SDK::uv_index,
      OM_SDK::max_params,
  };

  OM_SDK::TimeParam daily[] = {
      OM_SDK::weather_code,
      OM_SDK::temperature_2m_max,
      OM_SDK::temperature_2m_min,
      OM_SDK::sunrise,
      OM_SDK::sunset,
      OM_SDK::uv_index_max,
      OM_SDK::precipitation_probability_max,
      OM_SDK::max_params,
  };
  OM_SDK::OpenMeteoParams p{
      .latitude = latitude,
      .longitude = longitude,
      .hourly = hourly,
      .forecast_days = 1,

  };
  OM_SDK::OpenMeteoParams p2{
      .latitude = latitude,
      .longitude = longitude,
      .daily = daily,
      .forecast_days = 7,

  };
  openmeteo_sdk::WeatherApiResponse *output;
  OM_SDK::get_weather(&p, &output);
  copy_hourly(output);
  OM_SDK::get_weather(&p2, &output);
  copy_daily(output);
  save();
  ESP_LOGI(TAG, "Done Updating Weather");
}

void Weather::save() {
  if (expiry_time == 0) {
    return;
  }
  nvs_handle_t handle;
  ESP_LOGI(TAG, "Saving data");
  auto err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }
  err = nvs_set_i64(handle, NVS_TIME, expiry_time);
  err = nvs_set_blob(handle, NVS_FORECAST_24, &forecast24, sizeof(forecast24));
  err = nvs_set_blob(handle, NVS_FORECAST_7, &forecast7, sizeof(forecast7));

  err = nvs_commit(handle);
  if (ESP_OK != err) {
    ESP_LOGE(TAG, "commit: %s", esp_err_to_name(err));
  }
  nvs_close(handle);
}

void Weather::restore() {
  ESP_LOGI(TAG, "Restore Data");
  nvs_handle_t handle;
  auto err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
  }
  err = nvs_get_i64(handle, NVS_TIME, &expiry_time);
  size_t size = sizeof(forecast24);
  err = nvs_get_blob(handle, NVS_FORECAST_24, &forecast24, &size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s", esp_err_to_name(err));
  }
  size = sizeof(forecast7);
  err = nvs_get_blob(handle, NVS_FORECAST_24, &forecast7, &size);
  nvs_close(handle);
}