#include "clemsacode.h"
#include "esp_err.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "bluetooth/bt_spp_bluedroid.h"
#include "private.h"
#include <stdint.h>
#include <string.h>

#define TAG "garage_door"
#define MAX_BT_QUEUE_LEN 10
#define ITEM_SIZE sizeof(struct bt_spp_bluedroid_msg_event)
#define RF_GPIO GPIO_NUM_14

#define CMD_HOME_GARAGE_ENTER "home-garage-enter"
#define CMD_HOME_GARAGE_EXIT "home-garage-exit"

#define CMD_PARENTS_GARAGE_LEFT "parents-garage-left"
#define CMD_PARENTS_GARAGE_RIGHT "parents-garage-right"
#define CMD_NOP "nop"

#define CMD_RESP_ERR_INV_CMD "ERR_INV_CMD"
#define CMD_RESP_ERR_BUSY "ERR_BUSY"
#define CMD_RESP_OK "OK"
#define CMD_RESP_OK_PREFIX "OK:"

static StaticQueue_t bt_rx_queue_holder;
static uint8_t bt_rx_queue_storage[MAX_BT_QUEUE_LEN * ITEM_SIZE];

static StaticQueue_t bt_tx_queue_holder;
static uint8_t bt_tx_queue_storage[MAX_BT_QUEUE_LEN * ITEM_SIZE];

static StaticQueue_t confirmation_send_queue_holder;
static uint8_t confirmation_send_queue_storage[1 * sizeof(uint32_t)];
static QueueHandle_t confirmation_send_queue;

static struct clemsa_codegen generator;
static struct clemsa_codegen_tx tx;
static uint32_t last_tx_requester;


static void bt_send_string_msg(uint32_t handle, char* text) {
  struct bt_spp_bluedroid_send_msg msg;
  strcpy((char*)msg.buffer, text);
  msg.data_len = strlen(text) + 1; // Will add an extra new line to finish the message.
  msg.buffer[msg.data_len - 1] = '\n';
  msg.handle = handle;

  for (size_t i = 0 ; i < msg.data_len; i++) {
    ESP_LOGI(TAG, "ch: %d", msg.buffer[i]);
  }
  bt_spp_send_msg(&msg);
}

static void clemsa_codegen_tx_cb(struct clemsa_codegen_tx* tx) {
  xQueueSend(confirmation_send_queue, &last_tx_requester, portMAX_DELAY);
}

void send_code_sent_confirmation_task(void* arg) {
  uint32_t handle;

  while (1) {
    if (xQueueReceive(confirmation_send_queue, &handle, portMAX_DELAY) == pdTRUE) {
      ESP_LOGI(TAG, "Will sent code sent confirmation");
      bt_send_string_msg(handle, CMD_RESP_OK);
    } else {
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}

void app_main(void) {
  QueueHandle_t bt_rx_queue;
  QueueHandle_t bt_tx_queue;
  struct bt_spp_bluedroid_msg_event bt_evt;

  ESP_ERROR_CHECK(clemsa_codegen_init(&generator, RF_GPIO));

  generator.done_callback = clemsa_codegen_tx_cb;

  tx.repetition_count = 10;
  tx.code_len = CLEMSA_CODEGEN_DEFAULT_CODE_SIZE;

  bt_rx_queue = xQueueCreateStatic(MAX_BT_QUEUE_LEN, ITEM_SIZE, bt_rx_queue_storage, &bt_rx_queue_holder);
  bt_tx_queue = xQueueCreateStatic(MAX_BT_QUEUE_LEN, ITEM_SIZE, bt_tx_queue_storage, &bt_tx_queue_holder);
  confirmation_send_queue = xQueueCreateStatic(1, sizeof(uint32_t), confirmation_send_queue_storage, &confirmation_send_queue_holder);

  xTaskCreate
    (
     send_code_sent_confirmation_task,
     "Confirmation callback task",
     4*1024, NULL, 10, NULL);

  struct bt_spp_bluedroid_config bt_config = {
    .recv_queue = bt_rx_queue,
    .send_queue = bt_tx_queue
  };

  bt_spp_bluedroid_init(&bt_config);

  while (1) {
    if (xQueueReceive(bt_rx_queue, &bt_evt, portMAX_DELAY) == pdFALSE) {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    ESP_LOGI(TAG, "Received Bluetooth message: %s", bt_evt.buffer);
    if (strcmp(CMD_NOP, bt_evt.buffer) == 0) {
      ESP_LOGI(TAG, "Received NOP");
      // Do nothing, just for check if the connection is still alive.
    } else if (strcmp(CMD_HOME_GARAGE_ENTER, bt_evt.buffer) == 0) {
      ESP_LOGI(TAG, "Handling " CMD_HOME_GARAGE_ENTER " command");
      if (generator.busy) {
	bt_send_string_msg(bt_evt.handle, CMD_RESP_ERR_BUSY);
      } else {
	tx.code = HOME_GARAGE_ENTER_CODE;
	tx.code_name = "Home Enter Garage";
	last_tx_requester = bt_evt.handle;
	clemsa_codegen_begin_tx(&generator, &tx);
      }
    } else if (strcmp(CMD_HOME_GARAGE_EXIT, bt_evt.buffer) == 0) {
      ESP_LOGI(TAG, "Handling " CMD_HOME_GARAGE_EXIT " command");
      if (generator.busy) {
	bt_send_string_msg(bt_evt.handle, CMD_RESP_ERR_BUSY);
      } else {
	tx.code = HOME_GARAGE_EXIT_CODE;
	tx.code_name = "Home Exit Garage";
	last_tx_requester = bt_evt.handle;
	clemsa_codegen_begin_tx(&generator, &tx);
      }
    } else if (strcmp(CMD_PARENTS_GARAGE_LEFT, bt_evt.buffer) == 0) {
      ESP_LOGI(TAG, "Handling " CMD_PARENTS_GARAGE_LEFT " command");
      if (generator.busy) {
	bt_send_string_msg(bt_evt.handle, CMD_RESP_ERR_BUSY);
      } else {
	tx.code = PARENTS_GARAGE_ENTER_CODE;
	tx.code_name = "Parents Garage Left";
	last_tx_requester = bt_evt.handle;
	clemsa_codegen_begin_tx(&generator, &tx);
      }
    } else if (strcmp(CMD_PARENTS_GARAGE_RIGHT, bt_evt.buffer) == 0) {
      ESP_LOGI(TAG, "Handling " CMD_PARENTS_GARAGE_RIGHT " command");
      if (generator.busy) {
	bt_send_string_msg(bt_evt.handle, CMD_RESP_ERR_BUSY);
      } else {
	tx.code = PARENTS_GARAGE_ENTER_CODE;
	tx.code_name = "Parents Garage RIGHT";
	last_tx_requester = bt_evt.handle;
	clemsa_codegen_begin_tx(&generator, &tx);
      }
    } else {
      ESP_LOGW(TAG, "Received unknown command");
      bt_send_string_msg(bt_evt.handle, CMD_RESP_ERR_INV_CMD);
    }
  }
}
