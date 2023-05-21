#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "rfapp.h"
#include "esp_console.h"
#include <stdbool.h>
#include <stdint.h>

DECL_STATIC_TASK(pairing_led, 4096);
DECL_STATIC_QUEUE(numcmp_confirm, sizeof(bool), 1);

QueueHandle_t queue_numcmp_confirm_handle;

volatile bool passkey_pending_numcmp = false;

#define CONSOLE_MAX_LINE_LEN 256
void task_pairing_led(void* arg) {
  struct rgb colors[] = {COLOR_BLUE, COLOR_RED};
  int next = 0;

  while (!ready_to_reboot) {
    status_led_color_rgb(&colors[next++ % (sizeof(colors) / sizeof(struct rgb))]);
    vTaskDelay(300 / portTICK_PERIOD_MS);
  }

  vTaskDelete(NULL);
}

static void write_passkey_numcmp_result(bool accepted) {
  passkey_pending_numcmp = false;
  xQueueSend(queue_numcmp_confirm_handle, &accepted, 0);
}

static bool ensure_pairing_key_request_pending() {
  if (!passkey_pending_numcmp) {
    RF_LOGW("There's no pending pairing request");
  }

  return passkey_pending_numcmp;
}

static int cmd_accept(int argc, char **argv) {
  if (!ensure_pairing_key_request_pending()) {
    return 0;
  }

  write_passkey_numcmp_result(true);
  return 0;
}

static int cmd_decline(int argc, char** argv) {
  if (!ensure_pairing_key_request_pending()) {
    return 0;
  }

  write_passkey_numcmp_result(false);
  return 0;
}

static void register_commands() {
  esp_console_cmd_t accept_cmd = {
    .command = "accept",
    .help = "Accept incoming pairing request.",
    .func = &cmd_accept
  };

  esp_console_cmd_t decline_cmd = {
    .command = "decline",
    .help = "Decline incoming pairing request.",
    .func = &cmd_decline
  };

  esp_console_cmd_register(&accept_cmd);
  esp_console_cmd_register(&decline_cmd);

  esp_console_register_help_command();
}

static bool passkey_numcmp_cb(uint16_t conn_handle, uint32_t key) {
  struct ble_gap_conn_desc desc;
  bool accept;

  if (ble_gap_conn_find(conn_handle, &desc) != 0) {
    RF_LOGW("Number comparison aborted since connection is not available anymore");
    return false;
  }

  uint8_t* addr = desc.peer_id_addr.val;
  RF_LOGI("============================================================================================");
  RF_LOGI("Device (%02x:%02x:%02x:%02x:%02x:%02x) is attempting to pair.",
	  addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  RF_LOGI("Check that this key matches on the remote device: %" PRIu32, key);
  RF_LOGI("Use the command 'accept' or 'decline' to proceed or cancel the pairing request respectively.");
  RF_LOGI("============================================================================================");

  passkey_pending_numcmp = true;
  if (xQueueReceive(queue_numcmp_confirm_handle, &accept, 30000 / portTICK_PERIOD_MS) == pdTRUE) {
    return accept;
  } else {
    RF_LOGW("Pairing operation timeout. Operation aborted");
    return false;
  }
}

void app_pairing_mode_main(void) {
  RF_LOGI("Enter pairing mode...");
  pairing_mode = true;

  xTaskCreateStaticPinnedToCore
    (
     task_pairing_led,
     "Pairing Led Task",
     task_pairing_led_stack_size,
     NULL,
     tskIDLE_PRIORITY,
     task_pairing_led_stack,
     &task_pairing_led_storage,
     0);

  queue_numcmp_confirm_handle = xQueueCreateStatic
    (queue_numcmp_confirm_max_item_count,
     queue_numcmp_confirm_item_size,
     queue_numcmp_confirm_storage,
     &queue_numcmp_confirm_holder);
  rfble_opts_t ble_opts = {
    .device_name = "RF Companion",
    .discovery_mode = RF_BLE_DISC_GENERAL,
    .allow_device_pairing = true,
    .io_cap = RF_BLE_IO_CAP_KEYBOARD_DISPLAY,
    .pair_req_numcmp_cb = passkey_numcmp_cb,
    .gatt_read_cb = rf_companion_bt_read_chr_cb,
    .gatt_write_cb = rf_companion_bt_write_chr_cb
  };

  rfble_begin(&ble_opts);

  esp_console_repl_t* repl;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();

  repl_config.prompt = "rfapp >";
  repl_config.history_save_path = NULL;
  repl_config.max_cmdline_length = CONSOLE_MAX_LINE_LEN;

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));

#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));

#else
#error Unsupported console type
#endif

    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    rf_companion_main_task();
}
