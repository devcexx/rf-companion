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
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#define TAG "fixedcode"

static inline IRAM_ATTR bool fixedcode_cur_repetition_terminated(rf_fixedcode_tx_t* tx) {
  return tx->_port->_next_bit >= tx->code_len * 8;
}


static inline IRAM_ATTR void fixedcode_write(rf_fixedcode_port_t* port, uint32_t level) {
  #ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
  cc1101_trans_continuous_write(port->device, level);
  #else
  gpio_set_level(port->gpio, level);
  #endif
}

static void fixedcode_cleanup_transmission(void* arg, uint32_t _ignored) {
  rf_fixedcode_tx_t* tx = (rf_fixedcode_tx_t*) arg;
#ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
  fixedcode_write(tx->_port, 0);
  vTaskDelay(10); // Give time to the CC1101 to finish sending everything.
  cc1101_set_idle(tx->_port->device);
#endif

  ESP_LOGI(TAG, "Transmission of fixedcode completed");
}


static IRAM_ATTR void fixedcode_tick(rf_fixedcode_tx_t* tx) {
  rf_fixedcode_port_t* port = tx->_port;
  if (port->_terminated) {
    return;
  }

  if (port->_cur_tick < port->_next_repetition_start) {
    fixedcode_write(port, 0);
  } else if (fixedcode_cur_repetition_terminated(tx)) {
    fixedcode_write(port, 0);

    port->_cur_repetition++;
    if (port->_cur_repetition >= tx->repetitions) {
      port->_terminated = true;
      BaseType_t xHigherPriorityTaskWoken = pdFALSE;
      xTimerPendFunctionCallFromISR
	(fixedcode_cleanup_transmission,
	 (void*) tx,
	 0,
	 &xHigherPriorityTaskWoken);
      portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
      port->_next_repetition_start = port->_cur_tick + tx->ticks_between_repetitions;
      port->_next_bit = 0;
    }
  } else {
    uint32_t next_bit_idx = port->_next_bit;

    uint32_t next_bit_level = (tx->data[next_bit_idx / 8] >> (7 - (next_bit_idx % 8))) & 1;
    fixedcode_write(port, next_bit_level);
    port->_next_bit++;
  }
  port->_cur_tick++;

}

#ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
static IRAM_ATTR void fixedcode_cc1101_isr(const cc1101_device_t* device, void* arg) {
  rf_fixedcode_tx_t* tx = (rf_fixedcode_tx_t*) arg;
  fixedcode_tick(tx);

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

// Ref: https://github.com/rgerganov/tesla-opener
#define TESLA_CHARGER_BIT_RATE_SECOND 2500
#define TESLA_CHARGER_SIGNAL_PERIOD_US (1000000 / TESLA_CHARGER_BIT_RATE_SECOND)
#define TESLA_CHARGER_DISTANCE_BETWEEN_REPETITIONS_US 23000
#define TESLA_CHARGER_NUM_REPETITIONS 5

// https://github.com/fredilarsen/TeslaChargeDoorOpener/blob/master/TeslaChargeDoorOpener.ino
const uint8_t tesla_charger_door_payload[43] = {
    0x02, 0xAA, 0xAA, 0xAA, // Preamble of 26 bits by repeating 1010
    0x2B,                   // Sync byte
    0x2C, 0xCB, 0x33, 0x33, 0x2D, 0x34, 0xB5, 0x2B, 0x4D, 0x32,
    0xAD, 0x2C, 0x56, 0x59, 0x96, 0x66, 0x66, 0x5A, 0x69, 0x6A,
    0x56, 0x9A, 0x65, 0x5A, 0x58, 0xAC, 0xB3, 0x2C, 0xCC, 0xCC,
    0xB4, 0xD2, 0xD4, 0xAD, 0x34, 0xCA, 0xB4, 0xA0};

// Testing
/* const uint8_t tesla_charger_door_payload[43] = { */
/*     0b11001101, 0xAA, 0xAA, 0xAA, // Preamble of 26 bits by repeating 1010 */
/*     0xAA,                   // Sync byte */
/*     0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, */
/*     0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, */
/*     0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, */
/*     0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0b11001101}; */

void active_wait_us(int micros) {
  int64_t begin = esp_timer_get_time();
  while (esp_timer_get_time() - begin < micros);
}


void tesla_charger_send_byte(gpio_num_t gpio, uint8_t byte) {
  for (int i = 7; i >= 0; i--) {
    gpio_set_level(gpio, ((byte >> i) & 0x1));
    active_wait_us(TESLA_CHARGER_SIGNAL_PERIOD_US);
  }
}

// This function needs the Watchdog to have a higher timeout! (around 2 seconds will be fine)
void tesla_charger_open_door_sync(gpio_num_t gpio) {

  ESP_LOGI(TAG, "Sending Tesla Charger Door open signal...");
  portDISABLE_INTERRUPTS();
  for (int rep = 0; rep < TESLA_CHARGER_NUM_REPETITIONS; rep++) {
    for (int i = 0; i < (sizeof(tesla_charger_door_payload) / sizeof(uint8_t)); i++) {
      tesla_charger_send_byte(gpio, tesla_charger_door_payload[i]);
    }
    active_wait_us(TESLA_CHARGER_DISTANCE_BETWEEN_REPETITIONS_US);
  }
  portENABLE_INTERRUPTS();
  ESP_LOGI(TAG, "Done");
}

esp_err_t fixedcode_init_tx(rf_fixedcode_port_t* port, rf_fixedcode_tx_t* tx) {
  esp_err_t err;

  tx->_port = port;
  port->_cur_repetition = 0;
  port->_next_repetition_start = 0;
  port->_cur_tick = 0;
  port->_next_bit = 0;
  port->_terminated = false;

#ifdef FIXEDCODE_ENABLE_CC1101_SUPPORT
  cc1101_sync_mode_cfg_t sync_mode_cfg = {
    .clk_cb = fixedcode_cc1101_isr,
    .user = tx
  };

  ESP_LOGI(TAG, "%" PRIu32, tx->bit_rate);
  if ((err = cc1101_set_data_rate(port->device, tx->bit_rate)) != ESP_OK) {
    return err;
  }

  if ((err = cc1101_configure_sync_mode(port->device, &sync_mode_cfg)) != ESP_OK) {
    return err;
  }

  if ((err = cc1101_enable_tx(port->device, CC1101_TRANS_MODE_SYNCHRONOUS)) != ESP_OK) {
    return err;
  }
  fixedcode_write(port, 0);
#endif

  return ESP_OK;
}
