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

public:
  Action(ActionEnum action) : _action(action) {}
  Action(ActionEnum action, const char *value) : _action(action) {
    if (value)
      _value = strdup(value);
  }

  ActionEnum action() { return _action; }

  char *value() { return _value; }

  bool is_configuration() {
    switch (_action) {
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

private:
  char *_value = nullptr;
  const ActionEnum _action;
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