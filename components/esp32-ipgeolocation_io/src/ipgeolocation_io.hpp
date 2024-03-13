#pragma once

#include <cJSON.h>
#include <string>

class IpGeolocationIoParams {
public:
  virtual std::string get_str_parameter() = 0;
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
  std::string get_str_parameter() override;
};

class IpGeolocationIo {

public:
  static int get_location(IpGeolocationIoParams *params, cJSON **output);

private:
  static int _https_with_hostname_params(const char *path,
                                         IpGeolocationIoParams *params,
                                         cJSON **output);
};