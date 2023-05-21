#include "rfapp/rfapp.h"

#define TAG "RF Companion"
void app_main(void) {
  RF_LOGI("Device is up!");
  init_status_led();
  init_nvs();
  init_pairing_mode_button();
  init_clemsa_codegen();

  if (pairing_mode_button_state()) {
    app_pairing_mode_main();
  } else {
    app_rf_main();
  }
}
