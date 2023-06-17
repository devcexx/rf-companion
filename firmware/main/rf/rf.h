#ifndef RF_H
#define RF_H

#include "esp_err.h"
#include "hal/gpio_types.h"
#include <stdint.h>
#ifdef CONFIG_RFAPP_ENABLE_CC1101_SUPPORT
#define RF_ENABLE_CC1101_SUPPORT
#include "cc1101.h"
#endif

typedef struct rf_antenna_port rf_antenna_port_t;
typedef struct rf_antenna_tx rf_antenna_tx_t;

typedef esp_err_t (*rf_antenna_tx_begin_cb)(rf_antenna_port_t* port, rf_antenna_tx_t* tx);
typedef esp_err_t (*rf_antenna_tx_end_cb)(rf_antenna_port_t *port, rf_antenna_tx_t* tx);

typedef struct {
  esp_err_t (*tx_begin_func)(rf_antenna_port_t *port, rf_antenna_tx_t* tx);
} rf_antenna_tx_generator_t;

struct rf_antenna_tx {
  const char* tx_name;
  const rf_antenna_tx_generator_t* _generator;
};

struct rf_antenna_port {
  #ifdef RF_ENABLE_CC1101_SUPPORT
  cc1101_device_t* cc1101;
  #else
  gpio_num_t gpio;
  #endif

  rf_antenna_tx_begin_cb tx_begin_cb;
  rf_antenna_tx_end_cb tx_end_cb;

  rf_antenna_tx_t* _sending_tx;
  uint8_t state[128];
};

esp_err_t rf_antenna_write(rf_antenna_port_t *port, uint32_t level);
#ifdef RF_ENABLE_CC1101_SUPPORT
esp_err_t rf_antenna_init(rf_antenna_port_t *port, cc1101_device_cfg_t* config);
#else
esp_err_t rf_antenna_init(rf_antenna_port_t *port, gpio_num_t gpio);
#endif
esp_err_t rf_antenna_begin_tx(rf_antenna_port_t *port, rf_antenna_tx_t* tx);
esp_err_t rf_antenna_is_busy(rf_antenna_port_t *port);

#endif // RF_H
