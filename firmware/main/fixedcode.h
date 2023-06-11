#ifndef FIXEDCODE_H
#define FIXEDCODE_H

#include "driver/gpio.h"
#include "driver/gptimer.h"

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
#define FIXEDCODE_ENABLE_CC1101_SUPPORT
#include "cc1101.h"
#endif

extern const uint8_t tesla_charger_door_payload[43];

typedef struct {
#ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
  cc1101_device_t* device;
#else
  gptimer_handle_t clk;
  gpio_num_t gpio;
#endif

  size_t _cur_tick;
  size_t _next_bit;
  size_t _cur_repetition;
  size_t _next_repetition_start;
  bool _terminated;
} rf_fixedcode_port_t;

typedef struct {
  uint32_t bit_rate;
  const uint8_t* data;
  size_t code_len;
  uint32_t repetitions;
  uint32_t ticks_between_repetitions;

  rf_fixedcode_port_t* _port;
} rf_fixedcode_tx_t;

void tesla_charger_open_door_sync(gpio_num_t gpio);
esp_err_t fixedcode_init_tx(rf_fixedcode_port_t* port, rf_fixedcode_tx_t* tx);

#ifdef TESLA_CHARGER_ENABLE_CC1101_SUPPORT
esp_err_t tesla_charger_cc1101_open_door(cc1101_device_t *device, gpio_num_t gpio);


#endif
#endif
