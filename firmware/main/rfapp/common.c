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
#include "../clemsacode.h"
#include "../private.h"
#include "../bt/rfble_gatt.h"
#include "../teslacharger.h"

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

nvs_handle_t app_nvs_handle;

DECL_STATIC_QUEUE(tx_start, sizeof(tx_type_t), 1);
QueueHandle_t queue_tx_start_handle;

void init_antenna(void) {
  gpio_reset_pin(RF_ANTENNA_GPIO);
  gpio_set_direction(RF_ANTENNA_GPIO, GPIO_MODE_OUTPUT);
}

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

bool rf_antenna_is_busy() { return antenna_busy; }

void rf_antenna_set_busy(bool value) {
  bool changed = antenna_busy != value;
  antenna_busy = value;
  if (changed) {
    rfble_gatt_notify_antenna_state_change();
  }
}

static void clemsa_codegen_tx_cb(struct clemsa_codegen_tx* tx) {
  rf_antenna_set_busy(false);
  rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_COMPLETED);
}

// Task that will initiate the transmission from the CPU 1, so
// interrupts are also handled by CPU 1 and can run without weird
// stuff interferring on them.
static void transmission_initiator_task(void* arg) {
  tx_type_t tx_type;

  while (1) {
    xQueueReceive(queue_tx_start_handle, &tx_type, portMAX_DELAY);
    switch (tx_type) {
    case TX_TYPE_CLEMSA_CODEGEN:
      // In this kind of transmission, everything's already set up by
      // the caller, we just need to initiate the tx. The callback of
      // the tx will then free the antenna and send the termination
      // notification.
      ESP_ERROR_CHECK(clemsa_codegen_begin_tx(&generator, &tx));
      break;
    case TX_TYPE_TESLA_CHARGER_OPEN:
      // In this tx, the antenna is set to busy prior to this call,
      // and the state of the request is set to PROCESSING. We need to
      // run the tx, free the antenna and send the RF task completion
      // notification.
      tesla_charger_open_door_sync(RF_ANTENNA_GPIO);
      rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_COMPLETED);
      rf_antenna_set_busy(false);
    }

    RF_LOGI("Transmission initiated");
  }
}
uint8_t registers[] = {
    0x0b, 0x2e, 0x3f, 0x07, 0xd3, 0x91, 0xff, 0x04, 0x12, 0x00, 0x00, 0x0f,
    0x00, 0x10, 0xb0, 0x4b, 0x89, 0x42, 0x30, 0x22, 0xf8, 0x47, 0x07, 0x30,
    0x18, 0x14, 0x6c, 0x03, 0x40, 0x91, 0x87, 0x6b, 0xf8, 0x56, 0x11, 0xaa,
    0x2a, 0x17, 0x0d, 0x41, 0x00, 0x59, 0x7f, 0x3f, 0x88, 0x31, 0x0b};


uint8_t patable[8] = {0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


void init_clemsa_codegen() {
  queue_tx_start_handle = xQueueCreateStatic
    (queue_tx_start_max_item_count,
     queue_tx_start_item_size,
     queue_tx_start_storage,
     &queue_tx_start_holder);

#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT

#define MISO_GPIO GPIO_NUM_13
#define MOSI_GPIO GPIO_NUM_11
#define SCLK_GPIO GPIO_NUM_12
#define CSN_GPIO GPIO_NUM_2

  // In this mode, this pin will be used as input. The CC1101 will be
  // generate a clock at a frequency equal to the data rate (in this
  // case, 16 kHz).
#define GDO2_GPIO GPIO_NUM_4

  // Used for data transmission. The CC1101 will sample the signal of
  // the GPIO on each clock raise.
#define GDO0_GPIO GPIO_NUM_10


  ESP_ERROR_CHECK(gpio_install_isr_service(0));


  spi_bus_config_t spi_bus_cfg = {
    .miso_io_num = MISO_GPIO,
    .mosi_io_num = MOSI_GPIO,
    .sclk_io_num = SCLK_GPIO,
  };

  cc1101_device_cfg_t cfg = {
    .spi_host = SPI2_HOST,
    .gdo0_io_num = GDO0_GPIO,
    .gdo2_io_num = GDO2_GPIO,
    .cs_io_num = CSN_GPIO,
    .miso_io_num = MISO_GPIO,
    .crystal_freq = CC1101_CRYSTAL_26MHZ
  };

  cc1101_device_t* device;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi_bus_cfg, SPI_DMA_CH_AUTO));
  ESP_ERROR_CHECK(cc1101_init(&cfg, &device));
  ESP_ERROR_CHECK(cc1101_write_burst(device, CC1101_FIRST_CFG_REG, registers, sizeof(registers)));
  ESP_ERROR_CHECK(cc1101_write_patable(device, patable));
  ESP_ERROR_CHECK(clemsa_codegen_init(&generator, device));
  #else
  ESP_ERROR_CHECK(clemsa_codegen_init(&generator, RF_ANTENNA_GPIO));
  #endif

  generator.done_callback = clemsa_codegen_tx_cb;
  tx.repetition_count = 10;
  tx.code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE;

  xTaskCreatePinnedToCore
    (
     transmission_initiator_task,
     "Transmission initiator task",
     4*1024, NULL, tskIDLE_PRIORITY + 1, NULL, 1);
}

/**
 * Acquires the RF antenna and sends to the Transmission initiator
 * task a signal for initiating the transmission of a signal.
 */
static void rf_push_tx(tx_type_t type) {
  if (rf_antenna_is_busy()) {
    RF_LOGE("Couldn't initiate the transmission because the antenna is busy.");
    return;
  }

  rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_PROCESSING);
  rf_antenna_set_busy(true);
  xQueueSend(queue_tx_start_handle, &type, 0);
}

static void rf_push_clemsa_tx(const bool* code, const char* code_name) {
  if (rf_antenna_is_busy()) {
    RF_LOGE("Couldn't initiate the transmission because the antenna is busy.");
    return;
  }

  tx.code = code;
  tx.code_name = code_name;
  rf_push_tx(TX_TYPE_CLEMSA_CODEGEN);
}

// This function is only intended to be called from the Send RF GATT
// Operation, because it will notify back the GATT server about
// changes in the operation.
void rf_begin_send_stored_signal(rf_stored_signal_t signal) {
  if (rf_antenna_is_busy()) {
    rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_BUSY);
    return;
  }

  switch (signal) {
  case STORED_SIGNAL_HOME_GARAGE_ENTER:
    rf_push_clemsa_tx(HOME_GARAGE_ENTER_CODE, "Home Enter Garage");
    break;
  case STORED_SIGNAL_HOME_GARAGE_EXIT:
    rf_push_clemsa_tx(HOME_GARAGE_EXIT_CODE, "Home Exit Garage");
    break;
  case STORED_SIGNAL_PARENTS_GARAGE_LEFT:
    rf_push_clemsa_tx(PARENTS_GARAGE_ENTER_CODE, "Parents Enter Garage");
    break;
  case STORED_SIGNAL_PARENTS_GARAGE_RIGHT:
    rf_push_clemsa_tx(PARENTS_GARAGE_EXIT_CODE, "Parents Exit Garage");
    break;
  case STORED_SIGNAL_TESLA_CHARGER_DOOR_OPEN:
    rf_push_tx(TX_TYPE_TESLA_CHARGER_OPEN);
    break;
  default:
    RF_LOGE("Unknown stored signal requested to be sent: %d", signal);
    rfble_gatt_notify_send_rf_response(RFBLE_GATT_SEND_RF_UNKNOWN_SIGNAL);
    return;
  }

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
