#include "pm25.hpp"

#include <esp_check.h>

#include <pthread.h>
#include <string.h>

#define BUF_SIZE (129)
static pthread_mutex_t spiffsMutex;
const char TAG[] = "PM25";

PM25::PM25(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num,
           int cts_io_num)
    : _uart_num(uart_num) {
  uart_config_t uart_config = {
      .baud_rate = 9600,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
      .rx_flow_ctrl_thresh = 122,
      .source_clk = UART_SCLK_DEFAULT,
  };
  int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
  intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

  ESP_ERROR_CHECK(uart_driver_install(_uart_num, BUF_SIZE * 2, 0, 0, NULL,
                                      intr_alloc_flags));
  ESP_ERROR_CHECK(uart_param_config(_uart_num, &uart_config));
  ESP_ERROR_CHECK(
      uart_set_pin(_uart_num, tx_io_num, rx_io_num, rts_io_num, cts_io_num));
}

PM25::~PM25() { uart_driver_delete(_uart_num); }

bool PM25::get(PM25_AQI_Data *data) {
  if (!data) {
    return false;
  }
  if (pthread_mutex_lock(&spiffsMutex) == 0) {

    memcpy(data, &_pm25data, sizeof(_pm25data));
    pthread_mutex_unlock(&spiffsMutex);
  }
  return true;
}

bool PM25::read(PM25_AQI_Data *data) {
  if (!data) {
    return false;
  }
  if (read_data(true)) {
    memcpy(data, &_pm25data, sizeof(_pm25data));
    return true;
  }
  return false;
}

bool PM25::read_task(void *arg) {
  if (pthread_mutex_init(&spiffsMutex, NULL) != 0) {
    ESP_LOGE(TAG, "Failed to initialize the spiffs mutex");
  }
  while (1) {
    read_data(false);
  }
  return true;
}

bool PM25::read_data(bool single_threaded) {
  uint8_t _readbuffer[128];
  int length = 0;
  ESP_ERROR_CHECK(uart_get_buffered_data_len(_uart_num, (size_t *)&length));
  length = uart_read_bytes(_uart_num, _readbuffer, length, 100);
  if (length < 32)
    return false;
  int i = 0;
  while (i < length && _readbuffer[i] != 0x42) {
    ++i;
  }
  if (_readbuffer[i] == 0x42) {
    if (length - i < 32) {
      return false;
    }
    uint16_t checksum = 0;
    uint16_t buffer_u16[15];
    for (uint8_t j = 0; j < 30; ++j) {
      checksum += _readbuffer[j + i];
    }
    for (uint8_t j = 0; j < 15; j++) {
      buffer_u16[j] = _readbuffer[i + 2 + j * 2 + 1];
      buffer_u16[j] += (_readbuffer[i + 2 + j * 2] << 8);
    }
    if (checksum != ((PMSAQIdata *)&buffer_u16)->checksum)
      return false;
    if (single_threaded || pthread_mutex_lock(&spiffsMutex) == 0) {
      memcpy((void *)&_pm25data, (void *)buffer_u16, 30);
      if (!single_threaded)
        pthread_mutex_unlock(&spiffsMutex);
    }
    return true;
  }
  return false;
}

void PM25::print_desc() {
  ESP_LOGI(TAG,
           "---------------------------------------\n"
           "Concentration Units (standard)\n"
           "---------------------------------------\n"
           "\t\tPM 1.0: %d μg/m3\n"
           "\t\tPM 2.5: %d μg/m3\n"
           "\t\tPM  10: %d μg/m3\n"
           "Concentration Units (environmental) μg/m3\n"
           "---------------------------------------\n"
           "\t\tPM 1.0: %d μg/m3\n"
           "\t\tPM 2.5: %d μg/m3\n"
           "\t\tPM  10: %d μg/m3\n"
           "---------------------------------------\n"
           "Particles > 0.3um / 0.1L air: %d\n"
           "Particles > 0.5um / 0.1L air: %d\n"
           "Particles > 1.0um / 0.1L air: %d\n"
           "Particles > 2.5um / 0.1L air: %d\n"
           "Particles > 5.0um / 0.1L air: %d\n"
           "Particles > 10 um / 0.1L air: %d\n"
           "---------------------------------------\n",
           _pm25data.pm10_standard, _pm25data.pm25_standard,
           _pm25data.pm100_standard, _pm25data.pm10_env, _pm25data.pm25_env,
           _pm25data.pm100_env, _pm25data.particles_03um,
           _pm25data.particles_05um, _pm25data.particles_10um,
           _pm25data.particles_25um, _pm25data.particles_50um,
           _pm25data.particles_100um);
}