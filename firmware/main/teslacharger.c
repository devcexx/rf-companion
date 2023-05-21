#include "teslacharger.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include <stdint.h>
#include <unistd.h>

#define TAG "Tesla Charger"

// Ref: https://github.com/rgerganov/tesla-opener
#define TESLA_CHARGER_BIT_RATE_SECOND 2500
#define TESLA_CHARGER_SIGNAL_PERIOD_US (1000000 / TESLA_CHARGER_BIT_RATE_SECOND)
#define TESLA_CHARGER_DISTANCE_BETWEEN_REPETITIONS_US 23000
#define TESLA_CHARGER_NUM_REPETITIONS 5

// https://github.com/fredilarsen/TeslaChargeDoorOpener/blob/master/TeslaChargeDoorOpener.ino
static const uint8_t tesla_charger_door_payload[] = {
    0x02, 0xAA, 0xAA, 0xAA, // Preamble of 26 bits by repeating 1010
    0x2B,                   // Sync byte
    0x2C, 0xCB, 0x33, 0x33, 0x2D, 0x34, 0xB5, 0x2B, 0x4D, 0x32,
    0xAD, 0x2C, 0x56, 0x59, 0x96, 0x66, 0x66, 0x5A, 0x69, 0x6A,
    0x56, 0x9A, 0x65, 0x5A, 0x58, 0xAC, 0xB3, 0x2C, 0xCC, 0xCC,
    0xB4, 0xD2, 0xD4, 0xAD, 0x34, 0xCA, 0xB4, 0xA0};

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
