#include "rfapp.h"
#include "freertos/portmacro.h"

DECL_STATIC_TASK(activity_led, 4096);

void task_activity_led(void* arg) {
  status_led_color_rgb(&COLOR_GREEN);
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  while (1) {
    status_led_color_rgb(&COLOR_GREEN);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    status_led_off();
    vTaskDelay(60000 / portTICK_PERIOD_MS);
  }

  vTaskDelete(NULL);
}

void app_rf_main(void) {
  RF_LOGI("Initializing app...");
  pairing_mode = false;

  xTaskCreateStaticPinnedToCore
    (
     task_activity_led,
     "Activity Led Task",
     task_activity_led_stack_size,
     NULL,
     tskIDLE_PRIORITY,
     task_activity_led_stack,
     &task_activity_led_storage,
     0);

  rfble_opts_t ble_opts = {
    .device_name = "RF Companion",
    .discovery_mode = RF_BLE_DISC_FILTERED,
    .allow_device_pairing = false,
    .io_cap = RF_BLE_IO_CAP_KEYBOARD_DISPLAY,
    .gatt_read_cb = rf_companion_bt_read_chr_cb,
    .gatt_write_cb = rf_companion_bt_write_chr_cb
  };

  rfble_begin(&ble_opts);

  RF_LOGI("Ready!");
  rf_companion_main_task();
}
