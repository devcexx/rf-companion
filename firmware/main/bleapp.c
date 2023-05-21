#include "freertos/FreeRTOS.h"
#include "rfapp/rfapp.h"

#define TAG "RF Companion"
void app_main(void) {
  static_assert(sizeof(tx_type_t) == 1, "tx_type_t must be 1 byte!");

  RF_LOGI("Device is up!");
  init_status_led();
  init_nvs();
  init_pairing_mode_button();
  init_antenna();
  init_clemsa_codegen();

  uint8_t boot_mode = rf_app_get_next_boot_mode();
  RF_LOGI("Next boot mode: %d", boot_mode);
  rf_app_clear_next_boot_mode();

  switch (boot_mode) {
  case RF_APP_INIT_DEFAULT_MODE:
    app_rf_main();
    return;
  case RF_APP_INIT_PAIRING_MODE:
    app_pairing_mode_main();
    return;
  case RF_APP_INIT_HW_DEFINED:
    break;

  default:
    RF_LOGE("Read unknown boot mode from NVS: %d. Falling back to HW Defined mode", boot_mode);
  }

  if (pairing_mode_button_state()) {
    app_pairing_mode_main();
  } else {
    app_rf_main();
  }
}
