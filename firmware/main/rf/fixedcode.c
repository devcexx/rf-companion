#include "cc1101.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "fixedcode.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "rf.h"
#include "rf_priv.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define TAG "fixedcode"

static inline IRAM_ATTR bool fixedcode_cur_repetition_terminated(const fixedcode_tx_t* tx, const fixedcode_port_state_t* state) {
  return state->_next_bit >= tx->code_len * 8;
}

static void fixedcode_cleanup_transmission(void* arg, uint32_t _ignored) {
  rf_antenna_port_t* port = (rf_antenna_port_t*) arg;
  rf_antenna_write(port, 0);
  rf_antenna_set_free(port);

  ESP_LOGI(TAG, "Transmission of fixedcode completed");
}


static IRAM_ATTR void fixedcode_tick(rf_antenna_port_t* port, const fixedcode_tx_t* tx, fixedcode_port_state_t* state) {
  if (state->_terminated) {
    return;
  }

  if (state->_cur_tick < state->_next_repetition_start) {
    rf_antenna_write(port, 0);
  } else if (fixedcode_cur_repetition_terminated(tx, state)) {
    rf_antenna_write(port, 0);

    state->_cur_repetition++;
    if (state->_cur_repetition >= tx->repetitions) {
      state->_terminated = true;
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTimerPendFunctionCallFromISR
	(fixedcode_cleanup_transmission,
	 (void*) port,
	 0,
	 &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
      state->_next_repetition_start = state->_cur_tick + tx->ticks_between_repetitions;
      state->_next_bit = 0;
    }
  } else {
    uint32_t next_bit_idx = state->_next_bit;

    uint32_t next_bit_level = (tx->data[next_bit_idx / 8] >> (7 - (next_bit_idx % 8))) & 1;
    rf_antenna_write(port, next_bit_level);
    state->_next_bit++;
  }
  state->_cur_tick++;
}

#ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
static IRAM_ATTR void fixedcode_cc1101_isr(const cc1101_device_t* device, void* arg) {
  rf_antenna_port_t* port = (rf_antenna_port_t*) arg;
  fixedcode_tx_t* tx = (fixedcode_tx_t*) port->_sending_tx;
  fixedcode_port_state_t* state = (fixedcode_port_state_t*)port->state;
  fixedcode_tick(port, tx, state);
}
#else
static IRAM_ATTR bool fixedcode_clk_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* arg) {
  rf_fixedcode_tx_t* tx = (rf_fixedcode_tx_t*) arg;
  fixedcode_tick(tx);

  if (tx->_port->_next_bit * 8 == tx->code_len) {
    gptimer_stop(timer);
    gptimer_disable(timer);
    ESP_EARLY_LOGI(TAG, "Transmission terminated");
  }
}
#endif

static esp_err_t fixedcode_begin_tx(rf_antenna_port_t* port, rf_antenna_tx_t* rf_tx) {
#ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
  fixedcode_tx_t* tx = (fixedcode_tx_t*) rf_tx;
  esp_err_t err;
  cc1101_sync_mode_cfg_t sync_mode_cfg = {
    .clk_cb = fixedcode_cc1101_isr,
    .user = port
  };
  memset(port->state, 0, sizeof(fixedcode_port_state_t));

  if ((err = cc1101_set_data_rate(port->cc1101, tx->bit_rate)) != ESP_OK) {
    return err;
  }

  if ((err = cc1101_configure_sync_mode(port->cc1101, &sync_mode_cfg)) != ESP_OK) {
    return err;
  }

  if ((err = cc1101_enable_tx(port->cc1101, CC1101_TRANS_MODE_SYNCHRONOUS)) != ESP_OK) {
    return err;
  }
  rf_antenna_write(port, 0);
#endif

  return ESP_OK;
}

const rf_antenna_tx_generator_t fixedcode_generator = {
  .tx_begin_func = fixedcode_begin_tx
};

esp_err_t fixedcode_init_tx(rf_antenna_tx_t* tx) {
  tx->_generator = &fixedcode_generator;
  return ESP_OK;
}
