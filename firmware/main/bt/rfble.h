#ifndef RFBLE_H
#define RFBLE_H

#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

#if CONFIG_BT_NIMBLE_MAX_BONDS != CONFIG_BT_NIMBLE_WHITELIST_SIZE
#error It is required the maximum number of bonded devices to be the same as the max whitelist size (CONFIG_BT_NIMBLE_MAX_BONDS == CONFIG_BT_NIMBLE_WHITELIST_SIZE)
#endif

#define RF_BLE_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define RF_BLE_ADDR_FMT_PARAMS(addr)                                           \
  (addr).val[5], (addr).val[4], (addr).val[3], (addr).val[2], (addr).val[1],   \
      (addr).val[0]


#define RF_BLE_ADDR_WITH_TYPE_FMT RF_BLE_ADDR_FMT " (type=%d)"
#define RF_BLE_ADDR_WITH_TYPE_FMT_PARAMS(addr)                                 \
  RF_BLE_ADDR_FMT_PARAMS(addr), (addr).type

#define RF_BLE_ADDR_PEER_FMT						\
  "peer_id_addr=" RF_BLE_ADDR_WITH_TYPE_FMT				\
  "; peer_ota_addr=" RF_BLE_ADDR_WITH_TYPE_FMT

#define RF_BLE_ADDR_PEER_FMT_PARAMS(id, ota)				\
  RF_BLE_ADDR_WITH_TYPE_FMT_PARAMS(id), RF_BLE_ADDR_WITH_TYPE_FMT_PARAMS(ota)

#define RF_BLE_PEER_FULL_DESC_FMT                                              \
  "handle=%d; " RF_BLE_ADDR_PEER_FMT "; enc=%d; auth=%d; bond=%d; key_size=%d"

#define RF_BLE_PEER_FULL_DESC_FMT_PARAMS(desc)                                 \
  (desc).conn_handle,                                                          \
      RF_BLE_ADDR_PEER_FMT_PARAMS((desc).peer_id_addr, (desc).peer_ota_addr),  \
      (desc).sec_state.encrypted, (desc).sec_state.authenticated,              \
      (desc).sec_state.bonded, (desc).sec_state.key_size



#define RF_BLE_MAX_KNOWN_DEVICES CONFIG_BT_NIMBLE_MAX_BONDS

// GATT definitions
static const ble_uuid16_t gatt_chr_name_uuid = BLE_UUID16_INIT(0x2901);

/* a86ca5d3-4e5b-4773-a915-c82f15acfa4f */
/** RF Companion Service */
static const ble_uuid128_t rfble_gatt_svc_rf_companion_uuid =
  BLE_UUID128_INIT(0xa8, 0x6c, 0xa5, 0xd3, 0x4e, 0x5b, 0x47, 0x73,
		   0xa9, 0x15, 0xc8, 0x2f, 0x15, 0xac, 0xfa, 0x4f);

/* 5459a1e1-6974-4f1c-bdf8-839acb3a1a42 */
/** RF Companion Service - Send RF Signal characteristic: Sends a
   pre-recorded signal by the given identifier */
static const ble_uuid128_t rfble_gatt_chr_send_rf_uuid =
  BLE_UUID128_INIT(0x54, 0x59, 0xa1, 0xe1, 0x69, 0x74, 0x4f, 0x1c,
		   0xbd, 0xf8, 0x83, 0x9a, 0xcb, 0x3a, 0x1a, 0x42);

/* a0f3334d-76e9-485a-955b-5657ee50569f */
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

// Callback types
typedef void (*rfble_passkey_cb_display_key)(uint16_t conn_handle, uint32_t key);
typedef bool (*rfble_passkey_cb_request_accept_key)(uint16_t conn_handle, uint32_t key);
typedef uint32_t (*rfble_passkey_cb_request_enter_key)(uint16_t conn_handle);
typedef int(*rfble_gatt_cb_chr_access)(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id);

// Structs
typedef enum rfble_discovery_mode {
  /** Set the device to be discoverable */
  RF_BLE_DISC_GENERAL,

  /** Set the device as "non discoverable" and enables adv and connection
     whitelisting */
  RF_BLE_DISC_FILTERED
} rfble_discovery_mode_t;

typedef enum rfble_io_cap {
  RF_BLE_IO_CAP_DISPLAY_ONLY          = 0,
  RF_BLE_IO_CAP_DISPLAY_YESNO         = 1,
  RF_BLE_IO_CAP_KEYBOARD_ONLY         = 2,
  //   RF_BLE_IO_CAP_NO_INPUT_OUTPUT       = 3, Disabled for obvious security reasons.
  RF_BLE_IO_CAP_KEYBOARD_DISPLAY      = 4,
} rfble_io_cap_t;

typedef struct rfble_opts {
  /** The discovery mode mode for the device */
  rfble_discovery_mode_t discovery_mode;

  /** The IO capabilities of the device */
  rfble_io_cap_t io_cap;

  /** Allows pairing new devices, or re-pairing already paired ones */
  bool allow_device_pairing;

  /** Device name that will be used to identify the device while
     discovering. This parameter will be ignored if the discovery mode
     is set to filtered. */
  const char* device_name;

  /** Callback called when a pairing request requires to type a locally
     generated key in the remote device */
  rfble_passkey_cb_display_key pair_req_display_key_cb;

  /** Callback called when a pairing request requires a numeric
     comparison between two devices */
  rfble_passkey_cb_request_accept_key pair_req_numcmp_cb;

  /** Callback called when a pairing request requires the user to type
     the key available in the screen of the remote device*/
  rfble_passkey_cb_request_enter_key pair_req_type_key_cb;

  /** Callback called when a GATT attribute is attempted to be read. */
  rfble_gatt_cb_chr_access gatt_read_cb;

  /** Callback called when a GATT attribute is attempted to be written. */
  rfble_gatt_cb_chr_access gatt_write_cb;
} rfble_opts_t;

typedef struct rfble_state {
  uint16_t rfble_gatt_chr_antenna_state_handle;
  uint16_t conn_handle;
} rfble_state_t;

void rfble_begin(rfble_opts_t *opts);
void rfble_sync_whitelist_with_bonded_devs();

/** rfble_opts_t instance, initialized with rfble_begin */
extern rfble_opts_t rf_ble_opts;

/** rfle_state_t instance holding the state of the Bluetooth service. Initialized with rfble_begin */
extern rfble_state_t rf_ble_state;

bool rfble_is_connected();

/** Pushes a 8 bit number to the peer device on a GATT characteristic read request */
int rfble_gatt_push8(struct ble_gatt_access_ctxt *ctxt, uint8_t value);

/** Receives a 8 bit number to the peer device on a GATT characteristic write request */
int rfble_gatt_recv8(struct ble_gatt_access_ctxt *ctxt, uint8_t *value);

/** Sends a 8 bit number to the peer device as a notification of a GATT characteristic */
int rfble_gatt_notif8(uint16_t att_handle, uint8_t value);

// Temp shit copied from blprph example.
/** GATT server. */
#define GATT_SVR_SVC_ALERT_UUID               0x1811
#define GATT_SVR_CHR_SUP_NEW_ALERT_CAT_UUID   0x2A47
#define GATT_SVR_CHR_NEW_ALERT                0x2A46
#define GATT_SVR_CHR_SUP_UNR_ALERT_CAT_UUID   0x2A48
#define GATT_SVR_CHR_UNR_ALERT_STAT_UUID      0x2A45
#define GATT_SVR_CHR_ALERT_NOT_CTRL_PT        0x2A44

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int gatt_svr_init(void);
#endif
