#pragma once

#include <cJSON.h>

#define NVS_NAMESPACE "NETATMO"

#define NVS_TZ_TOKEN "TZ"
#define NVS_TZ_LONG_TOKEN "TZ_LONG"
#define NVS_CITY "CITY"
#define NVS_COUNTRY "COUNTRY"
#define NVS_LATITUDE "LAT"
#define NVS_LONGITUDE "LONG"
#ifndef CONFIG_IPGEOLOCATION_IO_API_KEY
#define CONFIG_IPGEOLOCATION_IO_API_KEY "c882ac7eeafe4b4ea7ed5e7c9f474540"
#endif
#define CLIENT_API_KEY CONFIG_IPGEOLOCATION_IO_API_KEY

class IpGeolocationIoParams {
public:
  virtual char *get_str_parameter() = 0;
};

class IpGeolocationIoIpGeoParams : public IpGeolocationIoParams {
private:
  const char *_ip = nullptr;
  const char *_lang = nullptr;
  const char *_fields = nullptr;
  const char *_excludes = nullptr;
  const char *_include = nullptr;

public:
  IpGeolocationIoIpGeoParams(const char *ip, const char *lang,
                             const char *fields, const char *excludes,
                             const char *include);
  char *get_str_parameter() override;
};

class IpGeolocationIo {

public:
  char *tz;
  char *tz_long;
  char *city;
  char *country;
  char *latitude;
  char *longitude;

  int update_geoloc();

private:
  int https_with_hostname_params(const char *path,
                                 IpGeolocationIoParams *params, cJSON **output);
};