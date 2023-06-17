#ifndef RF_PRIV_H
#define RF_PRIV_H

#include "esp_err.h"
#include "rf.h"

esp_err_t rf_antenna_set_busy(rf_antenna_port_t *port, rf_antenna_tx_t *tx);
esp_err_t rf_antenna_set_free(rf_antenna_port_t *port);

#endif // RF_PRIV_H
