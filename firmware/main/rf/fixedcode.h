#ifndef FIXEDCODE_H
#define FIXEDCODE_H

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "rf.h"

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
#define FIXEDCODE_ENABLE_CC1101_SUPPORT
#include "cc1101.h"
#endif

extern const rf_antenna_tx_generator_t fixedcode_generator;

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
} fixedcode_port_state_t;

typedef struct {
  const char* tx_name;
  const rf_antenna_tx_generator_t* _generator;

  uint32_t bit_rate;
  const uint8_t* data;
  size_t code_len;
  uint32_t repetitions;
  uint32_t ticks_between_repetitions;
} fixedcode_tx_t;

void tesla_charger_open_door_sync(gpio_num_t gpio);
esp_err_t fixedcode_init_tx(rf_antenna_tx_t* tx);

#ifdef TESLA_CHARGER_ENABLE_CC1101_SUPPORT
esp_err_t tesla_charger_cc1101_open_door(cc1101_device_t *device, gpio_num_t gpio);
#endif
#endif
