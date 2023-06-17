#include "cc1101.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "clemsacode.h"
#include "driver/gptimer.h"
#include "hal/gpio_types.h"
#include "rf.h"
#include "rf_priv.h"

#define TAG "clemsa_code"

// repetition is base 0!
static IRAM_ATTR uint32_t get_code_repetition_begin_cycle(uint32_t repetition, size_t code_len) {
  return CLEMSA_CODEGEN_SYNC_CLOCK_CYCLES +
    CLEMSA_CODEGEN_WAIT_CLOCK_CYCLES +
    (code_len + CLEMSA_CODEGEN_CYCLES_BETWEEN_REPETITIONS) * repetition;
}

static inline IRAM_ATTR void clemsa_codegen_ask_clk_enable(clemsa_port_state_t* state, bool enable) {
#ifdef CLEMSA_CODEGEN_ENABLE_CC1101_SUPPORT
  state->_ask_running = enable;
#else
  if (enable) {
    if (!state->_ask_running) {
      state->_ask_running = true;
      gptimer_set_raw_count(state->_ask_clk, 0);
      gptimer_start(state->_ask_clk);
    }
  } else if (state->_ask_running) {
    gptimer_stop(state->_ask_clk);
    state->_ask_running = false;
  }
#endif
}

static void clemsa_codegen_cleanup_transmission(void* arg, uint32_t _ignored) {
  rf_antenna_port_t* port = (rf_antenna_port_t*)arg;

#ifndef CLEMSA_CODEGEN_ENABLE_CC1101_SUPPORT
  clemsa_port_state_t* state = (clemsa_port_state_t*)port->state;
  gptimer_stop(state->_base_clk);

  clemsa_codegen_ask_clk_enable(state, false);
  gptimer_disable(state->_base_clk);
  gptimer_disable(state->_ask_clk);
#endif

  rf_antenna_write(port, 0);
  rf_antenna_set_free(port);
}

static IRAM_ATTR void clemsa_codegen_base_clk_fall(rf_antenna_port_t* port, clemsa_port_state_t* state) {
  clemsa_codegen_ask_clk_enable(state, false);

  rf_antenna_write(port, 0);
  state->_base_clk_cycles++;
}

static IRAM_ATTR void clemsa_codegen_base_clk_raise(rf_antenna_port_t* port, clemsa_port_state_t* state, const clemsa_codegen_tx_t* tx) {
  if (state->_base_clk_cycles < CLEMSA_CODEGEN_SYNC_CLOCK_CYCLES) {
    // Stage 1: Sending synchronization signal
    rf_antenna_write(port, 1);
  } else if (state->_base_clk_cycles >= state->_next_code_repetition_start_cycle) {
    // Stage 2: Sending the code. The code will be sent probably
    // multiple times, starting at a specific clock cycle, defined by
    // the length of the previous sent repetitions and the values of
    // CLEMSA_CODEGEN_WAIT_CLOCK_CYCLES and
    // CLEMSA_CODEGEN_CYCLES_BETWEEN_REPETITIONS
    if (state->_next_digit >= tx->code_len) {
      // Transmission of the code is done. Check if we need to send
      // more repetitions, and schedule them.
      state->_times_code_sent++;
      if (state->_times_code_sent >= tx->repetition_count) {
	// No more repetitions to send. Terminate.
	rf_antenna_write(port, 0);
	if (!state->_terminated) {
	  state->_terminated = true;
	  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	  xTimerPendFunctionCallFromISR
	    (clemsa_codegen_cleanup_transmission,
	     (void*) port,
	     0,
	     &xHigherPriorityTaskWoken);
	  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
      } else {
	// More repetitions avaiable. Schedule them.
	state->_next_digit = 0;
	state->_next_code_repetition_start_cycle = get_code_repetition_begin_cycle(state->_times_code_sent, tx->code_len);
      }
    } else {
      // Send the next digit of the code
      bool next_digit = tx->code[state->_next_digit++];

      if (next_digit) {
	state->_remaining_ask_ticks = CLEMSA_CODEGEN_ASK_TICKS_ONE;
      } else {
	state->_remaining_ask_ticks = CLEMSA_CODEGEN_ASK_TICKS_ZERO;
      }

      state->_ask_clk_high = false;
      clemsa_codegen_ask_clk_enable(state, true);
    }
  } else {
    // We're probably waiting to the next repetition to happen, so we
    // just set the port to zero and keep going.
    rf_antenna_write(port, 0);
  }
}

static IRAM_ATTR void clemsa_codegen_ask_clk_tick(rf_antenna_port_t* port, clemsa_port_state_t* state, const clemsa_codegen_tx_t* tx) {
  if (state->_remaining_ask_ticks <= 0) {
    rf_antenna_write(port, 0);
  } else {
    state->_remaining_ask_ticks--;
    state->_ask_clk_high = !state->_ask_clk_high;
    rf_antenna_write(port, state->_ask_clk_high);
  }
}

#ifdef CLEMSA_CODEGEN_ENABLE_CC1101_SUPPORT
static IRAM_ATTR void clemsa_codegen_cc1101_clk_isr(const cc1101_device_t* device, void* arg) {
  portDISABLE_INTERRUPTS();

  rf_antenna_port_t* port = (rf_antenna_port_t*) arg;
  clemsa_port_state_t* state = (clemsa_port_state_t*) port->state;
  const clemsa_codegen_tx_t* tx = (const clemsa_codegen_tx_t*) port->_sending_tx;

  if (state->_ask_running) {
    clemsa_codegen_ask_clk_tick(port, state, tx);
  }

  if (state->_ask_clk_high) {
    if (state->_cc1101_ticks == state->_cc1101_base_clk_next_fall) {
      state->_ask_clk_high = false;
      clemsa_codegen_base_clk_fall(port, state);
    }
  } else {
    if (state->_cc1101_ticks % 50 == 0) {
      state->_ask_clk_high = true;
      state->_cc1101_base_clk_next_fall = state->_cc1101_ticks + 31;
      clemsa_codegen_base_clk_raise(port, state, tx);
    }
  }
  state->_cc1101_ticks++;
  portENABLE_INTERRUPTS();
}
#else
static IRAM_ATTR bool clemsa_codegen_base_clk_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* arg) {
  portDISABLE_INTERRUPTS();
  struct clemsa_codegen_tx* tx = (struct clemsa_codegen_tx*) arg;

  if (tx->_base_clk_high) {
    // Currently on the high section of the pulse, we need to low the
    // signal. The alarm of the clock is set at
    // CLEMSA_CODEGEN_CLK_HIGH_COUNT. So, for keeping the signal low
    // for CLEMSA_CODEGEN_CLK_LOW_COUNT, we need to set the counter
    // back to CLEMSA_CODEGEN_CLK_HIGH_COUNT -
    // CLEMSA_CODEGEN_CLK_LOW_COUNT
    gptimer_set_raw_count(tx->_generator->_base_clk, CLEMSA_CODEGEN_CLK_HIGH_COUNT - CLEMSA_CODEGEN_CLK_LOW_COUNT);
    clemsa_codegen_base_clk_fall(tx);
  } else {
    // Reset to zero
    gptimer_set_raw_count(timer, 0);
    clemsa_codegen_base_clk_raise(tx);
  }

  // Reload the timer
  gptimer_alarm_config_t alarm_config = {
    .alarm_count = edata->alarm_value
  };
  gptimer_set_alarm_action(timer, &alarm_config);
  tx->_base_clk_high = !tx->_base_clk_high;
  portENABLE_INTERRUPTS();
  return false;
}
static IRAM_ATTR bool clemsa_codegen_ask_clk_isr(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* arg) {
  portDISABLE_INTERRUPTS();
  clemsa_codegen_ask_clk_tick((struct clemsa_codegen_tx*) arg);
  portENABLE_INTERRUPTS();
  return false;
}

#endif

/*********************/
/* TX INITIALIZATION */
/*********************/

#ifdef CLEMSA_CODEGEN_ENABLE_CC1101_SUPPORT
static esp_err_t clemsa_codegen_begin_tx_internal(rf_antenna_port_t *port, clemsa_port_state_t* state, clemsa_codegen_tx_t* tx) {
  esp_err_t err;

  cc1101_sync_mode_cfg_t sync_mode_cfg = {
    .clk_cb = clemsa_codegen_cc1101_clk_isr,
    .user = port
  };

  // A frequency of 16000 will lead to a real frequency of 16017
  // because of rounding. Preferring using a frequency slightly slower
  // so that final pulses are a little bit slower than 16 kHz.
  if ((err = cc1101_set_data_rate(port->cc1101, 15968)) != ESP_OK) {
    return err;
  }
  if ((err = cc1101_configure_sync_mode(port->cc1101, &sync_mode_cfg)) != ESP_OK) {
    return err;
  }

  if ((err = cc1101_enable_tx(port->cc1101, CC1101_TRANS_MODE_SYNCHRONOUS)) != ESP_OK) {
    return err;
  }
  rf_antenna_write(port, 0);
  return ESP_OK;
}
#else
static esp_err_t clemsa_codegen_begin_tx_internal(rf_antenna_port_t *port, clemsa_port_state_t* state, clemsa_codegen_tx_t* tx) {
  // TODO!
  /*   gptimer_config_t base_clk_cfg = { */
  /*   .clk_src = GPTIMER_CLK_SRC_DEFAULT, */
  /*   .direction = GPTIMER_COUNT_UP, */
  /*   .resolution_hz = CLEMSA_CODEGEN_BASE_CLK_RESOLUTION */
  /* }; */

  /* gptimer_config_t ask_clk_cfg = { */
  /*   .clk_src = GPTIMER_CLK_SRC_DEFAULT, */
  /*   .direction = GPTIMER_COUNT_UP, */
  /*   .resolution_hz = CLEMSA_CODEGEN_ASK_CLK_FREQUENCY */
  /* }; */

  /* if ((err = gptimer_new_timer(&base_clk_cfg, &ptr->_base_clk)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* if ((err = gptimer_new_timer(&ask_clk_cfg, &ptr->_ask_clk)) != ESP_OK) { */
  /*   gptimer_del_timer(ptr->_ask_clk); */
  /*   return err; */
  /* } */
  /*   gptimer_alarm_config_t base_clk_alarm_config = { */
  /*   .alarm_count = CLEMSA_CODEGEN_CLK_HIGH_COUNT, */
  /*   .flags.auto_reload_on_alarm = false, */
  /* }; */

  /* gptimer_event_callbacks_t base_clk_callback = { */
  /*   .on_alarm = clemsa_codegen_base_clk_isr */
  /* }; */

  /* gptimer_alarm_config_t ask_clk_alarm_config = { */
  /*   .alarm_count = 1, */
  /*   .flags.auto_reload_on_alarm = true, */
  /* }; */

  /* gptimer_event_callbacks_t ask_clk_callback = { */
  /*   .on_alarm = clemsa_codegen_ask_clk_isr */
  /* }; */

  /* // Init Base Clock */
  /* if ((err = gptimer_set_alarm_action(generator->_base_clk, &base_clk_alarm_config)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* if ((err = gptimer_register_event_callbacks(generator->_base_clk, &base_clk_callback, (void*)tx)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* if ((err = gptimer_enable(generator->_base_clk)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* // Init ASK */
  /* if ((err = gptimer_set_alarm_action(generator->_ask_clk, &ask_clk_alarm_config)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* if ((err = gptimer_register_event_callbacks(generator->_ask_clk, &ask_clk_callback, (void*)tx)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* if ((err = gptimer_enable(generator->_ask_clk)) != ESP_OK) { */
  /*   return err; */
  /* } */

  /* gptimer_set_raw_count(generator->_base_clk, 0); */
  /* gpio_set_level(generator->gpio, 0); */
  /* if ((err = gptimer_start(generator->_base_clk)) != ESP_OK) { */
  /*   return err; */
  /* } */
}
#endif

static esp_err_t clemsa_codegen_begin_tx(rf_antenna_port_t *port, rf_antenna_tx_t* rf_tx) {
  clemsa_port_state_t* state = (clemsa_port_state_t*)port->state;
  clemsa_codegen_tx_t* tx = (clemsa_codegen_tx_t*) rf_tx;

  memset(port->state, 0, sizeof(clemsa_port_state_t));
  state->_next_code_repetition_start_cycle = get_code_repetition_begin_cycle(0, tx->code_len);

  ESP_LOGI(TAG, "Begin TX %p %p %p", port, rf_tx, port->_sending_tx);
  return clemsa_codegen_begin_tx_internal(port, state, tx);
}

const rf_antenna_tx_generator_t clemsa_generator = {
  .tx_begin_func = clemsa_codegen_begin_tx
};
