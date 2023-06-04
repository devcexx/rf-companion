#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdint.h>
#include <unistd.h>
#include "driver/gpio.h"
#include "clemsacode.h"
#include "driver/gptimer.h"
#include "hal/gpio_types.h"
#include "private.h"

#define TAG "clemsa_code"

// repetition is base 0!
static uint32_t get_code_repetition_begin_cycle(uint32_t repetition, size_t code_len) {
  return CLEMSA_CODEGEN_SYNC_CLOCK_CYCLES +
    CLEMSA_CODEGEN_WAIT_CLOCK_CYCLES +
    (code_len + CLEMSA_CODEGEN_CYCLES_BETWEEN_REPETITIONS) * repetition;
}

static void clemsa_codegen_cleanup_transmission(void* arg, uint32_t _ignored) {
  struct clemsa_codegen_tx* tx = (struct clemsa_codegen_tx*) arg;
  tx->_terminated = true;
  tx->_generator->busy = false;

  gptimer_stop(tx->_generator->_base_clk);

  if (tx->_ask_running) {
    gptimer_stop(tx->_generator->_ask_clk);
  }


  gptimer_disable(tx->_generator->_base_clk);
  gptimer_disable(tx->_generator->_ask_clk);
  gpio_set_level(tx->_generator->gpio, 0);
  ESP_LOGI(TAG, "Transmission of code %s finished", tx->code_name);

  if (tx != NULL) {
    tx->_generator->done_callback(tx);
  }
}

static IRAM_ATTR void clemsa_codegen_base_clk_fall(struct clemsa_codegen_tx* tx) {
  if (tx->_ask_running) {
    tx->_ask_running = false;
    gptimer_stop(tx->_generator->_ask_clk);
  }
  gpio_set_level(tx->_generator->gpio, 0);
  tx->_base_clk_cycles++;
}

static IRAM_ATTR void clemsa_codegen_base_clk_raise(struct clemsa_codegen_tx* tx) {
  if (tx->_base_clk_cycles < CLEMSA_CODEGEN_SYNC_CLOCK_CYCLES) {
    // Stage 1: Sending synchronization signal
    gpio_set_level(tx->_generator->gpio, 1);
  } else if (tx->_base_clk_cycles >= tx->_next_code_repetition_start_cycle) {
    // Stage 2: Sending the code. The code will be sent probably
    // multiple times, starting at a specific clock cycle, defined by
    // the length of the previous sent repetitions and the values of
    // CLEMSA_CODEGEN_WAIT_CLOCK_CYCLES and
    // CLEMSA_CODEGEN_CYCLES_BETWEEN_REPETITIONS
    if (tx->_next_digit >= tx->code_len) {
      // Transmission of the code is done. Check if we need to send
      // more repetitions, and schedule them.
      tx->_times_code_sent++;
      if (tx->_times_code_sent >= tx->repetition_count) {
	// No more repetitions to send. Terminate.
	gpio_set_level(tx->_generator->gpio, 0);
	if (!tx->_terminated) {
	  tx->_terminated = true;
	  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
	  xTimerPendFunctionCallFromISR
	    (clemsa_codegen_cleanup_transmission,
	     (void*) tx,
	     0,
	     &xHigherPriorityTaskWoken);
	  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
	}
      } else {
	// More repetitions avaiable. Schedule them.
	tx->_next_digit = 0;
	tx->_next_code_repetition_start_cycle = get_code_repetition_begin_cycle(tx->_times_code_sent, tx->code_len);
      }
    } else {
      // Send the next digit of the code
      bool next_digit = tx->code[tx->_next_digit++];

      if (next_digit) {
	tx->_remaining_ask_ticks = CLEMSA_CODEGEN_ASK_TICKS_ONE;
      } else {
	tx->_remaining_ask_ticks = CLEMSA_CODEGEN_ASK_TICKS_ZERO;
      }

      tx->_ask_clk_high = false;
      gptimer_set_raw_count(tx->_generator->_ask_clk, 0);

      tx->_ask_running = true;
      gptimer_start(tx->_generator->_ask_clk);
    }
  } else {
    // We're probably waiting to the next repetition to happen, so we
    // just set the port to zero and keep going.
    gpio_set_level(tx->_generator->gpio, 0);
  }
}

static IRAM_ATTR bool clemsa_codegen_base_clk_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* arg) {
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

static IRAM_ATTR bool clemsa_codegen_ask_clk_tick(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* arg) {
  portDISABLE_INTERRUPTS();
  struct clemsa_codegen_tx* tx = (struct clemsa_codegen_tx*) arg;
  if (tx->_remaining_ask_ticks <= 0) {
    gpio_set_level(tx->_generator->gpio, 0);
  } else {
    tx->_remaining_ask_ticks--;
    tx->_ask_clk_high = !tx->_ask_clk_high;
    gpio_set_level(tx->_generator->gpio, tx->_ask_clk_high);
  }
  portENABLE_INTERRUPTS();
  return false;
}

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
esp_err_t clemsa_codegen_init(struct clemsa_codegen *ptr, cc1101_device_t* device) {
#else
esp_err_t clemsa_codegen_init(struct clemsa_codegen *ptr, gpio_num_t gpio) {
#endif
  int err;

  #ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
  ptr->cc1101_device = device;
  #else
  ptr->gpio = gpio;
  gpio_reset_pin(gpio);
  gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
  gpio_set_pull_mode(gpio, GPIO_PULLDOWN_ONLY);

  gptimer_config_t base_clk_cfg = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = CLEMSA_CODEGEN_BASE_CLK_RESOLUTION
  };

  gptimer_config_t ask_clk_cfg = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT,
    .direction = GPTIMER_COUNT_UP,
    .resolution_hz = CLEMSA_CODEGEN_ASK_CLK_FREQUENCY
  };

  if ((err = gptimer_new_timer(&base_clk_cfg, &ptr->_base_clk)) != ESP_OK) {
    return err;
  }

  if ((err = gptimer_new_timer(&ask_clk_cfg, &ptr->_ask_clk)) != ESP_OK) {
    return err;
  }

  #endif

  return ESP_OK;
}

esp_err_t clemsa_codegen_begin_tx(struct clemsa_codegen* generator, struct clemsa_codegen_tx* tx) {
  esp_err_t err;

  generator->busy = true;
  tx->_generator = generator;
  tx->_next_digit = 0;
  tx->_base_clk_high = false;
  tx->_base_clk_cycles = 0;
  tx->_next_code_repetition_start_cycle = get_code_repetition_begin_cycle(0, tx->code_len);
  tx->_times_code_sent = 0;
  tx->_terminated = false;

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
#else
  tx->_ask_running = false;
  gptimer_alarm_config_t base_clk_alarm_config = {
    .alarm_count = CLEMSA_CODEGEN_CLK_HIGH_COUNT,
    .flags.auto_reload_on_alarm = false,
  };

  gptimer_event_callbacks_t base_clk_callback = {
    .on_alarm = clemsa_codegen_base_clk_tick
  };

  gptimer_alarm_config_t ask_clk_alarm_config = {
    .alarm_count = 1,
    .flags.auto_reload_on_alarm = true,
  };

  gptimer_event_callbacks_t ask_clk_callback = {
    .on_alarm = clemsa_codegen_ask_clk_tick
  };

  // Init Base Clock
  if ((err = gptimer_set_alarm_action(generator->_base_clk, &base_clk_alarm_config)) != ESP_OK) {
    return err;
  }

  if ((err = gptimer_register_event_callbacks(generator->_base_clk, &base_clk_callback, (void*)tx)) != ESP_OK) {
    return err;
  }

  if ((err = gptimer_enable(generator->_base_clk)) != ESP_OK) {
    return err;
  }

  // Init ASK
  if ((err = gptimer_set_alarm_action(generator->_ask_clk, &ask_clk_alarm_config)) != ESP_OK) {
    return err;
  }

  if ((err = gptimer_register_event_callbacks(generator->_ask_clk, &ask_clk_callback, (void*)tx)) != ESP_OK) {
    return err;
  }

  if ((err = gptimer_enable(generator->_ask_clk)) != ESP_OK) {
    return err;
  }

  gptimer_set_raw_count(generator->_base_clk, 0);
  gpio_set_level(generator->gpio, 0);
  if ((err = gptimer_start(generator->_base_clk)) != ESP_OK) {
    return err;
  }


  return ESP_OK;
#endif // CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
}

bool clemsa_codegen_tx_finished(struct clemsa_codegen_tx *tx) {
  return tx->_terminated;
}
