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
} UserContext;

bool bh1750_get(bh1750_handle_t bh1750, float *output);

void sleep_action(void *pvParameter) {
  ESP_LOGI(TAG, "Entering sleep mode");

  /*
  const uint64_t m1 = 1ULL << GPIO_NUM_37;
  const uint64_t m2 = 1ULL << GPIO_NUM_38;
  const uint64_t m3 = 1ULL << GPIO_NUM_39;
  esp_sleep_enable_ext1_wakeup(m1 | m2, ESP_EXT1_WAKEUP_ANY_HIGH);
  ESP_ERROR_CHECK(gpio_pulldown_dis(GPIO_NUM_37));
  ESP_ERROR_CHECK(gpio_pullup_en(GPIO_NUM_37));
  ESP_ERROR_CHECK(gpio_pulldown_dis(GPIO_NUM_38));
  ESP_ERROR_CHECK(gpio_pullup_en(GPIO_NUM_38));
  ESP_ERROR_CHECK(gpio_pulldown_dis(GPIO_NUM_39));
  ESP_ERROR_CHECK(gpio_pullup_en(GPIO_NUM_39));
  */
  gpio_pullup_en(GPIO_NUM_38);
  gpio_pulldown_dis(GPIO_NUM_38);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_38, false);
  esp_deep_sleep_start();
}

void screen_update_cb(void *pvParameter) {
  UserContext *user_ctx = (UserContext *)pvParameter;
  Action *new_action = (Action *)calloc(sizeof(Action), 1);
  new_action->action = UpdateScreen;
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
  UserContext *user_ctx = (UserContext *)pvParameter;
  if (esp_timer_is_active(user_ctx->timers.screen_update)) {
    esp_timer_stop(user_ctx->timers.screen_update);
  }
  Action *new_action = (Action *)calloc(sizeof(Action), 1);
  new_action->action = ScreenOff;
  xQueueSend(user_ctx->actionQueue, (void *)&new_action, (TickType_t)0);
  start_or_restart_timer(user_ctx->timers.sleep, 1 * U_TO_MIN);
}

void update_screen(UserContext *user_ctx) {
  stop_sleep_timer(user_ctx);
  M5.Lcd.wakeup();
  float tem_val, hum_val;
  float lux = 0;
  bh1750_get(user_ctx->light_sensor, &lux);
  if (lux > 5000)
    lux = 5000;
  lux = lux * 255 / 5000;
  if (lux < 1)
    lux = 1;
  M5.Lcd.setBrightness(lux);
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 0);
  char time_buf[6] = {0};
  char format[] = "%H:%M";
  get_time(format, time_buf, sizeof(time_buf));
  ESP_LOGI(TAG, "%s", time_buf);
  M5.Lcd.printf("%s\n", time_buf);
  PMSAQIdata data;
  if (user_ctx->pm25->get(&data)) {
    M5.Lcd.printf("pm25: %d\n", data.pm25_standard);
  }
  if (sht3x_get_humiture(user_ctx->sht3x, &tem_val, &hum_val) == 0) {
    M5.Lcd.printf("temperature %.2f°C\n", tem_val);
    M5.Lcd.printf("humidity:%.2f %%\n", hum_val);
  }

  if (*user_ctx->str_ip) {
    time_t current;
    time(&current);
    struct tm *tm = localtime(&current);
    user_ctx->w->update_weather(user_ctx->geo->latitude(),
                                user_ctx->geo->longitude());
    M5.Lcd.printf(
        "\nWeather:%s\nUV %.2f\n"
        "precipitation:%.2f %%\n"
        "outside temperature %.2f°C\n",
        OM_SDK::EnumNamesWeatherCode(
            user_ctx->w->forecast24.weather_code[tm->tm_hour]),
        user_ctx->w->forecast24.uv_index[tm->tm_hour],
        user_ctx->w->forecast24.precipitation_probability[tm->tm_hour],
        user_ctx->w->forecast24.temperature_2m[tm->tm_hour]);
  }
}

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
  UserContext *user_ctx = (UserContext *)pvParameter;
  bool connected = false;
  Action *action = nullptr;
  init_timers(user_ctx);
  while (1) {
    if (xQueueReceive(user_ctx->actionQueue, &action, (TickType_t)1000)) {
      if (!connected && action->action != WifiConnected &&
          action->action != ApStarted) {
        free(action);
        action = nullptr;
        continue;
      }
      switch (action->action) {
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
        update_screen(user_ctx);
        ESP_LOGI(TAG, "Button");
        update_screen_off_timer(user_ctx);
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
  flatbuffers::FlatBufferBuilder builder;
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
  M5.Power.begin();
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
      Action *new_action = (Action *)calloc(sizeof(Action), 1);
      new_action->action = ButtonClicked;
      new_action->set_value("A");
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }
    if (M5.BtnB.wasClicked()) {
      Action *new_action = (Action *)calloc(sizeof(Action), 1);
      new_action->action = ButtonClicked;
      new_action->set_value("B");
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }
    if (M5.BtnC.wasClicked()) {
      Action *new_action = (Action *)calloc(sizeof(Action), 1);
      new_action->action = ButtonClicked;
      new_action->set_value("C");
      xQueueSend(userContext.actionQueue, (void *)&new_action, 100);
    }

    M5.delay(100);
  }
  bh1750_delete(&bh1750);
  i2c_bus_delete(&i2c_bus);
}