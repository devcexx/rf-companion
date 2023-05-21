#ifndef RFBLE_H
#define RFBLE_H

#include <stdbool.h>
#include <stdint.h>
#include "services/gap/ble_svc_gap.h"
#include "host/ble_hs.h"
#include "rfble_gatt.h"

#if CONFIG_BT_NIMBLE_MAX_BONDS != CONFIG_BT_NIMBLE_WHITELIST_SIZE
#error It is required the maximum number of bonded devices to be the same as the max whitelist size (CONFIG_BT_NIMBLE_MAX_BONDS == CONFIG_BT_NIMBLE_WHITELIST_SIZE)
#endif

#define RFBLE_ADDR_FMT "%02x:%02x:%02x:%02x:%02x:%02x"
#define RFBLE_ADDR_FMT_PARAMS(addr)                                           \
  (addr).val[5], (addr).val[4], (addr).val[3], (addr).val[2], (addr).val[1],   \
      (addr).val[0]


#define RFBLE_ADDR_WITH_TYPE_FMT RFBLE_ADDR_FMT " (type=%d)"
#define RFBLE_ADDR_WITH_TYPE_FMT_PARAMS(addr)                                  \
  RFBLE_ADDR_FMT_PARAMS(addr), (addr).type

#define RFBLE_ADDR_PEER_FMT                                                    \
  "peer_id_addr=" RFBLE_ADDR_WITH_TYPE_FMT                                     \
  "; peer_ota_addr=" RFBLE_ADDR_WITH_TYPE_FMT

#define RFBLE_ADDR_PEER_FMT_PARAMS(id, ota)				\
  RFBLE_ADDR_WITH_TYPE_FMT_PARAMS(id), RFBLE_ADDR_WITH_TYPE_FMT_PARAMS(ota)

#define RFBLE_PEER_FULL_DESC_FMT                                              \
  "handle=%d; " RFBLE_ADDR_PEER_FMT "; enc=%d; auth=%d; bond=%d; key_size=%d"

#define RFBLE_PEER_FULL_DESC_FMT_PARAMS(desc)                                  \
  (desc).conn_handle,                                                          \
      RFBLE_ADDR_PEER_FMT_PARAMS((desc).peer_id_addr, (desc).peer_ota_addr),   \
      (desc).sec_state.encrypted, (desc).sec_state.authenticated,              \
      (desc).sec_state.bonded, (desc).sec_state.key_size

#define RFBLE_MAX_KNOWN_DEVICES CONFIG_BT_NIMBLE_MAX_BONDS

// Callback types
typedef void (*rfble_passkey_cb_display_key)(uint16_t conn_handle, uint32_t key);
typedef bool (*rfble_passkey_cb_request_accept_key)(uint16_t conn_handle, uint32_t key);
typedef uint32_t (*rfble_passkey_cb_request_enter_key)(uint16_t conn_handle);
typedef int(*rfble_gatt_cb_chr_access)(struct ble_gatt_access_ctxt *ctxt, const ble_uuid_t* chr_id);

// Structs
typedef enum rfble_discovery_mode {
  /** Set the device to be discoverable */
  RFBLE_DISC_GENERAL,

  /** Set the device as "non discoverable" and enables adv and connection
     whitelisting */
  RFBLE_DISC_FILTERED
} rfble_discovery_mode_t;

typedef enum rfble_io_cap {
  RFBLE_IO_CAP_DISPLAY_ONLY          = 0,
  RFBLE_IO_CAP_DISPLAY_YESNO         = 1,
  RFBLE_IO_CAP_KEYBOARD_ONLY         = 2,
  //   RFBLE_IO_CAP_NO_INPUT_OUTPUT       = 3, Disabled for obvious security reasons.
  RFBLE_IO_CAP_KEYBOARD_DISPLAY      = 4,
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
  uint16_t conn_handle;
  rfble_gatt_handles_t gatt_handles;
} rfble_state_t;

void rfble_begin(rfble_opts_t *opts);
void rfble_sync_whitelist_with_bonded_devs();

/** rfble_opts_t instance, initialized with rfble_begin */
extern rfble_opts_t rfble_opts;

/** rfle_state_t instance holding the state of the Bluetooth service. Initialized with rfble_begin */
extern rfble_state_t rfble_state;

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

void rfble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
int rfble_gatt_init(void);
#endif
