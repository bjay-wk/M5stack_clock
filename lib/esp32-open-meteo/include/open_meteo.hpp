#include <weather_api_generated.h>

namespace OM_SDK {

typedef enum TimeParam {
  undefined,
  apparent_temperature,
  apparent_temperature_max,
  apparent_temperature_min,
  cape,
  cloud_cover,
  cloud_cover_high,
  cloud_cover_low,
  cloud_cover_mid,
  daylight_duration,
  dew_point_2m,
  diffuse_radiation,
  direct_normal_irradiance,
  direct_radiation,
  et0_fao_evapotranspiration,
  evapotranspiration,
  freezing_level_height,
  global_tilted_irradiance,
  global_tilted_irradiance_instant,
  is_day,
  lightning_potential,
  precipitation,
  precipitation_hours,
  precipitation_probability,
  precipitation_probability_max,
  precipitation_probability_mean,
  precipitation_probability_min,
  precipitation_sum,
  pressure_msl,
  rain,
  rain_sum,
  relative_humidity_2m,
  shortwave_radiation,
  shortwave_radiation_sum,
  showers,
  showers_sum,
  snow_depth,
  snowfall,
  snowfall_height,
  snowfall_sum,
  soil_moisture_0_to_1cm,
  soil_moisture_1_to_3cm,
  soil_moisture_27_to_81cm,
  soil_moisture_3_to_9cm,
  soil_moisture_9_to_27cm,
  soil_temperature_0cm,
  soil_temperature_18cm,
  soil_temperature_54cm,
  soil_temperature_6cm,
  sunrise,
  sunset,
  sunshine_duration,
  surface_pressure,
  temperature_2m,
  temperature_2m_max,
  temperature_2m_min,
  uv_index_clear_sky_max,
  uv_index_max,
  vapour_pressure_deficit,
  visibility,
  weather_code,
  wind_direction_10m,
  wind_direction_10m_dominant,
  wind_direction_120m,
  wind_direction_180m,
  wind_direction_80m,
  wind_gusts_10m,
  wind_gusts_10m_max,
  wind_speed_10m,
  wind_speed_10m_max,
  wind_speed_120m,
  wind_speed_180m,
  wind_speed_80m,
  max_params,
} TimeParam;

const char *const *EnumNamesTimeParams();

typedef enum Temperature_unit : uint8_t {
  undefined_tmp_unit = 0,
  celsius = 1,
  fahrenheit = 2,
} Temperature_unit;

const char *const *EnumNamesTemperatureUnit();

typedef enum Wind_speed_unit : uint8_t {
  undefined_wind_unit = 0,
  kmh = 1,
  ms = 2,
  mph = 3,
  kn = 4,
} Wind_speed_unit;

const char *const *EnumNamesWindSpeedUnit();

typedef enum Precipitation_unit : uint8_t {
  undefined_precipitation_unit = 0,
  mm = 1,
  inch = 2,
} Precipitation_unit;

const char *const *EnumNamesPrecipitationUnit();

typedef enum Timeformat : uint8_t {
  undefined_timeformat = 0,
  iso8601 = 1,
  unixtime = 2,
} Timeformat;
const char *const *EnumNamesTimeFormat();

typedef enum Cell_selection : uint8_t {
  undefined_selection = 0,
  land = 1,
  sea = 2,
  nearest = 3,
} Cell_selection;

const char *const *EnumNamesCellSelection();

struct OpenMeteoParams {
  float latitude;
  float longitude;
  float elevation{0.};
  bool elevation_default{true};
  TimeParam *hourly{nullptr};
  TimeParam *daily{nullptr}; // timezone is required.
  TimeParam *minutely_15{nullptr};
  TimeParam *current{nullptr};
  Temperature_unit temperature_unit{undefined_tmp_unit};
  Wind_speed_unit wind_speed_unit{undefined_wind_unit};
  Precipitation_unit precipitation_unit{undefined_precipitation_unit};
  Timeformat timeformat{undefined_timeformat};
  char *timezone{nullptr};
  int8_t past_days{0};
  int8_t forecast_days{0};
  int8_t forecast_hours{0};

  int8_t forecast_minutely_15{0};
  int8_t past_hours{0};
  int8_t past_minutely_15{0};
  time_t start_date{0};
  time_t end_date{0};
  time_t start_hour{0};
  time_t end_hour{0};
  time_t start_minutely_15{0};
  time_t end_minutely_15{0};
  openmeteo_sdk::Model *models{nullptr};
  Cell_selection cell_selection{undefined_selection};
};

int get_weather(OpenMeteoParams *params,
                openmeteo_sdk::WeatherApiResponse **output);
} // namespace OM_SDK
