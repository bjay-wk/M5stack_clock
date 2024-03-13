#pragma once

#include <driver/uart.h>
#include <stdint.h>

#include <string>

/**! Structure holding Plantower's standard packet **/
typedef struct PMSAQIdata {
  uint16_t framelen;       ///< How long this data chunk is
  uint16_t pm10_standard;  ///< Standard PM1.0
  uint16_t pm25_standard;  ///< Standard PM2.5
  uint16_t pm100_standard; ///< Standard PM10.0
  uint16_t pm10_env;       ///< Environmental PM1.0
  uint16_t pm25_env;       ///< Environmental PM2.5
  uint16_t pm100_env;      ///< Environmental PM10.0
  uint16_t particles_03um; ///< 0.3um Particle Count
  uint16_t particles_05um; ///< 0.5um Particle Count

  /**! Structure holding Plantower's standard packet **/
  uint16_t particles_10um;  ///< 1.0um Particle Count
  uint16_t particles_25um;  ///< 2.5um Particle Count
  uint16_t particles_50um;  ///< 5.0um Particle Count
  uint16_t particles_100um; ///< 10.0um Particle Count
  uint16_t unused;          ///< Unused
  uint16_t checksum;        ///< Packet checksum
} PM25_AQI_Data;

/*!
 *  @brief  Class that stores state and functions for interacting with
 *          PM2.5 Air Quality Sensor
 */
class PM25 {
public:
  PM25(uart_port_t uart_numint, int tx_io_num = 17, int rx_io_num = 16,
       int rts_io_num = 14, int cts_io_num = 15);
  ~PM25();

  /**
   * @brief try to read once PM25 buffer, and save it into output
   * @param output
   * @return Success
   * @note
   *      the default condition of heater is disabled
   */
  bool read(PM25_AQI_Data *output);

  /**
   * @brief try to read once PM25 buffer
   *
   * @param data object handle of shd3x
   * @return Success
   * @note
   *      the default condition of heater is disabled
   */
  bool read_task(void *arg);

  /**
   * @brief copy data save by read task
   * @param output
   * @return Success
   */
  bool get(PM25_AQI_Data *output);

  /**
   * @brief convert _pm25data to pretty print
   * @return void
   */
  void print_desc();

private:
  PMSAQIdata _pm25data;
  uart_port_t _uart_num;
  bool read_data(bool single_threaded);
};