#include <assert.h>
#include "freertos/portmacro.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "esp_random.h"
#include "host/ble_gap.h"
#include "host/ble_l2cap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_pvcy.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"

#include "driver/gpio.h"

#include "rfble.h"
#include <stdint.h>
#include <string.h>

#define TAG "RF BLE"

rfble_opts_t rfble_opts;
rfble_state_t rfble_state = {0};

static int rfble_gap_event(struct ble_gap_event *event, void *arg);

void ble_store_config_init(void);

#if MYNEWT_VAL(BLE_POWER_CONTROL)
static struct ble_gap_event_listener power_control_event_listener;
#endif

bool rfble_is_connected() {
  return rfble_state.conn_handle != 0;
}

void rfble_sync_whitelist_with_bonded_devs() {
  ble_addr_t peers[RFBLE_MAX_KNOWN_DEVICES];
  int num_peers = 0;
  int rc;

  rc = ble_store_util_bonded_peers(peers, &num_peers, RFBLE_MAX_KNOWN_DEVICES);
  assert(rc == 0);

  rc = ble_gap_wl_set(peers, num_peers);
  if (rc == 0) {
    uint8_t* addr;
    ESP_LOGI(TAG, "Sync'ed whitelist with list of bonded devices. Currently known devices:");
    for (int i = 0; i < num_peers; i++) {
      addr = peers[i].val;
      ESP_LOGI(TAG, " - %02x:%02x:%02x:%02x:%02x:%02x",
	       addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }
  } else {
    ESP_LOGE(TAG, "Failed to update whitelist with error code %d", rc);
  }
}
static void rfble_advertise(void) {
  struct ble_gap_adv_params adv_params;
  struct ble_hs_adv_fields fields;
  const char *name;
  int rc;

  /**
   *  Set the advertisement data included in our advertisements:
   *     o Flags (indicates advertisement type and other general info).
   *     o Advertising tx power.
   *     o Device name.
   *     o 16-bit service UUIDs (alert notifications).
   */

  memset(&fields, 0, sizeof fields);

  /* Advertise two flags:
   *     o Discoverability in forthcoming advertisement (general)
   *     o BLE-only (BR/EDR unsupported).
   */
  fields.flags = BLE_HS_ADV_F_BREDR_UNSUP;

  /* Indicate that the TX power level field should be included; have the
   * stack fill this value automatically.  This is done by assigning the
   * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
   */
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

  name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)name;
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;

  fields.uuids16 = (ble_uuid16_t[]) {
    BLE_UUID16_INIT(GATT_SVR_SVC_ALERT_UUID)
  };
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error setting advertisement data; rc=%d\n", rc);
    return;
  }

  /* Begin advertising. */
  memset(&adv_params, 0, sizeof adv_params);
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;

  if (rfble_opts.discovery_mode == RFBLE_DISC_GENERAL) {
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.filter_policy = BLE_HCI_ADV_FILT_NONE;
  } else {
    adv_params.disc_mode = BLE_GAP_DISC_MODE_NON;
    adv_params.filter_policy = BLE_HCI_ADV_FILT_BOTH;
  }

  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
			 &adv_params, rfble_gap_event, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error enabling advertisement; rc=%d\n", rc);
    return;
  }
}

/* #if MYNEWT_VAL(BLE_POWER_CONTROL) */
/* static void bleprph_power_control(uint16_t conn_handle) */
/* { */
/*     int rc; */

/*     rc = ble_gap_read_remote_transmit_power_level(conn_handle, 0x01 );  // Attempting on LE 1M phy */
/*     assert (rc == 0); */

/*     rc = ble_gap_set_transmit_power_reporting_enable(conn_handle, 0x1, 0x1); */
/*    assert (rc == 0); */
/* } */
/* #endif */


static void rfble_gap_handle_connect(uint16_t conn_handle, int status) {
  int rc;

  struct ble_gap_conn_desc desc;
  struct ble_store_key_sec key_sec = {0};
  struct ble_store_value_sec value_sec;

  if (status == 0) {
    assert(ble_gap_conn_find(conn_handle, &desc) == 0);

    if (!rfble_opts.allow_device_pairing) {
      // If the current configuration doesn't allow unknown devices to
      // connect, ensure that we do know this device. This shouldn't
      // be needed because of the whitelist, but dunno, just in case.
      key_sec.peer_addr = desc.peer_id_addr;
      rc = ble_store_read_peer_sec(&key_sec, &value_sec);

      if (rc != 0 || !value_sec.ltk_present) {
	// Kick out
	ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
	ESP_LOGE(TAG,
		 "Received a connection successful from a unpaired device. This MUST NOT happen!. "
		 RFBLE_ADDR_PEER_FMT,
		 RFBLE_ADDR_PEER_FMT_PARAMS(desc.peer_id_addr, desc.peer_ota_addr));
	return;
      }
    }

    rfble_state.conn_handle = conn_handle;
    ble_gap_security_initiate(conn_handle);
  } else {
    /* Connection failed; resume advertising. */
    rfble_state.conn_handle = 0;
    rfble_advertise();
  }
}

static void rfble_gap_handle_passkey_action(uint16_t conn_handle, struct ble_gap_passkey_params* params) {
  struct ble_sm_io pkey = {0};
  int rc;

  if (!rfble_opts.allow_device_pairing) {
    ESP_LOGE(TAG, "Security manager forwarded a pairing request while device pairing should be disabled. This MUST NOT happen!!!");
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return;
  }

  ESP_LOGI(TAG, "Passkey action initiated -- Pairing in progress");

  if (params->action == BLE_SM_IOACT_DISP) {
    if (rfble_opts.pair_req_display_key_cb == NULL) {
      ESP_LOGE(TAG, "Requested BLE_SM_IOACT_DISP passkey action but callback was unset.");
      ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      return;
    }

    pkey.action = params->action;
    pkey.passkey = esp_random() % 100000;
    rfble_opts.pair_req_display_key_cb(conn_handle, pkey.passkey);
    rc = ble_sm_inject_io(conn_handle, &pkey);
    ESP_LOGI(TAG, "ble_sm_inject_io result: %d\n", rc);
  } else if (params->action == BLE_SM_IOACT_NUMCMP) {
    if (rfble_opts.pair_req_numcmp_cb == NULL) {
      ESP_LOGE(TAG, "Requested BLE_SM_IOACT_NUMCMP passkey action but callback was unset.");
      ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      return;
    }

    pkey.action = params->action;
    pkey.numcmp_accept = rfble_opts.pair_req_numcmp_cb(conn_handle, params->numcmp);
    rc = ble_sm_inject_io(conn_handle, &pkey);
    ESP_LOGI(TAG, "ble_sm_inject_io result: %d\n", rc);
  } else if (params->action == BLE_SM_IOACT_INPUT) {
    if (rfble_opts.pair_req_type_key_cb == NULL) {
      ESP_LOGE(TAG, "Requested BLE_SM_IOACT_INPUT passkey action but callback was unset.");
      ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
      return;
    }

    pkey.action = params->action;
    pkey.passkey = rfble_opts.pair_req_type_key_cb(conn_handle);
    rc = ble_sm_inject_io(conn_handle, &pkey);
    ESP_LOGI(TAG, "ble_sm_inject_io result: %d\n", rc);
  } else {
    ESP_LOGW(TAG, "Device attempted to pair with an unsupported mechanism (%d). Disconnecting.", params->action);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }
}

static void rfble_gap_handle_disconnect(struct ble_gap_conn_desc* desc, int reason) {
  ESP_LOGI(TAG, "Peer disconnected; reason=%d; " RFBLE_PEER_FULL_DESC_FMT, reason, RFBLE_PEER_FULL_DESC_FMT_PARAMS(*desc));

  /* Connection terminated; resume advertising. */
  rfble_state.conn_handle = 0;
  rfble_advertise();
}

static void rfble_gap_handle_conn_update_req(uint16_t conn_handle) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);
  ESP_LOGI(TAG, "Connection updated request; " RFBLE_PEER_FULL_DESC_FMT, RFBLE_PEER_FULL_DESC_FMT_PARAMS(desc));
}

static void rfble_gap_handle_conn_update(uint16_t conn_handle) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);
  ESP_LOGI(TAG, "Connection updated; " RFBLE_PEER_FULL_DESC_FMT, RFBLE_PEER_FULL_DESC_FMT_PARAMS(desc));
}

static void rfble_gap_handle_adv_complete(int reason) {
  ESP_LOGI(TAG, "Advertise completed; reason=%d", reason);
  rfble_advertise();
}

static void rfble_gap_handle_enc_change(uint16_t conn_handle, int status) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);
  ESP_LOGI(TAG, "Encryption change; status=%d; " RFBLE_PEER_FULL_DESC_FMT, status, RFBLE_PEER_FULL_DESC_FMT_PARAMS(desc));
  if (status != 0) {
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    ESP_LOGW(TAG, "Encryption status is != 0. Connection terminated");
  }
}

static void rfble_gap_handle_identity_resolved(uint16_t conn_handle) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);
  ESP_LOGI(TAG, "Identity resolved; " RFBLE_ADDR_PEER_FMT, RFBLE_ADDR_PEER_FMT_PARAMS(desc.peer_id_addr, desc.peer_ota_addr));
}


static void rfble_gap_handle_subscribe(uint16_t conn_handle) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);
  ESP_LOGI(TAG, "Subscription updated; " RFBLE_PEER_FULL_DESC_FMT, RFBLE_PEER_FULL_DESC_FMT_PARAMS(desc));
}

static void rfble_gap_handle_mtu_update(uint16_t conn_handle, uint16_t channel, uint16_t value) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);
  ESP_LOGI(TAG, "MTU updated; channel=%d; value=%d; " RFBLE_PEER_FULL_DESC_FMT, channel, value, RFBLE_PEER_FULL_DESC_FMT_PARAMS(desc));
}

static int rfble_gap_handle_repeat_pairing(uint16_t conn_handle) {
  struct ble_gap_conn_desc desc;
  assert(ble_gap_conn_find(conn_handle, &desc) == 0);

  if (rfble_opts.allow_device_pairing) {
    ESP_LOGW(TAG, "Peer " RFBLE_ADDR_FMT " is attempting to re-pair. Allow device re-pairing is enabled and keys will be updated", RFBLE_ADDR_FMT_PARAMS(desc.peer_id_addr));
    ble_store_util_delete_peer(&desc.peer_id_addr);
    return BLE_GAP_REPEAT_PAIRING_RETRY;
  } else {
    ESP_LOGW(TAG, "Peer " RFBLE_ADDR_FMT " is attempting to repeat pairing but pairing is disabled. Ignoring request.", RFBLE_ADDR_FMT_PARAMS(desc.peer_id_addr));
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    return BLE_GAP_REPEAT_PAIRING_IGNORE;
  }
}

static int rfble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    rfble_gap_handle_connect(event->connect.conn_handle, event->connect.status);

#if MYNEWT_VAL(BLE_POWER_CONTROL)
    bleprph_power_control(event->connect.conn_handle);

    ble_gap_event_listener_register(&power_control_event_listener,
				    bleprph_gap_event, NULL);
#endif
    return 0;

  case BLE_GAP_EVENT_DISCONNECT:
    rfble_gap_handle_disconnect(&event->disconnect.conn, event->disconnect.reason);
    return 0;

  case BLE_GAP_EVENT_CONN_UPDATE_REQ:
    rfble_gap_handle_conn_update_req(event->conn_update_req.conn_handle);
    return 0;

  case BLE_GAP_EVENT_CONN_UPDATE:
    rfble_gap_handle_conn_update(event->conn_update.conn_handle);
    return 0;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    rfble_gap_handle_adv_complete(event->adv_complete.reason);
    return 0;

  case BLE_GAP_EVENT_ENC_CHANGE:
    rfble_gap_handle_enc_change(event->enc_change.conn_handle, event->enc_change.status);
    return 0;

  case BLE_GAP_EVENT_IDENTITY_RESOLVED:
    rfble_gap_handle_identity_resolved(event->identity_resolved.conn_handle);
    return 0;

  case BLE_GAP_EVENT_SUBSCRIBE:
    rfble_gap_handle_subscribe(event->subscribe.conn_handle);
    return 0;

  case BLE_GAP_EVENT_MTU:
    rfble_gap_handle_mtu_update(event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
    return 0;

  case BLE_GAP_EVENT_REPEAT_PAIRING:
    return rfble_gap_handle_repeat_pairing(event->repeat_pairing.conn_handle);

  case BLE_GAP_EVENT_PASSKEY_ACTION:
    rfble_gap_handle_passkey_action(event->passkey.conn_handle, &event->passkey.params);
    return 0;

#if MYNEWT_VAL(BLE_POWER_CONTROL)
  case BLE_GAP_EVENT_TRANSMIT_POWER:
    MODLOG_DFLT(INFO, "Transmit power event : status=%d conn_handle=%d reason=%d "
		"phy=%d power_level=%x power_level_flag=%d delta=%d",
		event->transmit_power.status,
		event->transmit_power.conn_handle,
		event->transmit_power.reason,
		event->transmit_power.phy,
		event->transmit_power.transmit_power_level,
		event->transmit_power.transmit_power_level_flag,
		event->transmit_power.delta);
    return 0;

  case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
    MODLOG_DFLT(INFO, "Pathloss threshold event : conn_handle=%d current path loss=%d "
		"zone_entered =%d",
		event->pathloss_threshold.conn_handle,
		event->pathloss_threshold.current_path_loss,
		event->pathloss_threshold.zone_entered);
    return 0;
#endif

  default:
    ESP_LOGW(TAG, "Received unhandled event %d", event->type);
  }

  return 0;
}

static void rfble_on_reset(int reason) {
  ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

static void rfble_on_sync(void) {
  // Set whitelist before start advertising
  rfble_sync_whitelist_with_bonded_devs();

  /* Begin advertising. */
  rfble_advertise();
}

static void rfble_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");

  /* This function will return only when nimble_port_stop() is executed */
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void rfble_begin(rfble_opts_t* opts) {
  int rc;

  memcpy(&rfble_opts, opts, sizeof(rfble_opts_t));

  /* NVS is required at this point, but must be initialized by caller. */
  nimble_port_init();

  /* Initialize the NimBLE host configuration. */
  ble_hs_cfg.reset_cb = rfble_on_reset;
  ble_hs_cfg.sync_cb = rfble_on_sync;
  ble_hs_cfg.gatts_register_cb = rfble_gatt_register_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  ble_hs_cfg.sm_io_cap = opts->io_cap;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_oob_data_flag = 0;

  // Id key exchange is required for mutual RPA resolution.
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;



  rc = rfble_gatt_init();
  assert(rc == 0);

  /* Set the default device name. */
  if (opts->device_name != NULL) {
    rc = ble_svc_gap_device_name_set(opts->device_name);
  }
  assert(rc == 0);


  /* XXX Need to have template for store */
  ble_store_config_init();
  nimble_port_freertos_init(rfble_host_task);
}
