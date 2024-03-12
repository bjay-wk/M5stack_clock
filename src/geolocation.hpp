#include "ipgeolocation_io.hpp"

#pragma once

#include <cJSON.h>

#define NVS_NAMESPACE "GEO"

#define NVS_TZ_TOKEN "TZ"
#define NVS_TZ_LONG_TOKEN "TZ_LONG"
#define NVS_CITY "CITY"
#define NVS_COUNTRY "COUNTRY"
#define NVS_LATITUDE "LAT"
#define NVS_LONGITUDE "LONG"

class Geolocation {

public:
  const char *city() const { return _city; }
  const char *country() const { return _country; }
  const char *tz() const { return _tz; }
  const char *posix_tz() const { return _posix_tz; }
  float latitude() { return _latitude; }
  float longitude() { return _longitude; }
  int update_geoloc();

private:
  float _latitude;
  float _longitude;
  char _city[86] = {0};
  char _country[57] = {0};
  char _tz[31] = {0};
  char _posix_tz[45] = {0};
  int download_posix_tz();
  void save_data();
  void restore_data();
};