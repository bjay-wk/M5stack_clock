#pragma once

#include "esp_http_server.h"
#include "string.h"

typedef enum ActionEnum {
  ScreenOff,
  UpdateScreen,
  WifiConnected,
  WifiDisconnected,
  ApStarted,
  ButtonClicked,
} ActionEnum;

class Action {
  char *_value = nullptr;

public:
  ActionEnum action = ActionEnum::ScreenOff;
  char *get_value() { return _value; }
  void set_value(const char *value) {
    if (_value != nullptr)
      free(_value);
    _value = strdup(value);
  }
  bool is_configuration() {
    switch (action) {
    case WifiConnected:
    case ApStarted:
      return true;
    default:
      return false;
    }
  }
  ~Action() {
    if (_value != nullptr)
      free(_value);
  }
};

class HttpManagerBase {
public:
  static char *get_arg(const char *uri, const char arg[], int size_of_arg) {
    char *value = strdup(strstr(uri, arg) + size_of_arg - 1);
    char *value_end = strchr(value, '&');
    if (value_end) {
      *value_end = '\0';
    }
    return value;
  }
  virtual esp_err_t httphandler(httpd_req_t *req, const char *str_ip,
                                Action *action) = 0;
};