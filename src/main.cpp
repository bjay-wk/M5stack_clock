#include "bh1750.h"
#include "geolocation.hpp"
#include "http_manager.h"
#include "pm25.hpp"
#include "sntp.h"
#include "weather.hpp"
#include "weather_api_generated.h"
#include <M5Unified.h>
#include <driver/rtc_io.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <flatbuffers/flatbuffers.h>
#include <http_app.h>
#include <nvs_flash.h>
#include <sht3x.h>
#include <wifi_manager.h>

#define I2C_MASTER_SCL_IO (gpio_num_t)22
#define I2C_MASTER_SDA_IO (gpio_num_t)21
#define I2C_MASTER_NUM I2C_NUM_1
#define I2C_MASTER_FREQ_HZ 100000

#define U_TO_SEC 1000000
#define U_TO_MIN U_TO_SEC * 60
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

const char TAG[] = "main";

typedef struct Timers {
  esp_timer_handle_t screen_off;
  esp_timer_handle_t sleep;
  esp_timer_handle_t screen_update;

} Timers;

typedef struct UserContext {
  char str_ip[16];
  QueueHandle_t actionQueue;
  Geolocation *geo;
  Timers timers;
  bh1750_handle_t light_sensor;
  PM25 *pm25;
  sht3x_handle_t sht3x;
  Weather *w;

  int _page;
  bool screen_on;
} UserContext;

typedef void (*ScreenUpdateFunc)(UserContext *user_ctx);

void page_main(UserContext *user_ctx);
void page_today(UserContext *user_ctx);
void page_tomorrow(UserContext *user_ctx);
void page_d2(UserContext *user_ctx);
void page_d3(UserContext *user_ctx);
void page_d4(UserContext *user_ctx);
bool bh1750_get(bh1750_handle_t bh1750, float *output);

ScreenUpdateFunc screen_update_func[] = {
    page_main, page_today, page_tomorrow, page_d2, page_d3, page_d4,
};

void change_page(int page, UserContext *user_ctx) {
  user_ctx->_page += page;
  if (user_ctx->_page < 0) {
    user_ctx->_page = (int)(ARRAY_SIZE(screen_update_func)) - 1;
  }
  if (user_ctx->_page >= ARRAY_SIZE(screen_update_func)) {
    user_ctx->_page = 0;
  }
}

void sleep_action(void *pvParameter) {
  ESP_LOGI(TAG, "Entering sleep mode");
  gpio_pullup_en(GPIO_NUM_38);
  gpio_pulldown_dis(GPIO_NUM_38);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_38, false);
  esp_deep_sleep_start();
}

void screen_update_cb(void *pvParameter) {
  UserContext *user_ctx = static_cast<UserContext *>(pvParameter);
  Action *new_action = new Action(UpdateScreen);
  xQueueSend(user_ctx->actionQueue, (void *)&new_action, 100);
}
void stop_sleep_timer(UserContext *user_ctx) {
  if (esp_timer_is_active(user_ctx->timers.sleep))
    ESP_ERROR_CHECK(esp_timer_stop(user_ctx->timers.sleep));
}

void start_or_restart_timer(esp_timer_handle_t timer, uint64_t timeout_us) {
  if (esp_timer_is_active(timer))
    ESP_ERROR_CHECK(esp_timer_restart(timer, timeout_us));
  else
    ESP_ERROR_CHECK(esp_timer_start_once(timer, timeout_us));
}

void update_screen_off_timer(UserContext *user_ctx) {
  start_or_restart_timer(user_ctx->timers.screen_off, 1 * U_TO_MIN);
}

void screen_off(void *pvParameter) {
  ESP_LOGI(TAG, "Screen off");
  UserContext *user_ctx = static_cast<UserContext *>(pvParameter);
  user_ctx->screen_on = false;
  if (esp_timer_is_active(user_ctx->timers.screen_update)) {
    esp_timer_stop(user_ctx->timers.screen_update);
  }
  Action *new_action = new Action(ScreenOff);
  xQueueSend(user_ctx->actionQueue, (void *)&new_action, (TickType_t)0);
  start_or_restart_timer(user_ctx->timers.sleep, 1 * U_TO_MIN);
}

void update_screen(UserContext *user_ctx) {
  stop_sleep_timer(user_ctx);
  user_ctx->screen_on = true;
  M5.Lcd.wakeup();
  float lux = 0;
  bh1750_get(user_ctx->light_sensor, &lux);
  if (lux > 5000)
    lux = 5000;
  lux = lux * 255 / 5000;
  if (lux < 1)
    lux = 1;
  M5.Lcd.setBrightness(lux);
  screen_update_func[user_ctx->_page](user_ctx);
}

void page_main(UserContext *user_ctx) {
  ESP_LOGI(TAG, "Show Main page");
  M5.Lcd.setTextSize(3);
  float tem_val, hum_val;
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  char time_buf[20] = {0};
  char format[] = "%a %D\n%H:%M";
  get_time(format, time_buf, sizeof(time_buf), 0);
  ESP_LOGI(TAG, "%s", time_buf);
  M5.Lcd.printf("%s\n", time_buf);
  PMSAQIdata data;
  M5.Lcd.setTextSize(2);
  if (user_ctx->pm25->get(&data)) {
    M5.Lcd.printf("pm25: %d\n", data.pm25_standard);
  }
  if (sht3x_get_humiture(user_ctx->sht3x, &tem_val, &hum_val) == 0) {
    M5.Lcd.printf("temp: %02.0fC\n"
                  "hum:  %02.0f%%\n",
                  tem_val, hum_val);
  }

  if (*user_ctx->str_ip) {
    time_t now;
    time(&now);
    struct tm tm;
    localtime_r(&now, &tm);
    user_ctx->w->update_weather(user_ctx->geo->latitude(),
                                user_ctx->geo->longitude());
    M5.Lcd.printf("\n%s\n"
                  "UV:   %.1f\n"
                  "rain: %.0f%%\n"
                  "temp: %.0fC\n",
                  OM_SDK::EnumNamesWeatherCode(
                      user_ctx->w->forecast24.weather_code[tm.tm_hour]),
                  user_ctx->w->forecast24.uv_index[tm.tm_hour],
                  user_ctx->w->forecast24.precipitation_probability[tm.tm_hour],
                  user_ctx->w->forecast24.temperature_2m[tm.tm_hour]);
  }
}

void page_today(UserContext *user_ctx) {
  ESP_LOGI(TAG, "Show Today page");
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2.5);
  M5.Lcd.setCursor(0, 0);
  char time_buf[16] = {0};
  char format[] = "%D";
  get_time(format, time_buf, sizeof(time_buf), 0);
  M5.Lcd.printf(
      "Today:\n%s\n%s\n\n", time_buf,
      OM_SDK::EnumNamesWeatherCode(user_ctx->w->forecast7.weather_code[0]));
  if (*user_ctx->str_ip) {
    time_t now;
    time(&now);
    struct tm tm;
    localtime_r(&now, &tm);
    user_ctx->w->update_weather(user_ctx->geo->latitude(),
                                user_ctx->geo->longitude());
    Forecast24 *f24 = &(user_ctx->w->forecast24);
    if (tm.tm_hour == 24) {

      M5.Lcd.printf("      24h\n"
                    "      %s"
                    "rain: %02.0f%%\n"
                    "temp: %02.0fC\n"
                    "UV:   %02.1f\n",
                    OM_SDK::EnumNamesWeatherCode(f24->weather_code[tm.tm_hour]),
                    f24->temperature_2m[tm.tm_hour],
                    f24->precipitation_probability[tm.tm_hour],
                    f24->uv_index[tm.tm_hour]);
    } else {
      int t1 = 8;
      int t2 = 16;
      if (tm.tm_hour == 23 || tm.tm_hour == 22) {
        t1 = 23;
        t2 = 24;
      } else if (tm.tm_hour == 21 || tm.tm_hour == 22) {
        t1 = 22;
        t2 = 24;
      } else if (tm.tm_hour == 20 || tm.tm_hour == 19) {
        t1 = 21;
        t2 = 23;
      } else if (tm.tm_hour == 18 || tm.tm_hour == 17) {
        t1 = 19;
        t2 = 22;
      } else if (tm.tm_hour == 16) {
        t1 = 18;
        t2 = 21;
      } else if (tm.tm_hour == 15 || tm.tm_hour == 14) {
        t1 = 17;
        t2 = 20;
      } else if (tm.tm_hour == 13 || tm.tm_hour == 12) {
        t1 = 16;
        t2 = 20;
      } else if (tm.tm_hour == 11) {
        t1 = 14;
        t2 = 19;
      } else if (tm.tm_hour == 10 || tm.tm_hour == 9) {
        t1 = 13;
        t2 = 18;
      } else if (tm.tm_hour > 6) {
        t1 = 12;
        t2 = 19;
      }

      M5.Lcd.printf("     %dh     %dh\n"
                    "UV:  %02.1f    %.1f\n"
                    "rain:%02.0f%%    %02.0f%%\n"
                    "temp:%02.0fC    %02.0fC\n",
                    t1,
                    t2, /*OM_SDK::EnumNamesWeatherCode(f24->weather_code[t1]),
                OM_SDK::EnumNamesWeatherCode(f24->weather_code[t2]),*/
                    f24->uv_index[t1], f24->uv_index[t2],
                    f24->precipitation_probability[t1],
                    f24->precipitation_probability[t2], f24->temperature_2m[t1],
                    f24->temperature_2m[t2]);
    }
  }
}

void page_tomorrow(UserContext *user_ctx) {
  ESP_LOGI(TAG, "Show Tomorrow page");
  ForecastTmr *f_tmr = &(user_ctx->w->forecast_tmr);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2.5);
  M5.Lcd.setCursor(0, 0);
  char time_buf[16] = {0};
  char format[] = "%a %D";
  get_time(format, time_buf, sizeof(time_buf), 1);
  M5.Lcd.printf("Tomorrow\n%s\n", time_buf);
  M5.Lcd.printf("%s\n\n", OM_SDK::EnumNamesWeatherCode(
                              user_ctx->w->forecast7.weather_code[1]));
  M5.Lcd.printf("     9h     15h\n"
                "UV:  %02.1f    %02.1f\n"
                "rain:%02.0f%%    %02.0f%%\n"
                "temp:%02.0fC    %02.0fC\n",
                /*OM_SDK::EnumNamesWeatherCode(f24->weather_code[t1]),
               OM_SDK::EnumNamesWeatherCode(f24->weather_code[t2]),*/
                f_tmr->uv_index[0], f_tmr->uv_index[1],
                f_tmr->precipitation_probability[0],
                f_tmr->precipitation_probability[1], f_tmr->temperature_2m[0],
                f_tmr->temperature_2m[1]);
}

void page_week(UserContext *user_ctx, int day) {
  ESP_LOGI(TAG, "Show Day %d page", day);
  Forecast7 *f_7 = &(user_ctx->w->forecast7);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextSize(2.6);
  M5.Lcd.setCursor(0, 0);
  char time_buf[16] = {0};
  char format[] = "%a %D";
  get_time(format, time_buf, sizeof(time_buf), day);
  M5.Lcd.printf("%s\n", time_buf);
  M5.Lcd.printf("%s\n\n", OM_SDK::EnumNamesWeatherCode(f_7->weather_code[day]));
  char time_buf_sunset[6] = {0};
  char time_buf_sunrise[6] = {0};
  char format_h[] = "%H:%M";
  struct tm timeinfo;
  localtime_r(&(f_7->sunrise[day]), &timeinfo);
  strftime(time_buf_sunrise, ARRAY_SIZE(time_buf_sunrise), format_h, &timeinfo);
  localtime_r(&(f_7->sunset[day]), &timeinfo);
  strftime(time_buf_sunset, ARRAY_SIZE(time_buf_sunset), format_h, &timeinfo);
  M5.Lcd.printf("UV:      %02.1f \n"
                "rain:    %02.0f%%\n"
                "max:     %02.0fC \n"
                "min:     %02.0fC\n"
                "sunrise: %s\n"
                "sunset:  %s\n",
                f_7->uv_index_max[day], f_7->precipitation_probability_max[day],
                f_7->temperature_2m_max[day], f_7->temperature_2m_min[day],
                time_buf_sunrise, time_buf_sunset

  );
}

void page_d2(UserContext *user_ctx) { page_week(user_ctx, 2); }
void page_d3(UserContext *user_ctx) { page_week(user_ctx, 3); }
void page_d4(UserContext *user_ctx) { page_week(user_ctx, 4); }

void wake_up(UserContext *user_ctx) {
  check_and_update_ntp_time();
  settimezone(user_ctx->geo->posix_tz());
}

void init_timers(UserContext *user_ctx) {
  Timers *timers = &(user_ctx->timers);
  const esp_timer_create_args_t single_timer_args = {
      .callback = &sleep_action,
      .arg = user_ctx,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "sleep_timer",
      .skip_unhandled_events = false};
  ESP_ERROR_CHECK(esp_timer_create(&single_timer_args, &timers->sleep));

  const esp_timer_create_args_t screen_off_timer_args = {
      .callback = &screen_off,
      .arg = user_ctx,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "screen_off",
      .skip_unhandled_events = false};
  ESP_ERROR_CHECK(
      esp_timer_create(&screen_off_timer_args, &timers->screen_off));

  const esp_timer_create_args_t time_timer_args = {
      .callback = &screen_update_cb,
      .arg = user_ctx,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "screen_update",
      .skip_unhandled_events = false};
  ESP_ERROR_CHECK(esp_timer_create(&time_timer_args, &timers->screen_update));
  esp_timer_start_periodic(timers->screen_update, U_TO_SEC * 30);
}

void action_task(void *pvParameter) {
  UserContext *user_ctx = static_cast<UserContext *>(pvParameter);
  bool connected = false;
  Action *action = nullptr;
  init_timers(user_ctx);
  while (1) {
    if (xQueueReceive(user_ctx->actionQueue, &action, (TickType_t)1000)) {
      if (!connected && action->action() != WifiConnected &&
          action->action() != ApStarted) {
        free(action);
        action = nullptr;
        continue;
      }
      switch (action->action()) {
      case UpdateScreen: {
        update_screen(user_ctx);
        break;
      }
      case WifiConnected: {
        connected = true;
        auto wakeup_cause = esp_sleep_get_wakeup_cause();
        if (wakeup_cause != ESP_SLEEP_WAKEUP_EXT1 &&
            wakeup_cause != ESP_SLEEP_WAKEUP_EXT0) {
          user_ctx->geo->update_geoloc();
        }
        wake_up(user_ctx);
        update_screen(user_ctx);
        update_screen_off_timer(user_ctx);
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
        update_screen_off_timer(user_ctx);
        stop_sleep_timer(user_ctx);
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
      case ScreenOff:
        M5.Lcd.fillScreen(BLACK);
        M5.Display.sleep();
        break;
      case ButtonClicked:
        ESP_LOGI(TAG, "Button");
        if (*action->value() == 'A' && user_ctx->screen_on) {
          change_page(-1, user_ctx);
        } else if (*action->value() == 'B') {
        } else if (*action->value() == 'C' && user_ctx->screen_on) {
          change_page(1, user_ctx);
        }
        update_screen(user_ctx);
        update_screen_off_timer(user_ctx);
        break;
      }
      free(action);
      action = nullptr;
    }
  }
}

static esp_err_t wifi_handler(httpd_req_t *req) {
  Action *action = new Action(ScreenOff);
  esp_err_t result = ESP_OK;
  if (strcmp(req->uri, "/") == 0) {
    httpd_resp_set_status(req, "302");
    httpd_resp_set_hdr(req, "Location", "wifi/");
    httpd_resp_send(req, NULL, 0);
  } else {
    ESP_LOGI(TAG, "%s", req->uri);
    httpd_resp_send_404(req);
  }
  UserContext *userContext = static_cast<UserContext *>(req->user_ctx);
  xQueueSend(userContext->actionQueue, (void *)&action, (TickType_t)0);
  return result;
}

void cb_connection_stopped(void *pvParameter, void *user_ctx) {
  UserContext *userContext = static_cast<UserContext *>(user_ctx);
  userContext->str_ip[0] = '\0';
  Action *new_action = new Action(WifiDisconnected);
  xQueueSend(userContext->actionQueue, (void *)&new_action, (TickType_t)100);
}

void cb_connection_ok(void *pvParameter, void *user_ctx) {
  ip_event_got_ip_t *param = static_cast<ip_event_got_ip_t *>(pvParameter);
  UserContext *userContext = static_cast<UserContext *>(user_ctx);
  esp_ip4addr_ntoa(&param->ip_info.ip, userContext->str_ip, IP4ADDR_STRLEN_MAX);
  ESP_LOGI(TAG, "IP: %s", userContext->str_ip);
  Action *new_action = new Action(WifiConnected);
  xQueueSend(userContext->actionQueue, (void *)&new_action, 100);
}

void cb_connection_AP_started(void *pvParameter, void *user_ctx) {
  UserContext *userContext = static_cast<UserContext *>(user_ctx);
  Action *new_action = new Action(ApStarted);
  xQueueSend(userContext->actionQueue, (void *)&new_action, 100);
}

static void i2c_init(i2c_bus_handle_t *i2c_bus, bh1750_handle_t *bh1750,
                     sht3x_handle_t *sht3x) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_MASTER_SDA_IO,
      .scl_io_num = I2C_MASTER_SCL_IO,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master = {.clk_speed = I2C_MASTER_FREQ_HZ},
      .clk_flags = 0,
  };
  *i2c_bus = i2c_bus_create(I2C_MASTER_NUM, &conf);
  *bh1750 = bh1750_create(*i2c_bus, BH1750_I2C_ADDRESS_DEFAULT);
  *sht3x = sht3x_create(*i2c_bus, SHT3x_ADDR_PIN_SELECT_VSS);
  sht3x_heater(*sht3x, SHT3x_HEATER_DISABLED);
  sht3x_set_measure_mode(*sht3x, SHT3x_PER_2_MEDIUM);
}

bool bh1750_get(bh1750_handle_t bh1750, float *output) {
  bh1750_power_on(bh1750);
  bh1750_set_measure_mode(bh1750, BH1750_ONETIME_4LX_RES);
  vTaskDelay(30 / portTICK_RATE_MS);
  auto ret = bh1750_get_data(bh1750, output);
  bh1750_power_down(bh1750);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "No ack, sensor not connected...\n");
  }
  return ret == ESP_OK;
}

extern "C" void app_main(void) {
  i2c_bus_handle_t i2c_bus = nullptr;
  bh1750_handle_t bh1750 = nullptr;
  sht3x_handle_t sht3x = nullptr;
  i2c_init(&i2c_bus, &bh1750, &sht3x);

  esp_err_t err = nvs_flash_init();

  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated and needs to be erased
    // Retry nvs_flash_init
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  M5.begin();
  M5.Lcd.setBrightness(50);
  M5.Lcd.setTextSize(1.5);
  Geolocation geo;
  UserContext userContext = {
      .str_ip = "",
      .actionQueue = xQueueCreate(10, sizeof(struct Action *)),
      .geo = &geo,
      .timers = {0, 0, 0},
      .light_sensor = bh1750,
      .pm25 = new PM25(UART_NUM_2),
      .sht3x = sht3x,
      .w = new Weather(),
      ._page = 0,
      .screen_on = true,
  };
  auto wakeup_cause = esp_sleep_get_wakeup_cause();
  if (wakeup_cause == ESP_SLEEP_WAKEUP_EXT1 ||
      wakeup_cause == ESP_SLEEP_WAKEUP_EXT0) {
    settimezone(userContext.geo->posix_tz());
    update_screen(&userContext);
  } else {
    M5.Lcd.print("Power On");
  }

  ESP_LOGI(TAG, "POWERON");
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

  while (1) {
    M5.update();
    if (M5.BtnA.wasClicked()) {
      Action *new_action = new Action(ButtonClicked, "A");
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }
    if (M5.BtnB.wasClicked()) {
      Action *new_action = new Action(ButtonClicked, "B");
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }
    if (M5.BtnC.wasClicked()) {
      Action *new_action = new Action(ButtonClicked, "C");
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }

    M5.delay(100);
  }
  bh1750_delete(&bh1750);
  i2c_bus_delete(&i2c_bus);
}