/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <inttypes.h>
#include "freertos/projdefs.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#include "time.h"
#include "sys/time.h"
#include "driver/gpio.h"

#include "bt_spp_bluedroid.h"

#define SPP_TAG "bt_spp"
#define SPP_SERVER_NAME "RF_COMPANION"
#define DEVICE_NAME "garage-remote"

static struct bt_spp_bluedroid_peer peer = {0};
static struct bt_spp_bluedroid_config bt_spp_config = {0};

static const esp_spp_mode_t esp_spp_mode = ESP_SPP_MODE_CB;
static const bool esp_spp_enable_l2cap_ertm = true;

static struct timeval time_new, time_old;
static long data_num = 0;

static const esp_spp_sec_t sec_mask = ESP_SPP_SEC_AUTHENTICATE;
static const esp_spp_role_t role_slave = ESP_SPP_ROLE_SLAVE;

static char *bda2str(uint8_t * bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

void bt_spp_peer_reset() {
  peer.handle = 0;
  peer.data_len = 0;
  peer.cong = false;
  peer.write_ready = true;
}

static bool bt_spp_poll_send_queue() {
  struct bt_spp_bluedroid_send_msg msg;

  if (xQueueReceive(bt_spp_config.send_queue, &msg, 0) == pdTRUE) {
    ESP_LOGI(SPP_TAG, "Polled message from send queue");
    ESP_ERROR_CHECK(esp_spp_write(msg.handle, msg.data_len, msg.buffer));
    return true;
  }

  ESP_LOGI(SPP_TAG, "Polled send queue but it was empty");
  return false;
}

static struct bt_spp_bluedroid_peer* bt_spp_alloc_peer(uint32_t handle) {
  if (peer.handle != handle) {
    if (peer.handle != 0) {
      ESP_LOGW(SPP_TAG, "Disconnecting previous client with handle: %" PRIu32, peer.handle);
      esp_spp_disconnect(peer.handle);
    }
    bt_spp_peer_reset();
    peer.handle = handle;
  }

  return &peer;
}
static bool bt_spp_peer_clear_to_send(struct bt_spp_bluedroid_peer* peer) {
  return peer->write_ready && !peer->cong;
}

void process_peer_buffer(struct bt_spp_bluedroid_peer* peer) {
  struct bt_spp_bluedroid_msg_event evt;

  char* buf = (char*) peer->buffer;
  char ch;
  evt.handle = peer->handle;
  size_t cur_cmd_start = 0; // Beginning of next command, inclusive

  ESP_LOGI(SPP_TAG, "Contents of peer buffer before scan: %zd", peer->data_len);
  ESP_LOG_BUFFER_HEXDUMP(SPP_TAG, peer->buffer, peer->data_len, ESP_LOG_INFO);

  for (size_t i = 0; i < peer->data_len; i++) {
    ch = buf[i];
    if (ch == '\r') {
      // Command found between cur_cmd_start and i
      evt.data_len = i - cur_cmd_start;
      memcpy(evt.buffer, buf + cur_cmd_start, evt.data_len);
      evt.buffer[evt.data_len] = '\0';
      cur_cmd_start = i + 1;

      ESP_LOGI(SPP_TAG, "Will emit message received event with data:");
      ESP_LOG_BUFFER_HEXDUMP(SPP_TAG, evt.buffer, evt.data_len + 1, ESP_LOG_INFO);

      if (xQueueSend(bt_spp_config.recv_queue, &evt, 0) != pdTRUE) {
	ESP_LOGE(SPP_TAG, "Cannot handle next SPP message! Queue overflow! Discarding!");
      }
    }
  }

  if (cur_cmd_start < peer->data_len) {
    memmove(peer->buffer, peer->buffer + cur_cmd_start, peer->data_len - cur_cmd_start);
  } else {
    peer->data_len = 0;
  }

  ESP_LOGI(SPP_TAG, "Contents of peer buffer after scan:");
  ESP_LOG_BUFFER_HEXDUMP(SPP_TAG, peer->buffer, peer->data_len, ESP_LOG_INFO);
}

static void esp_spp_handle_msg(struct spp_data_ind_evt_param* param) {

  struct bt_spp_bluedroid_peer* peer = bt_spp_alloc_peer(param->handle);

  if (peer->data_len + param->len >= BT_SPP_PEER_RECV_BUFFER_SIZE) {
    esp_spp_disconnect(peer->handle);
    ESP_LOGE(SPP_TAG, "Client %" PRIu32 " did overflow the reception buffer. Disconnected", peer->handle);
    return;
  }

  if (param->len == 0) {
    return;
  }

  memcpy(peer->buffer + peer->data_len, param->data, param->len);
  peer->data_len += param->len;

  process_peer_buffer(peer);
}

static void esp_spp_handle_cong(struct spp_cong_evt_param* param) {
  struct bt_spp_bluedroid_peer* peer = bt_spp_alloc_peer(param->handle);
  peer->cong = param->cong;

  if (bt_spp_peer_clear_to_send(peer)) {
    bt_spp_poll_send_queue();
  }
}

static void esp_spp_handle_write_event(struct spp_write_evt_param* param) {
  struct bt_spp_bluedroid_peer* peer = bt_spp_alloc_peer(param->handle);
  peer->write_ready = true;
  peer->cong = param->cong;

  if (bt_spp_peer_clear_to_send(peer)) {
    bt_spp_poll_send_queue();
  }
}

void bt_spp_send_msg(struct bt_spp_bluedroid_send_msg *msg) {
  ESP_LOGI(SPP_TAG, "Will try to send");
  struct bt_spp_bluedroid_peer* peer = bt_spp_alloc_peer(msg->handle);
  ESP_LOGI(SPP_TAG, "Write ready? %d; Cong? %d", peer->write_ready, peer->cong);
  if (bt_spp_peer_clear_to_send(peer)) {
    ESP_LOGI(SPP_TAG, "Clear to send to peer %" PRIu32, peer->handle);
    ESP_ERROR_CHECK(esp_spp_write(peer->handle, msg->data_len, msg->buffer));
  } else if (xQueueSend(bt_spp_config.send_queue, msg, 0) != pdTRUE) {
    ESP_LOGE(SPP_TAG, "Cannot schedule message for sending. Send queue is full!");
  } else {
    ESP_LOGI(SPP_TAG, "Message scheduled to be sent");
  }
}

static void esp_spp_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(SPP_TAG, "ESP_SPP_INIT_EVT");
            esp_spp_start_srv(sec_mask, role_slave, 0, SPP_SERVER_NAME);
        } else {
            ESP_LOGE(SPP_TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
        }
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT");
        break;
    case ESP_SPP_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT");
        break;
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CLOSE_EVT status:%d handle:%"PRIu32" close_by_remote:%d", param->close.status,
                 param->close.handle, param->close.async);
        break;
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGI(SPP_TAG, "ESP_SPP_START_EVT handle:%"PRIu32" sec_id:%d scn:%d", param->start.handle, param->start.sec_id,
                     param->start.scn);
            esp_bt_dev_set_device_name(DEVICE_NAME);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
        } else {
            ESP_LOGE(SPP_TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
        }
        break;
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CL_INIT_EVT");
        break;
    case ESP_SPP_DATA_IND_EVT:
      ESP_LOGI(SPP_TAG, "DATA IND peer %" PRIu32, param->data_ind.handle);
      esp_spp_handle_msg(&param->data_ind);
        break;
    case ESP_SPP_CONG_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_CONG_EVT");
	esp_spp_handle_cong(&param->cong);
        break;
    case ESP_SPP_WRITE_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_WRITE_EVT");
	esp_spp_handle_write_event(&param->write);
        break;
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_OPEN_EVT status:%d handle:%"PRIu32", rem_bda:[%s]", param->srv_open.status,
                 param->srv_open.handle, bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        gettimeofday(&time_old, NULL);
        break;
    case ESP_SPP_SRV_STOP_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_SRV_STOP_EVT");
        break;
    case ESP_SPP_UNINIT_EVT:
        ESP_LOGI(SPP_TAG, "ESP_SPP_UNINIT_EVT");
        break;
    default:
        break;
    }
}

void esp_bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:{
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(SPP_TAG, "authentication success: %s bda:[%s]", param->auth_cmpl.device_name,
                     bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        } else {
            ESP_LOGE(SPP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT:{
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(SPP_TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(SPP_TAG, "Input pin code: 1234");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '1';
            pin_code[1] = '2';
            pin_code[2] = '3';
            pin_code[3] = '4';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;
    }

#if (CONFIG_BT_SSP_ENABLED == true)
    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %"PRIu32, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%"PRIu32, param->key_notif.passkey);
        break;
    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!");
        break;
#endif

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(SPP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d bda:[%s]", param->mode_chg.mode,
                 bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str)));
        break;

    default: {
        ESP_LOGI(SPP_TAG, "event: %d", event);
        break;
    }
    }
    return;
}

void bt_spp_bluedroid_init(struct bt_spp_bluedroid_config* cfg) {

  char bda_str[18] = {0};
  esp_err_t ret = nvs_flash_init();

  memcpy(&bt_spp_config, cfg, sizeof(struct bt_spp_bluedroid_config));
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK( ret );

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  if ((ret = esp_bluedroid_init()) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  if ((ret = esp_bluedroid_enable()) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  if ((ret = esp_bt_gap_register_callback(esp_bt_gap_cb)) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s gap register failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  if ((ret = esp_spp_register_callback(esp_spp_cb)) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s spp register failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  esp_spp_cfg_t bt_spp_cfg = {
    .mode = esp_spp_mode,
    .enable_l2cap_ertm = esp_spp_enable_l2cap_ertm,
    .tx_buffer_size = 0, /* Only used for ESP_SPP_MODE_VFS mode */
  };
  if ((ret = esp_spp_enhanced_init(&bt_spp_cfg)) != ESP_OK) {
    ESP_LOGE(SPP_TAG, "%s spp init failed: %s\n", __func__, esp_err_to_name(ret));
    return;
  }

  /* Set default parameters for Secure Simple Pairing */
  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
  esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

  /*
   * Set default parameters for Legacy Pairing
   * Use variable pin, input pin code when pairing
   */
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
  esp_bt_pin_code_t pin_code;
  esp_bt_gap_set_pin(pin_type, 0, pin_code);

  ESP_LOGI(SPP_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));
}
