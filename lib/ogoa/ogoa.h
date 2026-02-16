#ifndef OGOA_H
#define OGOA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 0       1       2       3       4 ... N      N+1
+-------+-------+-------+-------+------------+-------+
| Start |  Seq  | Type  |  Len  |  Payload   | Chksum|
+-------+-------+-------+-------+------------+-------+
*/

#define OGOA_FRAME_MAX_BYTES 256u
#define OGOA_HEADER_BYTES 4u
#define OGOA_CHECKSUM_BYTES 1u
#define OGOA_MAX_PAYLOAD (OGOA_FRAME_MAX_BYTES - OGOA_HEADER_BYTES - OGOA_CHECKSUM_BYTES)

#define OGOA_START_BYTE 0x27u

#define OGOA_TYPE_STATUS_REQUEST 0x4Bu
#define OGOA_TYPE_STATUS_RESPONSE 0xB4u
#define OGOA_TYPE_ACK 0x67u
#define OGOA_TYPE_LIDAR_SEND 0xAAu

#define OGOA_ACK_TIMEOUT_MS 100u
#define OGOA_STATUS_LOOP_INTERVAL_MS 250u

typedef enum {
    OGOA_OK = 0,
    OGOA_ERR_BAD_ARG = -1,
    OGOA_ERR_PAYLOAD_TOO_LARGE = -2,
    OGOA_ERR_TX_FAILED = -3,
    OGOA_ERR_CHECKSUM = -4
} ogoa_err_t;

typedef struct {
    uint8_t seq;
    uint8_t type;
    uint8_t len;
    uint8_t payload[OGOA_MAX_PAYLOAD];
} ogoa_frame_t;

typedef int (*ogoa_tx_fn)(void *user_ctx, const uint8_t *data, size_t len);
typedef void (*ogoa_rx_fn)(void *user_ctx, const ogoa_frame_t *frame);
typedef void (*ogoa_error_fn)(void *user_ctx, ogoa_err_t err);

typedef struct {
    ogoa_tx_fn tx;
    ogoa_rx_fn on_frame;
    ogoa_error_fn on_error;
} ogoa_ops_t;

typedef struct {
    ogoa_ops_t ops;
    void *user_ctx;

    uint8_t next_seq;

    uint8_t tx_frame[OGOA_FRAME_MAX_BYTES];
    size_t tx_len;
    uint8_t tx_waiting_ack;
    uint8_t tx_retried_once;
    uint8_t tx_pending_seq;
    uint8_t tx_status_loop;
    uint32_t tx_last_action_ms;

    uint8_t rx_buf[OGOA_FRAME_MAX_BYTES];
    uint8_t rx_index;
    uint8_t rx_expected_payload_len;
    uint8_t rx_state;

    uint8_t have_last_non_ack;
    uint8_t last_non_ack_seq;
    uint8_t last_non_ack_type;
    uint8_t last_non_ack_len;
    uint8_t last_non_ack_crc;
} ogoa_ctx_t;

void ogoa_init(ogoa_ctx_t *ctx, const ogoa_ops_t *ops, void *user_ctx);

ogoa_err_t ogoa_send(ogoa_ctx_t *ctx, uint8_t type, const uint8_t *payload, uint8_t len, uint32_t now_ms);
void ogoa_process_byte(ogoa_ctx_t *ctx, uint8_t byte, uint32_t now_ms);
void ogoa_tick(ogoa_ctx_t *ctx, uint32_t now_ms);

size_t ogoa_build_frame_bytes(uint8_t seq, uint8_t type, const uint8_t *payload, uint8_t len, uint8_t *out_frame);
uint8_t ogoa_calc_checksum(const uint8_t *frame_without_checksum, size_t len_without_checksum);

#ifdef __cplusplus
}
#endif

#endif
