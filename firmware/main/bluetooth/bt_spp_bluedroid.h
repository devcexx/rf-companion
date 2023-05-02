#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/queue.h"

#define BT_SPP_PEER_RECV_BUFFER_SIZE 256
#define BT_SPP_PEER_SEND_BUFFER_SIZE 256
#define BT_SPP_SEND_QUEUE_SIZE 10

struct bt_spp_bluedroid_peer {
  uint32_t handle;
  uint8_t buffer[BT_SPP_PEER_RECV_BUFFER_SIZE];
  size_t data_len;
  bool cong;
  bool write_ready;
};

struct bt_spp_bluedroid_msg_event {
  uint32_t handle;
  char buffer[BT_SPP_PEER_RECV_BUFFER_SIZE];
  size_t data_len;
};

struct bt_spp_bluedroid_send_msg {
  uint32_t handle;
  uint8_t buffer[BT_SPP_PEER_SEND_BUFFER_SIZE];
  size_t data_len;
};

struct bt_spp_bluedroid_config {
  QueueHandle_t recv_queue;
  QueueHandle_t send_queue;
};

void bt_spp_bluedroid_init(struct bt_spp_bluedroid_config* config);
void bt_spp_peer_reset();
void bt_spp_send_msg(struct bt_spp_bluedroid_send_msg *msg);
