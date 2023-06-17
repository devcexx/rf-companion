#ifndef RFAPP_H
#define RFAPP_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include <stdint.h>
#include "esp_log.h"
#include "../bt/rfble.h"

#define PAIRING_BUTTON_MICROS (3 * 1000000)

// Make this match your workbench!
#if CONFIG_RFAPP_TARGET_ESP32S3_LOLIN_MINI
#define STATUS_LED_GPIO 47
#define RF_ANTENNA_GPIO GPIO_NUM_18
#define PAIRING_BUTTON_GPIO GPIO_NUM_16

#else

#define STATUS_LED_GPIO 48
#define RF_ANTENNA_GPIO GPIO_NUM_18
#define PAIRING_BUTTON_GPIO GPIO_NUM_11
#endif

#ifdef CONFIG_RFAPP_DEVO_MODE
#define RF_COMPANION_DEVICE_NAME "RF Comp Devo"
#else
#define RF_COMPANION_DEVICE_NAME "RF Companion"
#endif

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
#define CC1101_MISO_GPIO GPIO_NUM_13
#define CC1101_MOSI_GPIO GPIO_NUM_11
#define CC1101_SCLK_GPIO GPIO_NUM_12
#define CC1101_CSN_GPIO GPIO_NUM_2
#define CC1101_GDO2_GPIO GPIO_NUM_4
#define CC1101_GDO0_GPIO GPIO_NUM_10
#endif


/** The NVS namespace used by the application */
#define RF_APP_NVS_NS "rfapp"

/** The key in NVS use to determine the next init mode for the app (see RF_APP_INIT_*)*/
#define RF_APP_INIT_MODE_KEY "rfapp-init-mode"

/** Indicates no specific boot mode: it will defined by the current status of the pairing button */
#define RF_APP_INIT_HW_DEFINED 0

/** Indicates that the device will be forced to go into default mode */
#define RF_APP_INIT_DEFAULT_MODE 1

/** Indicates that the device will be forced to go into pairing mode */
#define RF_APP_INIT_PAIRING_MODE 2

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

struct rgb {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

extern struct rgb COLOR_RED;
extern struct rgb COLOR_BLUE;
extern struct rgb COLOR_GREEN;
extern struct rgb COLOR_CYAN;

void init_antenna(void);

void init_status_led(void);
void status_led_color(uint8_t r, uint8_t g, uint8_t b);
void status_led_off(void);
void status_led_color_rgb(struct rgb *rgb);

uint8_t rf_app_get_next_boot_mode();
void rf_app_set_next_boot_mode(uint8_t mode);
void rf_app_clear_next_boot_mode();

void app_pairing_mode_main(void);
void app_rf_main(void);

void init_nvs(void);

int rf_companion_bt_read_chr_cb(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id);
int rf_companion_bt_write_chr_cb(struct ble_gatt_access_ctxt *ctxt,
                                 const ble_uuid_t *chr_id);

void rf_companion_main_task();

void init_pairing_mode_button();
bool pairing_mode_button_state();

#endif
