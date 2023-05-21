#include "esp_err.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "rfapp.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CONSOLE_MAX_LINE_LEN 256

DECL_STATIC_TASK(pairing_led, 4096);
DECL_STATIC_QUEUE(numcmp_confirm, sizeof(bool), 1);

QueueHandle_t queue_numcmp_confirm_handle;
volatile bool passkey_pending_numcmp = false;

struct {
  struct arg_str* action;
  struct arg_str* device;
  struct arg_end* end;
} devs_cmd_args;

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
    printf("There's no pending pairing request\n");
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

static int cmd_exit(int argc, char** argv) {
  rf_app_set_next_boot_mode(RF_APP_INIT_DEFAULT_MODE);
  esp_restart();
  return 0;
}

static bool parse_peer_addr(const char* in, ble_addr_t* addr) {
  char eof = 0;
  if (sscanf(in, RFBLE_ADDR_FMT "%c",
	     &addr->val[5],
	     &addr->val[4],
	     &addr->val[3],
	     &addr->val[2],
	     &addr->val[1],
	     &addr->val[0], &eof) != 6 || eof != 0) {
    return false;
  }

  return true;
}

static int cmd_devs(int argc, char** argv) {
  int nerrors = arg_parse(argc, argv, (void**) &devs_cmd_args);
  if (nerrors != 0) {
    arg_print_errors(stderr, devs_cmd_args.end, argv[0]);
    return 1;
  }

  const char* action = devs_cmd_args.action->sval[0];
  const char* device = devs_cmd_args.device->sval[0];
  ble_addr_t peers[RFBLE_MAX_KNOWN_DEVICES];
  ble_addr_t deleting_peer;
  int rc;
  int num_peers;

  bool unused_device;
  if (strcmp("", action) == 0) {
    // List devices
    unused_device = true;
    rc = ble_store_util_bonded_peers(peers, &num_peers, RFBLE_MAX_KNOWN_DEVICES);
    if (rc != 0) {
      printf("Unable to enumerate bonded devices: %d\n", rc);
      return 0;
    }

    printf("List of bonded devices:\n");
    for (int i = 0; i < num_peers; i++) {
      printf(" - " RFBLE_ADDR_FMT "\n", RFBLE_ADDR_FMT_PARAMS(peers[i]));
    }
    printf("\n");
  } else if (strcmp("delete", action) == 0 || strcmp("remove", action) == 0) {
    unused_device = false;
    if (!parse_peer_addr(device, &deleting_peer)) {
      printf("Invalid bluetooth address specified.\n");
      return 0;
    }

    if ((rc = ble_store_util_delete_peer(&deleting_peer)) == 0) {
      printf("Device removed from bonded list\n");
    } else {
      printf("Error removing peer: %d.\n", rc);
    }
  } else if (strcmp("clear", action) == 0) {
    unused_device = true;
    if ((rc = ble_store_clear()) == 0) {
      printf("Cleared list of bonded devices\n");
    } else {
      printf("Error clearing list of bonded devices: %d.\n", rc);
    }
  } else {
    printf("Invalid action specified: %s\n", action);
    return 0;
  }

  if (unused_device && strcmp("", device) != 0) {
    printf("Warning: unused device parameter.\n");
  }

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

  esp_console_cmd_t exit_cmd = {
    .command = "exit",
    .help = "Exits from the pairing mode",
    .func = &cmd_exit
  };

  devs_cmd_args.action = arg_str0(NULL, NULL, "<action>", "action");
  devs_cmd_args.device = arg_str0(NULL, NULL, "<dev>", "bluetooth address");
  devs_cmd_args.end = arg_end(2);

  esp_console_cmd_t devices_cmd = {
    .command = "devs",
    .help = "Manages paired devices. Usage:\n"
    "devs               - List all paired devices.\n"
    "devs delete <addr> - Removes device from paired device list.\n"
    "devs clear         - Clears the whole list of paired devices\n",
    .func = &cmd_devs,
    .argtable = &devs_cmd_args
  };



  esp_console_cmd_register(&accept_cmd);
  esp_console_cmd_register(&decline_cmd);
  esp_console_cmd_register(&devices_cmd);
  esp_console_cmd_register(&exit_cmd);

  esp_console_register_help_command();
}

static bool passkey_numcmp_cb(uint16_t conn_handle, uint32_t key) {
  struct ble_gap_conn_desc desc;
  bool accept;

  if (ble_gap_conn_find(conn_handle, &desc) != 0) {
    RF_LOGW("Number comparison aborted since connection is not available anymore");
    return false;
  }

  RF_LOGI("============================================================================================");
  RF_LOGI("Device " RFBLE_ADDR_FMT " is attempting to pair.", RFBLE_ADDR_FMT_PARAMS(desc.peer_id_addr));
  RF_LOGI("Check that this key matches on the remote device: %" PRIu32, key);
  RF_LOGI("Use the command 'accept' or 'decline' to proceed or cancel the pairing request respectively.");
  RF_LOGI("============================================================================================");

#ifdef CONFIG_RFAPP_DEVO_MODE
  RF_LOGW("Automatic accepting peer " RFBLE_ADDR_FMT " since development mode is enabled.", RFBLE_ADDR_FMT_PARAMS(desc.peer_id_addr));
  accept = true;
  return accept;
#else
  passkey_pending_numcmp = true;
  if (xQueueReceive(queue_numcmp_confirm_handle, &accept, 30000 / portTICK_PERIOD_MS) == pdTRUE) {
    return accept;
  } else {
    RF_LOGW("Pairing operation timeout. Operation aborted");
    return false;
  }
#endif
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
    .device_name = RF_COMPANION_DEVICE_NAME,
    .discovery_mode = RFBLE_DISC_GENERAL,
    .allow_device_pairing = true,
    .io_cap = RFBLE_IO_CAP_KEYBOARD_DISPLAY,
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

    RF_LOGI("You're in the RF Companion pairing mode. Pair your device with the ESP32 now.");
    RF_LOGI("You may find the ESP32 under the name of '" RF_COMPANION_DEVICE_NAME "'");
    RF_LOGI("Make sure your device supports BLE 4.2+!");
    RF_LOGI("");

    register_commands();
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    rf_companion_main_task();
}
