#include "cc1101.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/portmacro.h"
#include "hal/gpio_types.h"
#include "hal/spi_types.h"
#include "host/ble_uuid.h"
#include "led_strip.h"
#include "led_strip_types.h"
#include "nvs.h"
#include "rfapp.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include <stdint.h>
#include "../rf/clemsacode.h"
#include "../private.h"
#include "../bt/rfble_gatt.h"
#include "../rf/fixedcode.h"
#include "../stored_codes.h"

struct rgb COLOR_RED = {.r = 255, .g = 0, .b = 0};
struct rgb COLOR_BLUE = {.r = 0, .g = 0, .b = 255};
struct rgb COLOR_GREEN = {.r = 0, .g = 255, .b = 0};
struct rgb COLOR_CYAN = {.r = 0, .g = 255, .b = 255};

static led_strip_handle_t status_led;
static rf_antenna_port_t antenna = {0};

bool pairing_mode;
bool ready_to_reboot = false;

nvs_handle_t app_nvs_handle;

DECL_STATIC_QUEUE(tx_start, sizeof(rf_antenna_tx_t*), 1);
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

void status_led_off(void) {
  status_led_color(0, 0, 0);
}

void status_led_color(uint8_t r, uint8_t g, uint8_t b) {
#if CONFIG_RFAPP_TARGET_ESP32S3_LOLIN_MINI
    led_strip_set_pixel(status_led, 0, g, r, b);
#else
    led_strip_set_pixel(status_led, 0, r, g, b);
#endif
  led_strip_refresh(status_led);
}

void status_led_color_rgb(struct rgb *rgb) {
  status_led_color(rgb->r, rgb->g, rgb->b);
}

void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(nvs_open(RF_APP_NVS_NS, NVS_READWRITE, &app_nvs_handle));
}

uint8_t rf_app_get_next_boot_mode() {
  uint8_t mode;
  esp_err_t ret;

  ret = nvs_get_u8(app_nvs_handle, RF_APP_INIT_MODE_KEY, &mode);
  if (ret == ESP_ERR_NVS_NOT_FOUND) {
    return RF_APP_INIT_HW_DEFINED;
  }

  if (ret == ESP_OK) {
    return mode;
  }

  RF_LOGE("Unable to read next boot mode: %s", esp_err_to_name(ret));
  return RF_APP_INIT_HW_DEFINED;
}

void rf_app_set_next_boot_mode(uint8_t mode) {
  esp_err_t ret = nvs_set_u8(app_nvs_handle, RF_APP_INIT_MODE_KEY, mode);
  if (ret == ESP_OK) {
    ret = nvs_commit(app_nvs_handle);
  }

  if (ret != ESP_OK) {
    RF_LOGE("Unable to set boot mode: %s", esp_err_to_name(ret));
  }
}


void rf_app_clear_next_boot_mode() {
  esp_err_t ret = nvs_erase_key(app_nvs_handle, RF_APP_INIT_MODE_KEY);
  if (ret == ESP_OK) {
    ret = nvs_commit(app_nvs_handle);
  }

  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    RF_LOGE("Unable to clear boot mode: %s", esp_err_to_name(ret));
  }
}

// Task that will initiate the transmission from the CPU 1, so
// interrupts are also handled by CPU 1 and can run without weird
// stuff interferring on them.
static void transmission_initiator_task(void* arg) {
  rf_antenna_tx_t* tx;
  while (1) {
    xQueueReceive(queue_tx_start_handle, &tx, portMAX_DELAY);
    RF_LOGI("Tranmission of %s initiated. TX: %p", tx->tx_name, tx);

    ESP_ERROR_CHECK(rf_antenna_begin_tx(&antenna, tx));
    RF_LOGI("Transmission initiated");
  }
}

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
void init_cc1101_antenna(void) {
  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  spi_bus_config_t spi_bus_cfg = {
    .miso_io_num = CC1101_MISO_GPIO,
    .mosi_io_num = CC1101_MOSI_GPIO,
    .sclk_io_num = CC1101_SCLK_GPIO,
  };

  cc1101_device_cfg_t cfg = {
    .spi_host = SPI2_HOST,
    .gdo0_io_num = CC1101_GDO0_GPIO,
    .gdo2_io_num = CC1101_GDO2_GPIO,
    .cs_io_num = CC1101_CSN_GPIO,
    .miso_io_num = CC1101_MISO_GPIO,
    .crystal_freq = CC1101_CRYSTAL_26MHZ
  };

  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi_bus_cfg, SPI_DMA_CH_AUTO));
  ESP_ERROR_CHECK(rf_antenna_init(&antenna, &cfg));
  RF_LOGI("Initialized CC1101 antenna");
  cc1101_debug_print_regs(antenna.cc1101);
}
#endif

static esp_err_t antenna_begin_tx_cb(rf_antenna_port_t* port, rf_antenna_tx_t* tx) {
  RF_LOGI("Initiated antenna transmission");
  rfble_gatt_notify_antenna_state_change();
  return ESP_OK;
}

static esp_err_t antenna_end_tx_cb(rf_antenna_port_t* port, rf_antenna_tx_t* tx) {
  RF_LOGI("Terminated antenna transmission");
  rfble_gatt_notify_antenna_state_change();
  rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_COMPLETED);
  return ESP_OK;
}

void init_antenna(void) {
  queue_tx_start_handle = xQueueCreateStatic
    (queue_tx_start_max_item_count,
     queue_tx_start_item_size,
     queue_tx_start_storage,
     &queue_tx_start_holder);

  xTaskCreatePinnedToCore
    (
     transmission_initiator_task,
     "Transmission initiator task",
     4*1024, NULL, tskIDLE_PRIORITY + 1, NULL, 1);

  antenna.tx_begin_cb = antenna_begin_tx_cb;
  antenna.tx_end_cb = antenna_end_tx_cb;
#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
  init_cc1101_antenna();
#endif
}

/**
 * Acquires the RF antenna and sends to the Transmission initiator
 * task a signal for initiating the transmission of a signal.
 */
static void rf_push_tx(const rf_antenna_tx_t* tx) {
  if (rf_antenna_is_busy(&antenna)) {
    RF_LOGE("Couldn't initiate the transmission because the antenna is busy.");
    rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_BUSY);
    return;
  }

  rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_PROCESSING);
  xQueueSend(queue_tx_start_handle, &tx, 0);
}

int rf_companion_bt_read_chr_cb(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id) {
  if (ble_uuid_cmp(chr_id, &rfble_gatt_chr_antenna_state_uuid.u) == 0) {
    RF_LOGI("Requested antenna state");
    return rfble_gatt_push8(ctxt, rf_antenna_is_busy(&antenna));
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

    int32_t index = (int32_t)value - 1;
    if (index >= STORED_CODES_COUNT) {
      RF_LOGW("Requested stored code at index %d is outside the range", value);
      rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_UNKNOWN_SIGNAL);
    } else {
      RF_LOGI("Requested sending stored RF signal with id %d", value);
      rf_push_tx(stored_codes[index]);
    }

    return 0;
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

  #ifdef CONFIG_RFAPP_DEVO_MODE
  RF_LOGW("/====================================================================\\");
  RF_LOGW("|                              WARNING!                              |");
  RF_LOGW("|  Application is running in development mode. In this mode, pairing |");
  RF_LOGW("|  request will be AUTO ACCEPTED! Make sure this is not running on a |");
  RF_LOGW("|                       production environment!                      |");
  RF_LOGW("\\====================================================================/");
  #endif

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
