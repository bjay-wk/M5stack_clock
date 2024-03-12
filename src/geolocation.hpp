#pragma once

#include <cJSON.h>
#include <esp_netif.h>

class Geolocation {

public:
  Geolocation();
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
  char _city[86];
  char _country[57];
  char _tz[31];
  char _posix_tz[45];
  esp_ip6_addr_t _public_ip;
  bool _ip_set;
  int download_posix_tz();
  void save_data();
  void restore_data();
};