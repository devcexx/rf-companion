#ifndef RFAPP_H
#define RFAPP_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <stdint.h>
#include "esp_log.h"
#include "../bt/rfble.h"

#define RF_GPIO GPIO_NUM_43
#define PAIRING_BUTTON_GPIO GPIO_NUM_15
#define PAIRING_BUTTON_MICROS (3 * 1000000)

#define RF_APP_TAG "RF Companion"
#define RF_LOGE(format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_ERROR,   RF_APP_TAG, format, ##__VA_ARGS__)
#define RF_LOGW(format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_WARN,    RF_APP_TAG, format, ##__VA_ARGS__)
#define RF_LOGI(format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_INFO,    RF_APP_TAG, format, ##__VA_ARGS__)
#define RF_LOGD(format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_DEBUG,   RF_APP_TAG, format, ##__VA_ARGS__)
#define RF_LOGV(format, ... ) ESP_LOG_LEVEL_LOCAL(ESP_LOG_VERBOSE, RF_APP_TAG, format, ##__VA_ARGS__)

#define STATIC_TASK_STACK_SUFFIX _stack
#define DECL_STATIC_TASK(task, stacksize)                                      \
  size_t task_##task##_stack_size = stacksize;                                 \
  StackType_t task_##task##_stack[stacksize];                                  \
  StaticTask_t task_##task##_storage;

#define DECL_STATIC_QUEUE(queue, itemsize, itemcount)                          \
  size_t queue_##queue##_item_size = itemsize;                                 \
  size_t queue_##queue##_max_item_count = itemcount;                           \
  uint8_t queue_##queue##_storage[itemsize * itemcount];                       \
  StaticQueue_t queue_##queue##_holder;

/**
 * Set to true if the device is in pairing mode.
 */
extern bool pairing_mode;

/**
 * Set to true when the device is in pairing mode and the reset button
 * has been pressed and it is waiting the user to release the button to reboot.
 */
extern bool ready_to_reboot;

typedef enum {
  STORED_SIGNAL_HOME_GARAGE_EXIT = 1,
  STORED_SIGNAL_HOME_GARAGE_ENTER = 2
} rf_stored_signal_t;

struct rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

extern struct rgb COLOR_RED;
extern struct rgb COLOR_BLUE;
extern struct rgb COLOR_GREEN;
extern struct rgb COLOR_CYAN;

void init_status_led(void);
void status_led_color(uint8_t r, uint8_t g, uint8_t b);
static void status_led_off() {
  status_led_color(0, 0, 0);
}
static void status_led_color_rgb(struct rgb* rgb) {
  status_led_color(rgb->r, rgb->g, rgb->b);
}

void app_pairing_mode_main(void);
void app_rf_main(void);

void init_nvs(void);

bool rf_antenna_is_busy();
void rf_antenna_set_busy(bool value);

int rf_companion_bt_read_chr_cb(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id);
int rf_companion_bt_write_chr_cb(struct ble_gatt_access_ctxt *ctxt,
                                 const ble_uuid_t *chr_id);

void init_clemsa_codegen();
void rf_begin_send_stored_signal(rf_stored_signal_t signal);

void rf_companion_main_task();

void init_pairing_mode_button();
bool pairing_mode_button_state();

#endif
