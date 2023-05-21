#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/portmacro.h"
#include "hal/gpio_types.h"
#include "host/ble_uuid.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "rfapp.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include <stdint.h>
#include "../clemsacode.h"
#include "../private.h"

#if CONFIG_RFAPP_TARGET_ESP32S3_LOLIN_MINI
#define STATUS_LED_GPIO 47
#else
#define STATUS_LED_GPIO 48
#endif

struct rgb COLOR_RED = {.r = 255, .g = 0, .b = 0};
struct rgb COLOR_BLUE = {.r = 0, .g = 0, .b = 255};
struct rgb COLOR_GREEN = {.r = 0, .g = 255, .b = 0};
struct rgb COLOR_CYAN = {.r = 0, .g = 255, .b = 255};

static led_strip_handle_t status_led;
static bool antenna_busy = false;

static struct clemsa_codegen generator = {0};
static struct clemsa_codegen_tx tx = {0};

bool pairing_mode;
bool ready_to_reboot = false;

DECL_STATIC_QUEUE(tx_start, sizeof(bool), 1);
QueueHandle_t queue_tx_start_handle;

void init_status_led(void) {
  led_strip_config_t strip_config = {
    .strip_gpio_num = STATUS_LED_GPIO,
    .max_leds = 1,
  };

  led_strip_rmt_config_t rmt_config = {
    .resolution_hz = 10 * 1000 * 1000
  };

  ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &status_led));
  led_strip_clear(status_led);
}

void status_led_color(uint8_t r, uint8_t g, uint8_t b) {
#if CONFIG_RFAPP_TARGET_ESP32S3_LOLIN_MINI
    led_strip_set_pixel(status_led, 0, g, r, b);
#else
    led_strip_set_pixel(status_led, 0, r, g, b);
#endif
  led_strip_refresh(status_led);
}

void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}


bool rf_antenna_is_busy() {
  return antenna_busy;
}
void rf_antenna_set_busy(bool value) {
  if (antenna_busy != value && rfble_is_connected()) {
    int rc = rfble_gatt_notif8(rf_ble_state.conn_handle, value);
    if (rc != 0) {
      RF_LOGE("Failed to notify antenna state change: %d", rc);
    }
  }
  antenna_busy = value;
}

static void clemsa_codegen_tx_cb(struct clemsa_codegen_tx* tx) {
  rf_antenna_set_busy(false);
}

// Task that will initiate the transmission from the CPU 1, so
// interrupts are also handled by CPU 1 and can run without weird
// stuff interferring on them.
static void transmission_initiator_task(void* arg) {
  bool recv;

  while (1) {
    xQueueReceive(queue_tx_start_handle, &recv, portMAX_DELAY);
    ESP_ERROR_CHECK(clemsa_codegen_begin_tx(&generator, &tx));
    RF_LOGI("Transmission initiated");
  }
}

void init_clemsa_codegen() {
  queue_tx_start_handle = xQueueCreateStatic
    (queue_tx_start_max_item_count,
     queue_tx_start_item_size,
     queue_tx_start_storage,
     &queue_tx_start_holder);

  ESP_ERROR_CHECK(clemsa_codegen_init(&generator, RF_GPIO));

  generator.done_callback = clemsa_codegen_tx_cb;
  tx.repetition_count = 10;
  tx.code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE;

  xTaskCreatePinnedToCore
    (
     transmission_initiator_task,
     "Transmission initiator task",
     4*1024, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}

void rf_begin_send_stored_signal(rf_stored_signal_t signal) {
  if (rf_antenna_is_busy()) {
    // TODO Do... something??
    return;
  }


  switch (signal) {
  case STORED_SIGNAL_HOME_GARAGE_ENTER:
    	tx.code = HOME_GARAGE_ENTER_CODE;
	tx.code_name = "Home Enter Garage";
	break;
  case STORED_SIGNAL_HOME_GARAGE_EXIT:
    	tx.code = HOME_GARAGE_EXIT_CODE;
	tx.code_name = "Home Exit Garage";
	break;
  default:
    // TODO Unknown signal?
    RF_LOGE("Unknown stored signal requested to be sent??");
    return;
  }

  // TODO Should this be atomic?
  rf_antenna_set_busy(true);

  bool value = true;
  xQueueSend(queue_tx_start_handle, &value, 0);
}

int rf_companion_bt_read_chr_cb(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id) {
  if (ble_uuid_cmp(chr_id, &rfble_gatt_chr_antenna_state_uuid.u) == 0) {
    RF_LOGI("Requested antenna state");

    return rfble_gatt_push8(ctxt, rf_antenna_is_busy());
  }

  return BLE_ATT_ERR_UNLIKELY;
}

int rf_companion_bt_write_chr_cb(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id) {
  int rc;

  if (ble_uuid_cmp(chr_id, &rfble_gatt_chr_send_rf_uuid.u) == 0) {
    uint8_t value;
    rc = rfble_gatt_recv8(ctxt, &value);
    if (rc != 0) {
      return rc;
    }

    RF_LOGI("Requested sending stored RF signal with id %d", value);
    rf_begin_send_stored_signal(value);
  }

  return BLE_ATT_ERR_UNLIKELY;
}

void init_pairing_mode_button() {
  gpio_reset_pin(PAIRING_BUTTON_GPIO);
  gpio_set_direction(PAIRING_BUTTON_GPIO, GPIO_MODE_INPUT);
  gpio_set_pull_mode(PAIRING_BUTTON_GPIO, GPIO_PULLDOWN_ONLY);
}

bool pairing_mode_button_state() {
  return gpio_get_level(PAIRING_BUTTON_GPIO);
}

void rf_companion_main_task() {
  int64_t pairing_button_pressed_since = esp_timer_get_time();
  bool ready_to_reboot_led_on = false;

  while (true) {
    if (ready_to_reboot) {
      if (ready_to_reboot_led_on) {
	status_led_off();
      } else {
	status_led_color_rgb(&COLOR_CYAN);
      }
      ready_to_reboot_led_on = !ready_to_reboot_led_on;

      if (!gpio_get_level(PAIRING_BUTTON_GPIO)) {
	vTaskDelay(100 / portTICK_PERIOD_MS);
	esp_restart();
	break;
      }

      // Delay for a blink in the led
      vTaskDelay(100 / portTICK_PERIOD_MS);
      continue;
    }

    if (!pairing_mode_button_state()) {
      pairing_button_pressed_since = esp_timer_get_time();
    }

    if (esp_timer_get_time() - pairing_button_pressed_since > PAIRING_BUTTON_MICROS) {
      if (pairing_mode) {
	// If in pairing mode, the button will reboot the esp in
	// normal mode, once the user releases it.
	ready_to_reboot = true;
	RF_LOGI("Pairing mode button held. Ready to reboot.");
      } else {
	// When not in pairing mode, the button will just reboot instantly the esp.
	RF_LOGI("Pairing mode button held. Rebooting");
	esp_restart();
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

  vTaskDelete(NULL);
}
