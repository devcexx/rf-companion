#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_uuid.h"
#include <stdint.h>

#ifndef RFBLE_GATT_H
#define RFBLE_GATT_H

// GATT definitions
static const ble_uuid16_t rfble_gatt_chr_name_uuid = BLE_UUID16_INIT(0x2901);

/* 4ffaac15-2fc8-15a9-7347-5b4ed3a56ca8 */
/** RF Companion Service */
static const ble_uuid128_t rfble_gatt_svc_rf_companion_uuid =
  BLE_UUID128_INIT(0xa8, 0x6c, 0xa5, 0xd3, 0x4e, 0x5b, 0x47, 0x73,
		   0xa9, 0x15, 0xc8, 0x2f, 0x15, 0xac, 0xfa, 0x4f);

/* 421a3acb-9a83-f8bd-1c4f-7469e1a15954 */
/** RF Companion Service - Send RF Signal characteristic: Sends a
   pre-recorded signal by the given identifier. It uses a notify
   channel to notify client about the status of the request */
static const ble_uuid128_t rfble_gatt_chr_send_rf_uuid =
  BLE_UUID128_INIT(0x54, 0x59, 0xa1, 0xe1, 0x69, 0x74, 0x4f, 0x1c,
		   0xbd, 0xf8, 0x83, 0x9a, 0xcb, 0x3a, 0x1a, 0x42);

/* 9f5650ee-5756-5b95-5a48-e9764d33f3a0 */
/** RF Companion Service - RF Antenna status characteristic: Retrieves
   the status of the RF antenna on a given moment: 1 means busy, 0
   means free. */
static const ble_uuid128_t rfble_gatt_chr_antenna_state_uuid =
    BLE_UUID128_INIT(0xa0, 0xf3, 0x33, 0x4d, 0x76, 0xe9, 0x48, 0x5a, 0x95, 0x5b,
                     0x56, 0x57, 0xee, 0x50, 0x56, 0x9f);

/** The handle of the Antenna State characteristic, that can be used
    for sending notifications. This value will only be valid after the
    GATT service has been registered */
extern uint32_t rfble_gatt_chr_antenna_state_handle;

typedef enum rfble_gatt_send_rf_notif {
  RFBLE_GATT_SEND_RF_PROCESSING = 1,
  RFBLE_GATT_SEND_RF_BUSY = 2,
  RFBLE_GATT_SEND_RF_COMPLETED = 3,
  RFBLE_GATT_SEND_RF_UNKNOWN_SIGNAL = 4,
} rfble_gatt_send_rf_notif_t;

typedef enum rfble_gatt_antenna_state_notif {
  RFBLE_GATT_ANTENNA_STATE_FREE = 0,
  RFBLE_GATT_ANTENNA_STATE_BUSY = 1
} rfble_gatt_antenna_state_notif_t;

typedef struct rfble_gatt_handles {
  uint16_t antenna_state_handle;
  uint16_t send_rf_handle;
} rfble_gatt_handles_t;

void rfble_gatt_notify_antenna_state_change();
void rfble_gatt_notify_send_rf_response(rfble_gatt_send_rf_notif_t notification);

#endif
