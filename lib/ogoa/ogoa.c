#include "ogoa.h"

#include <string.h>

enum {
    RX_WAIT_START = 0,
    RX_WAIT_SEQ = 1,
    RX_WAIT_TYPE = 2,
    RX_WAIT_LEN = 3,
    RX_WAIT_PAYLOAD = 4,
    RX_WAIT_CHECKSUM = 5
};

static uint8_t frame_crc_fingerprint(const ogoa_frame_t *frame);
static void emit_error(ogoa_ctx_t *ctx, ogoa_err_t err);
static int send_raw(ogoa_ctx_t *ctx, const uint8_t *data, size_t len);
static int send_ack(ogoa_ctx_t *ctx, uint8_t seq);
static int dispatch_frame(ogoa_ctx_t *ctx, const ogoa_frame_t *frame);
static uint8_t is_duplicate_non_ack(const ogoa_ctx_t *ctx, const ogoa_frame_t *frame);
static void remember_non_ack(ogoa_ctx_t *ctx, const ogoa_frame_t *frame);

void ogoa_init(ogoa_ctx_t *ctx, const ogoa_ops_t *ops, void *user_ctx)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    if (ops != NULL) {
        ctx->ops = *ops;
    }
    ctx->user_ctx = user_ctx;
    ctx->rx_state = RX_WAIT_START;
}

uint8_t ogoa_calc_checksum(const uint8_t *frame_without_checksum, size_t len_without_checksum)
{
    size_t i;
    uint8_t crc = 0u;

    if (frame_without_checksum == NULL) {
        return 0u;
    }

    for (i = 0; i < len_without_checksum; ++i) {
        crc ^= frame_without_checksum[i];
    }
    return crc;
}

size_t ogoa_build_frame_bytes(uint8_t seq, uint8_t type, const uint8_t *payload, uint8_t len, uint8_t *out_frame)
{
    size_t total_len;

    if (out_frame == NULL || len > OGOA_MAX_PAYLOAD) {
        return 0u;
    }

    total_len = (size_t)OGOA_HEADER_BYTES + (size_t)len + (size_t)OGOA_CHECKSUM_BYTES;

    out_frame[0] = OGOA_START_BYTE;
    out_frame[1] = seq;
    out_frame[2] = type;
    out_frame[3] = len;
    if (len > 0u && payload != NULL) {
        memcpy(&out_frame[4], payload, len);
    }
    out_frame[4u + len] = ogoa_calc_checksum(out_frame, total_len - 1u);

    return total_len;
}

ogoa_err_t ogoa_send(ogoa_ctx_t *ctx, uint8_t type, const uint8_t *payload, uint8_t len, uint32_t now_ms)
{
    size_t frame_len;
    uint8_t seq;
    int sent;

    if (ctx == NULL || ctx->ops.tx == NULL) {
        return OGOA_ERR_BAD_ARG;
    }
    if (len > OGOA_MAX_PAYLOAD || (len > 0u && payload == NULL)) {
        return OGOA_ERR_PAYLOAD_TOO_LARGE;
    }
    if (ctx->tx_waiting_ack || ctx->tx_status_loop) {
        return OGOA_ERR_TX_FAILED;
    }

    seq = ctx->next_seq;
    frame_len = ogoa_build_frame_bytes(seq, type, payload, len, ctx->tx_frame);
    if (frame_len == 0u) {
        return OGOA_ERR_TX_FAILED;
    }

    sent = send_raw(ctx, ctx->tx_frame, frame_len);
    if (!sent) {
        return OGOA_ERR_TX_FAILED;
    }

    ctx->tx_len = frame_len;
    ctx->tx_last_action_ms = now_ms;
    ctx->tx_pending_seq = seq;
    ctx->tx_waiting_ack = (uint8_t)(type != OGOA_TYPE_ACK);
    ctx->tx_retried_once = 0u;
    ctx->next_seq = (uint8_t)(ctx->next_seq + 1u);

    return OGOA_OK;
}

void ogoa_tick(ogoa_ctx_t *ctx, uint32_t now_ms)
{
    uint32_t elapsed;

    if (ctx == NULL) {
        return;
    }

    if (ctx->tx_waiting_ack) {
        elapsed = now_ms - ctx->tx_last_action_ms;
        if (elapsed < OGOA_ACK_TIMEOUT_MS) {
            return;
        }

        if (!ctx->tx_retried_once) {
            if (send_raw(ctx, ctx->tx_frame, ctx->tx_len)) {
                ctx->tx_retried_once = 1u;
                ctx->tx_last_action_ms = now_ms;
            } else {
                emit_error(ctx, OGOA_ERR_TX_FAILED);
            }
            return;
        }

        ctx->tx_waiting_ack = 0u;
        ctx->tx_status_loop = 1u;
        ctx->tx_last_action_ms = now_ms;
    }

    if (ctx->tx_status_loop) {
        static const uint8_t no_payload = 0u;
        if ((now_ms - ctx->tx_last_action_ms) >= OGOA_STATUS_LOOP_INTERVAL_MS) {
            size_t len = ogoa_build_frame_bytes(ctx->next_seq, OGOA_TYPE_STATUS_REQUEST, &no_payload, 0u, ctx->tx_frame);
            if (len > 0u && send_raw(ctx, ctx->tx_frame, len)) {
                ctx->tx_len = len;
                ctx->tx_pending_seq = ctx->next_seq;
                ctx->next_seq = (uint8_t)(ctx->next_seq + 1u);
                ctx->tx_last_action_ms = now_ms;
            } else {
                emit_error(ctx, OGOA_ERR_TX_FAILED);
            }
        }
    }
}

void ogoa_process_byte(ogoa_ctx_t *ctx, uint8_t byte, uint32_t now_ms)
{
    ogoa_frame_t frame;
    uint8_t expected_crc;
    uint8_t received_crc;
    uint8_t duplicate;

    if (ctx == NULL) {
        return;
    }

    switch (ctx->rx_state) {
    case RX_WAIT_START:
        if (byte == OGOA_START_BYTE) {
            ctx->rx_buf[0] = byte;
            ctx->rx_index = 1u;
            ctx->rx_state = RX_WAIT_SEQ;
        }
        break;

    case RX_WAIT_SEQ:
        ctx->rx_buf[ctx->rx_index++] = byte;
        ctx->rx_state = RX_WAIT_TYPE;
        break;

    case RX_WAIT_TYPE:
        ctx->rx_buf[ctx->rx_index++] = byte;
        ctx->rx_state = RX_WAIT_LEN;
        break;

    case RX_WAIT_LEN:
        ctx->rx_buf[ctx->rx_index++] = byte;
        ctx->rx_expected_payload_len = byte;
        if (ctx->rx_expected_payload_len > OGOA_MAX_PAYLOAD) {
            ctx->rx_state = RX_WAIT_START;
            ctx->rx_index = 0u;
            emit_error(ctx, OGOA_ERR_PAYLOAD_TOO_LARGE);
        } else if (ctx->rx_expected_payload_len == 0u) {
            ctx->rx_state = RX_WAIT_CHECKSUM;
        } else {
            ctx->rx_state = RX_WAIT_PAYLOAD;
        }
        break;

    case RX_WAIT_PAYLOAD:
        ctx->rx_buf[ctx->rx_index++] = byte;
        if (ctx->rx_index == (uint8_t)(4u + ctx->rx_expected_payload_len)) {
            ctx->rx_state = RX_WAIT_CHECKSUM;
        }
        break;

    case RX_WAIT_CHECKSUM:
        ctx->rx_buf[ctx->rx_index++] = byte;
        expected_crc = ogoa_calc_checksum(ctx->rx_buf, (size_t)ctx->rx_index - 1u);
        received_crc = ctx->rx_buf[ctx->rx_index - 1u];

        if (expected_crc == received_crc) {
            frame.seq = ctx->rx_buf[1];
            frame.type = ctx->rx_buf[2];
            frame.len = ctx->rx_buf[3];
            if (frame.len > 0u) {
                memcpy(frame.payload, &ctx->rx_buf[4], frame.len);
            }

            if (frame.type == OGOA_TYPE_ACK && frame.len == 0u) {
                if (ctx->tx_waiting_ack && frame.seq == ctx->tx_pending_seq) {
                    ctx->tx_waiting_ack = 0u;
                    ctx->tx_retried_once = 0u;
                }
            } else {
                if (send_ack(ctx, frame.seq)) {
                    duplicate = is_duplicate_non_ack(ctx, &frame);
                    if (!duplicate) {
                        remember_non_ack(ctx, &frame);
                        dispatch_frame(ctx, &frame);
                    }
                    if (ctx->tx_status_loop && frame.type == OGOA_TYPE_STATUS_RESPONSE) {
                        ctx->tx_status_loop = 0u;
                    }
                    ctx->tx_last_action_ms = now_ms;
                } else {
                    emit_error(ctx, OGOA_ERR_TX_FAILED);
                }
            }
        } else {
            emit_error(ctx, OGOA_ERR_CHECKSUM);
        }

        ctx->rx_state = RX_WAIT_START;
        ctx->rx_index = 0u;
        break;

    default:
        ctx->rx_state = RX_WAIT_START;
        ctx->rx_index = 0u;
        break;
    }
}

static uint8_t frame_crc_fingerprint(const ogoa_frame_t *frame)
{
    uint8_t fp;
    uint8_t i;

    fp = (uint8_t)(frame->seq ^ frame->type ^ frame->len);
    for (i = 0u; i < frame->len; ++i) {
        fp ^= frame->payload[i];
    }
    return fp;
}

static void emit_error(ogoa_ctx_t *ctx, ogoa_err_t err)
{
    if (ctx != NULL && ctx->ops.on_error != NULL) {
        ctx->ops.on_error(ctx->user_ctx, err);
    }
}

static int send_raw(ogoa_ctx_t *ctx, const uint8_t *data, size_t len)
{
    int written;

    written = ctx->ops.tx(ctx->user_ctx, data, len);
    return written == (int)len;
}

static int send_ack(ogoa_ctx_t *ctx, uint8_t seq)
{
    uint8_t frame[OGOA_FRAME_MAX_BYTES];
    size_t len;

    len = ogoa_build_frame_bytes(seq, OGOA_TYPE_ACK, NULL, 0u, frame);
    if (len == 0u) {
        return 0;
    }
    return send_raw(ctx, frame, len);
}

static int dispatch_frame(ogoa_ctx_t *ctx, const ogoa_frame_t *frame)
{
    if (ctx->ops.on_frame != NULL) {
        ctx->ops.on_frame(ctx->user_ctx, frame);
    }
    return 1;
}

static uint8_t is_duplicate_non_ack(const ogoa_ctx_t *ctx, const ogoa_frame_t *frame)
{
    if (!ctx->have_last_non_ack) {
        return 0u;
    }
    if (ctx->last_non_ack_seq != frame->seq ||
        ctx->last_non_ack_type != frame->type ||
        ctx->last_non_ack_len != frame->len) {
        return 0u;
    }

    return (uint8_t)(ctx->last_non_ack_crc == frame_crc_fingerprint(frame));
}

static void remember_non_ack(ogoa_ctx_t *ctx, const ogoa_frame_t *frame)
{
    ctx->have_last_non_ack = 1u;
    ctx->last_non_ack_seq = frame->seq;
    ctx->last_non_ack_type = frame->type;
    ctx->last_non_ack_len = frame->len;
    ctx->last_non_ack_crc = frame_crc_fingerprint(frame);
}
