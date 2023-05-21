/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include "rfble.h"

#define CHR_SECURE_READ_FLAGS                                                      \
  BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_READ_AUTHEN | \
      BLE_GATT_CHR_F_READ_AUTHOR


#define CHR_SECURE_WRITE_FLAGS                                                 \
  BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |                            \
      BLE_GATT_CHR_F_WRITE_AUTHEN | BLE_GATT_CHR_F_WRITE_AUTHOR

#define DSC_SECURE_READ_FLAGS                                                  \
  BLE_ATT_F_READ | BLE_ATT_F_READ_AUTHEN | BLE_ATT_F_READ_ENC |                \
      BLE_ATT_F_READ_AUTHOR

#define BLE_GATT_END { 0 }

#define DSC_CHARACTERISTIC_NAME(name) \
  {						\
    .uuid = &gatt_chr_name_uuid.u,		\
    .att_flags = DSC_SECURE_READ_FLAGS,		\
    .access_cb = gatt_svr_chr_access_sec_test,	\
    .arg = name,				\
    .min_key_size = 0				\
  }

static int
gatt_svr_chr_access_sec_test(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt,
                             void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &rfble_gatt_svc_rf_companion_uuid.u,
    .characteristics =
    (struct ble_gatt_chr_def[]){
      {
	.uuid = &rfble_gatt_chr_send_rf_uuid.u,
	.access_cb = gatt_svr_chr_access_sec_test,
	.flags = CHR_SECURE_WRITE_FLAGS,
	.min_key_size = 0,
	.val_handle = NULL,
	.descriptors = (struct ble_gatt_dsc_def[]){
	  DSC_CHARACTERISTIC_NAME("Send pre-stored RF signal"),
	  BLE_GATT_END
	},
      },
      {
	.uuid = &rfble_gatt_chr_antenna_state_uuid.u,
	.access_cb = gatt_svr_chr_access_sec_test,
	.flags = BLE_GATT_CHR_PROP_NOTIFY | CHR_SECURE_READ_FLAGS,
	.min_key_size = 0,
	.val_handle = &rf_ble_state.rfble_gatt_chr_antenna_state_handle,
	.descriptors = (struct ble_gatt_dsc_def[]){
	  DSC_CHARACTERISTIC_NAME("RF Antenna busy indicator"),
	  BLE_GATT_END
	},
      },
      BLE_GATT_END
    },
  },
  BLE_GATT_END
};


static int gatt_svr_chr_write(struct os_mbuf *om, uint16_t min_len, uint16_t max_len,
			      void *dst, uint16_t *len) {
  uint16_t om_len;
  int rc;

  om_len = OS_MBUF_PKTLEN(om);
  if (om_len < min_len || om_len > max_len) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  rc = ble_hs_mbuf_to_flat(om, dst, max_len, len);
  if (rc != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  return 0;
}

static int rfble_gatt_push(struct ble_gatt_access_ctxt *ctxt, void* value, size_t len) {
  return os_mbuf_append(ctxt->om, value, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int rfble_gatt_notif(uint16_t conn_handle, uint16_t attr_handle, void* value, size_t len) {
  struct os_mbuf *om = ble_hs_mbuf_from_flat(value, sizeof(uint8_t));
  return ble_gatts_notify_custom(conn_handle, attr_handle, om);
}

int rfble_gatt_push8(struct ble_gatt_access_ctxt *ctxt, uint8_t value) {
  return rfble_gatt_push(ctxt, &value, sizeof(uint8_t));
}

int rfble_gatt_recv8(struct ble_gatt_access_ctxt *ctxt, uint8_t* value) {
  return gatt_svr_chr_write(ctxt->om, sizeof(uint8_t), sizeof(uint8_t), value, NULL);
}

int rfble_gatt_notif8(uint16_t att_handle, uint8_t value) {
  uint16_t conn = rf_ble_state.conn_handle;
  if (conn == 0) {
    return BLE_HS_ENOTCONN;
  }

  return rfble_gatt_notif(conn, att_handle, &value, sizeof(uint8_t));
}

static int gatt_svr_chr_access_sec_test(uint16_t conn_handle, uint16_t attr_handle,
					struct ble_gatt_access_ctxt *ctxt,
					void *arg) {

    char buf[BLE_UUID_STR_LEN];
    MODLOG_DFLT(INFO, "Access to UUID %s",
  	      ble_uuid_to_str(ctxt->chr->uuid, buf));
  switch (ctxt->op) {
  case BLE_GATT_ACCESS_OP_READ_DSC: {
    if (ble_uuid_cmp(ctxt->dsc->uuid, &gatt_chr_name_uuid.u) == 0) {
      return rfble_gatt_push(ctxt, arg, strlen(arg));
    }
  }
    break;

  case BLE_GATT_ACCESS_OP_READ_CHR:
    if (rf_ble_opts.gatt_read_cb != NULL) {
      return rf_ble_opts.gatt_read_cb(ctxt, ctxt->chr->uuid);
    }

  case BLE_GATT_ACCESS_OP_WRITE_CHR:
    if (rf_ble_opts.gatt_write_cb != NULL) {
      return rf_ble_opts.gatt_write_cb(ctxt, ctxt->chr->uuid);
    }

    break;
  }

  return BLE_ATT_ERR_UNLIKELY;
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
  char buf[BLE_UUID_STR_LEN];

  switch (ctxt->op) {
  case BLE_GATT_REGISTER_OP_SVC:
    MODLOG_DFLT(DEBUG, "registered service %s with handle=%d\n",
		ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
		ctxt->svc.handle);
    break;

  case BLE_GATT_REGISTER_OP_CHR:
    MODLOG_DFLT(DEBUG, "registering characteristic %s with "
		"def_handle=%d val_handle=%d\n",
		ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
		ctxt->chr.def_handle,
		ctxt->chr.val_handle);
    break;

  case BLE_GATT_REGISTER_OP_DSC:
    MODLOG_DFLT(DEBUG, "registering descriptor %s with handle=%d\n",
		ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
		ctxt->dsc.handle);
    break;

  default:
    assert(0);
    break;
  }
}

int gatt_svr_init(void) {
  int rc;

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_ans_init();

  rc = ble_gatts_count_cfg(gatt_svr_svcs);
  if (rc != 0) {
    return rc;
  }

  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  if (rc != 0) {
    return rc;
  }

  return 0;
}
