#pragma once

#include "esp_log.h"
#include "open_meteo.hpp"
#include <time.h>

typedef struct Forecast24 {
  OM_SDK::WeatherCode weather_code[24];
  float uv_index[24] = {0};
  float precipitation_probability[24] = {0};
  float temperature_2m[24] = {0};
} Forecast24;

typedef struct ForecastTmr {
  OM_SDK::WeatherCode weather_code[2];
  float uv_index[2] = {0};
  float precipitation_probability[2] = {0};
  float temperature_2m[2] = {0};
} ForecastTmr;

typedef struct Forecast7 {
  float temperature_2m_max[7] = {0.0};
  float temperature_2m_min[7] = {0.0};
  float precipitation_probability_max[7] = {0};
  float uv_index_max[7] = {0};
  time_t sunset[7];
  time_t sunrise[7];
  OM_SDK::WeatherCode weather_code[7];
} Forecast7;

class Weather {
public:
  Forecast24 forecast24;
  Forecast7 forecast7;
  ForecastTmr forecast_tmr;
  Weather();
  void update_weather(float latitude, float longitude);

private:
  time_t expiry_time = {0};
  void copy_hourly(const openmeteo_sdk::WeatherApiResponse *output);
  void copy_daily(const openmeteo_sdk::WeatherApiResponse *output);
  void save();
  void restore();
};