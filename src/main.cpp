#include "geolocation.hpp"
#include "http_manager.h"
#include "open_meteo.hpp"
#include "sntp.h"
#include "weather_api_generated.h"
#include <M5Unified.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <esp_wifi.h>
#include <flatbuffers/flatbuffers.h>
#include <http_app.h>
#include <nvs_flash.h>
#include <wifi_manager.h>

const char TAG[] = "main";

typedef struct UserContext {
  char str_ip[16];
  QueueHandle_t actionQueue;
} UserContext;

void do_nothing(void *pvParameter) {
  UserContext *user_ctx = (UserContext *)pvParameter;
  Action *new_action = (Action *)calloc(sizeof(Action), 1);
  new_action->action = DoNothing;
  xQueueSend(user_ctx->actionQueue, (void *)&new_action, (TickType_t)0);
}

void sleep_action(void *pvParameter) {}

void start_or_restart_timer(esp_timer_handle_t single_timer,
                            uint64_t timeout_us) {
  if (esp_timer_is_active(single_timer))
    ESP_ERROR_CHECK(esp_timer_restart(single_timer, timeout_us));
  else
    ESP_ERROR_CHECK(esp_timer_start_once(single_timer, timeout_us));
}

void action_task(void *pvParameter) {
  UserContext *user_ctx = (UserContext *)pvParameter;
  bool connected = false;
  Action *action = nullptr;
  const esp_timer_create_args_t single_timer_args = {
      .callback = &sleep_action,
      .arg = pvParameter,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "sleep_timer",
      .skip_unhandled_events = false};

  esp_timer_handle_t single_timer;
  ESP_ERROR_CHECK(esp_timer_create(&single_timer_args, &single_timer));

  const esp_timer_create_args_t do_nothing_timer_args = {
      .callback = &do_nothing,
      .arg = pvParameter,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "do_nothing",
      .skip_unhandled_events = false};
  esp_timer_handle_t do_nothing_timer;
  ESP_ERROR_CHECK(esp_timer_create(&do_nothing_timer_args, &do_nothing_timer));

  while (1) {
    if (xQueueReceive(user_ctx->actionQueue, &action, (TickType_t)1000)) {
      if (!connected && action->action != WifiConnected &&
          action->action != ApStarted) {
        free(action);
        action = nullptr;
        continue;
      }
      if (action->action == ApStarted) {
        start_or_restart_timer(single_timer, 10 * 60000000);
      } else {
        start_or_restart_timer(single_timer, 5 * 60000000);
      }
      switch (action->action) {
      case WifiConnected: {
        connected = true;
        check_and_update_ntp_time();
        char time_buf[6] = {0};
        char format[] = "%H:%M";

        M5.Lcd.wakeup();
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.printf("Wifi Connected\nIP:\n%s\n", user_ctx->str_ip);
        ESP_LOGI(TAG, "Wifi Connected");
        get_time(format, time_buf, sizeof(time_buf));
        M5.Lcd.printf("%s\n", time_buf);
        ESP_LOGI(TAG, "sntp time: %s", time_buf);
        auto test = Geolocation();
        test.update_geoloc();
        ESP_LOGI(TAG, "city: %s", test.city());
        settimezone(test.posix_tz());
        start_or_restart_timer(do_nothing_timer, 3000000);
        time_t now;
        time(&now);
        OM_SDK::TimeParam daily[] = {OM_SDK::temperature_2m,
                                     OM_SDK::max_params};
        OM_SDK::OpenMeteoParams p{
            .latitude = test.latitude(),
            .longitude = test.longitude(),
            .current = daily,
            .forecast_days = 4,
        };
        //OM_SDK::get_weather(&p);
        break;
      }
      case WifiDisconnected:
        connected = false;
        M5.Lcd.wakeup();
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.printf("Wifi Disconnected");
        ESP_LOGI(TAG, "Wifi Disconnected");
        M5.Lcd.fillScreen(BLACK);
        start_or_restart_timer(do_nothing_timer, 3000000);
        break;
      case ApStarted:
        M5.Lcd.wakeup();
        M5.Lcd.fillScreen(BLACK);
        M5.Lcd.setCursor(0, 0);
        M5.Lcd.printf(
            "Ap Started\nSSID:\n %s\nPassword:\n %s\nurl:\n http://%s",
            DEFAULT_AP_SSID, DEFAULT_AP_PASSWORD, DEFAULT_AP_IP);
        ESP_LOGI(TAG, "Ap Started");
        break;
      case DoNothing:
        M5.Lcd.fillScreen(BLACK);
        M5.Display.sleep();
        break;
      }
      free(action);
      action = nullptr;
    }
  }
}

static esp_err_t wifi_handler(httpd_req_t *req) {
  Action *action = (Action *)calloc(sizeof(Action), 1);
  esp_err_t result = ESP_OK;
  if (strcmp(req->uri, "/") == 0) {
    httpd_resp_set_status(req, "302");
    httpd_resp_set_hdr(req, "Location", "wifi/");
    httpd_resp_send(req, NULL, 0);
  } else {
    ESP_LOGI(TAG, "%s", req->uri);
    httpd_resp_send_404(req);
  }
  UserContext *userContext = (UserContext *)req->user_ctx;
  xQueueSend(userContext->actionQueue, (void *)&action, (TickType_t)0);
  return result;
}

void cb_connection_stopped(void *pvParameter, void *user_ctx) {
  UserContext *userContext = (UserContext *)user_ctx;
  userContext->str_ip[0] = '\0';
  Action *new_action = (Action *)calloc(sizeof(Action), 1);
  new_action->action = WifiDisconnected;
  xQueueSend(userContext->actionQueue, (void *)&new_action, (TickType_t)100);
}

void cb_connection_ok(void *pvParameter, void *user_ctx) {
  ip_event_got_ip_t *param = (ip_event_got_ip_t *)pvParameter;
  UserContext *userContext = (UserContext *)user_ctx;
  esp_ip4addr_ntoa(&param->ip_info.ip, userContext->str_ip, IP4ADDR_STRLEN_MAX);
  ESP_LOGI(TAG, "IP: %s", userContext->str_ip);
  Action *new_action = (Action *)calloc(sizeof(Action), 1);
  new_action->action = WifiConnected;
  xQueueSend(userContext->actionQueue, (void *)&new_action, 100);
}

void cb_connection_AP_started(void *pvParameter, void *user_ctx) {
  UserContext *userContext = (UserContext *)user_ctx;
  Action *new_action = (Action *)calloc(sizeof(Action), 1);
  new_action->action = ApStarted;
  xQueueSend(userContext->actionQueue, (void *)&new_action, 100);
}

extern "C" void app_main(void) {
  flatbuffers::FlatBufferBuilder builder;
  esp_err_t err = nvs_flash_init();

  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  M5.begin();
  M5.Power.begin();
  M5.Lcd.setBrightness(50);
  M5.Lcd.setTextSize(1.5);
  M5.Lcd.print("Power On");
  ESP_LOGI(TAG, "POWERON");

  UserContext userContext = {
      .str_ip = "",
      .actionQueue = xQueueCreate(10, sizeof(struct Action *)),
  };
  wifi_manager_start(&userContext);
  esp_netif_set_hostname(wifi_manager_get_esp_netif_ap(), "esp-32-finger-ap");
  esp_netif_set_hostname(wifi_manager_get_esp_netif_sta(), "esp-32-finger-sta");
  wifi_manager_set_callback(WM_ORDER_START_HTTP_SERVER, NULL);
  wifi_manager_set_callback(WM_ORDER_STOP_HTTP_SERVER, NULL);
  wifi_manager_set_callback(WM_ORDER_START_DNS_SERVICE, NULL);
  wifi_manager_set_callback(WM_ORDER_STOP_DNS_SERVICE, NULL);
  wifi_manager_set_callback(WM_ORDER_START_WIFI_SCAN, NULL);
  wifi_manager_set_callback(WM_ORDER_LOAD_AND_RESTORE_STA, NULL);
  wifi_manager_set_callback(WM_ORDER_CONNECT_STA, NULL);
  wifi_manager_set_callback(WM_ORDER_DISCONNECT_STA, NULL);
  wifi_manager_set_callback(WM_ORDER_START_AP, &cb_connection_AP_started);
  wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_connection_stopped);
  wifi_manager_set_callback(WM_EVENT_SCAN_DONE, NULL);
  wifi_manager_set_callback(WM_EVENT_STA_GOT_IP, &cb_connection_ok);
  wifi_manager_set_callback(WM_ORDER_STOP_AP, NULL);
  wifi_manager_set_callback(WM_MESSAGE_CODE_COUNT, NULL);
  http_app_set_handler_hook(HTTP_GET, &wifi_handler);

  xTaskCreate(&action_task, "action_task", 8192, &userContext, 5, nullptr);
  // fingerMng.fingerprint->fpm_setAddMode(0x34);
  // ESP_LOGI(TAG, "IDLE yes");

  while (1) {
    M5.update();
    if (M5.BtnA.wasClicked()) {
      Action *new_action = (Action *)calloc(sizeof(Action), 1);
      new_action->action = DoNothing;
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }

    M5.delay(100);
  }
}