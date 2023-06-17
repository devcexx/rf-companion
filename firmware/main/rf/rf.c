#include "rf.h"
#include "cc1101.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

esp_err_t rf_antenna_set_busy(rf_antenna_port_t *port, rf_antenna_tx_t *tx) {
  if (port->_sending_tx == NULL) {
    port->_sending_tx = tx;
    if (port->tx_begin_cb != NULL) {
      return port->tx_begin_cb(port, tx);
    }
  }
  return ESP_OK;
}
esp_err_t rf_antenna_set_free(rf_antenna_port_t *port) {
  if (port->_sending_tx != NULL) {
    #ifdef RF_ENABLE_CC1101_SUPPORT
    vTaskDelay(10);
    cc1101_set_idle(port->cc1101);
    #endif

    port->_sending_tx = NULL;
    if (port->tx_end_cb != NULL) {
      return port->tx_end_cb(port, port->_sending_tx);
    }
  }
  return ESP_OK;
}

esp_err_t rf_antenna_begin_tx(rf_antenna_port_t *port, rf_antenna_tx_t* tx) {
  esp_err_t err;

  ESP_LOGI("", "Set TX: %p", tx);
  if ((err = rf_antenna_set_busy(port, tx)) != ESP_OK) {
    return err;
  }

  if ((err = tx->_generator->tx_begin_func(port, tx)) != ESP_OK) {
    rf_antenna_set_free(port);
    return err;
  }

  return ESP_OK;
}
esp_err_t rf_antenna_is_busy(rf_antenna_port_t *port) {
  return port->_sending_tx != NULL;
}

esp_err_t rf_antenna_write(rf_antenna_port_t *port, uint32_t level) {
  #ifdef RF_ENABLE_CC1101_SUPPORT
  return cc1101_trans_continuous_write(port->cc1101, level);
  #else
  // TODO!
  #endif
}

#ifdef RF_ENABLE_CC1101_SUPPORT
static uint8_t registers[] = {
    0x0b, 0x2e, 0x3f, 0x07, 0xd3, 0x91, 0xff, 0x04, 0x12, 0x00, 0x00, 0x0f,
    0x00, 0x10, 0xb0, 0x4b, 0x89, 0x42, 0x30, 0x22, 0xf8, 0x47, 0x07, 0x30,
    0x18, 0x14, 0x6c, 0x03, 0x40, 0x91, 0x87, 0x6b, 0xf8, 0x56, 0x11, 0xaa,
    0x2a, 0x17, 0x0d, 0x41, 0x00, 0x59, 0x7f, 0x3f, 0x88, 0x31, 0x0b};

static uint8_t patable[8] = {0x00, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
esp_err_t rf_antenna_init(rf_antenna_port_t *port, cc1101_device_cfg_t* config) {
  esp_err_t err;

  if ((err = cc1101_init(config, &port->cc1101)) != ESP_OK) {
    return err;
  }

  if ((err = cc1101_write_burst(port->cc1101, CC1101_FIRST_CFG_REG, registers, sizeof(registers))) != ESP_OK) {
    return err;
  }
  if ((err = cc1101_write_patable(port->cc1101, patable)) != ESP_OK) {
    return err;
  }

  cc1101_debug_print_regs(port->cc1101);
  return ESP_OK;
}
#else
esp_err_t rf_antenna_init(rf_antenna_port_t *port, gpio_num_t gpio) {

}
#endif
