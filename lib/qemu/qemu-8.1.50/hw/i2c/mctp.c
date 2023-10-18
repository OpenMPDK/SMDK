/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * SPDX-FileCopyrightText: Copyright (c) 2023 Samsung Electronics Co., Ltd.
 * SPDX-FileContributor: Klaus Jensen <k.jensen@samsung.com>
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"

#include "hw/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus_master.h"
#include "hw/i2c/mctp.h"
#include "net/mctp.h"

#include "trace.h"

/* DSP0237 1.2.0, Figure 1 */
typedef struct MCTPI2CPacketHeader {
    uint8_t dest;
#define MCTP_I2C_COMMAND_CODE 0xf
    uint8_t command_code;
    uint8_t byte_count;
    uint8_t source;
} MCTPI2CPacketHeader;

typedef struct MCTPI2CPacket {
    MCTPI2CPacketHeader i2c;
    MCTPPacket          mctp;
} MCTPI2CPacket;

#define i2c_mctp_payload_offset offsetof(MCTPI2CPacket, mctp.payload)
#define i2c_mctp_payload(buf) (buf + i2c_mctp_payload_offset)

/* DSP0236 1.3.0, Figure 20 */
typedef struct MCTPControlMessage {
#define MCTP_MESSAGE_TYPE_CONTROL 0x0
    uint8_t type;
#define MCTP_CONTROL_FLAGS_RQ               (1 << 7)
#define MCTP_CONTROL_FLAGS_D                (1 << 6)
    uint8_t flags;
    uint8_t command_code;
    uint8_t data[];
} MCTPControlMessage;

enum MCTPControlCommandCodes {
    MCTP_CONTROL_SET_EID                    = 0x01,
    MCTP_CONTROL_GET_EID                    = 0x02,
    MCTP_CONTROL_GET_VERSION                = 0x04,
    MCTP_CONTROL_GET_MESSAGE_TYPE_SUPPORT   = 0x05,
};

#define MCTP_CONTROL_ERROR_UNSUPPORTED_CMD 0x5

#define i2c_mctp_control_data_offset \
    (i2c_mctp_payload_offset + offsetof(MCTPControlMessage, data))
#define i2c_mctp_control_data(buf) (buf + i2c_mctp_control_data_offset)

/**
 * The byte count field in the SMBUS Block Write containers the number of bytes
 * *following* the field itself.
 *
 * This is at least 5.
 *
 * 1 byte for the MCTP/I2C piggy-backed I2C source address in addition to the
 * size of the MCTP transport/packet header.
 */
#define MCTP_I2C_BYTE_COUNT_OFFSET (sizeof(MCTPPacketHeader) + 1)

void i2c_mctp_schedule_send(MCTPI2CEndpoint *mctp)
{
    I2CBus *i2c = I2C_BUS(qdev_get_parent_bus(DEVICE(mctp)));

    mctp->tx.state = I2C_MCTP_STATE_TX_START_SEND;

    i2c_bus_master(i2c, mctp->tx.bh);
}

static void i2c_mctp_tx(void *opaque)
{
    DeviceState *dev = DEVICE(opaque);
    I2CBus *i2c = I2C_BUS(qdev_get_parent_bus(dev));
    I2CSlave *slave = I2C_SLAVE(dev);
    MCTPI2CEndpoint *mctp = MCTP_I2C_ENDPOINT(dev);
    MCTPI2CEndpointClass *mc = MCTP_I2C_ENDPOINT_GET_CLASS(mctp);
    MCTPI2CPacket *pkt = (MCTPI2CPacket *)mctp->buffer;
    uint8_t flags = 0;

    switch (mctp->tx.state) {
    case I2C_MCTP_STATE_TX_SEND_BYTE:
        if (mctp->pos < mctp->len) {
            uint8_t byte = mctp->buffer[mctp->pos];

            trace_i2c_mctp_tx_send_byte(mctp->pos, byte);

            /* send next byte */
            i2c_send_async(i2c, byte);

            mctp->pos++;

            break;
        }

        /* packet sent */
        i2c_end_transfer(i2c);

        /* end of any control data */
        mctp->len = 0;

        /* fall through */

    case I2C_MCTP_STATE_TX_START_SEND:
        if (mctp->tx.is_control) {
            /* packet payload is already in buffer; max 1 packet */
            flags = FIELD_DP8(flags, MCTP_H_FLAGS, SOM, 1);
            flags = FIELD_DP8(flags, MCTP_H_FLAGS, EOM, 1);
        } else {
            const uint8_t *payload;

            /* get message bytes from derived device */
            mctp->len = mc->get_buf(mctp, &payload, I2C_MCTP_MAXMTU, &flags);
            assert(mctp->len <= I2C_MCTP_MAXMTU);

            memcpy(pkt->mctp.payload, payload, mctp->len);
        }

        if (!mctp->len) {
            trace_i2c_mctp_tx_done();

            /* no more packets needed; release the bus */
            i2c_bus_release(i2c);

            mctp->state = I2C_MCTP_STATE_IDLE;
            mctp->tx.is_control = false;

            break;
        }

        mctp->state = I2C_MCTP_STATE_TX;

        pkt->i2c = (MCTPI2CPacketHeader) {
            .dest = mctp->tx.addr << 1,
            .command_code = MCTP_I2C_COMMAND_CODE,
            .byte_count = MCTP_I2C_BYTE_COUNT_OFFSET + mctp->len,

            /* DSP0237 1.2.0, Figure 1 */
            .source = slave->address << 1 | 0x1,
        };

        pkt->mctp.hdr = (MCTPPacketHeader) {
            .version = 0x1,
            .eid.dest = mctp->tx.eid,
            .eid.source = mctp->my_eid,
            .flags = flags,
        };

        pkt->mctp.hdr.flags = FIELD_DP8(pkt->mctp.hdr.flags, MCTP_H_FLAGS,
                                        PKTSEQ, mctp->tx.pktseq++);
        pkt->mctp.hdr.flags = FIELD_DP8(pkt->mctp.hdr.flags, MCTP_H_FLAGS, TAG,
                                        mctp->tx.tag);

        mctp->len += sizeof(MCTPI2CPacket);
        assert(mctp->len < I2C_MCTP_MAX_LENGTH);

        mctp->buffer[mctp->len] = i2c_smbus_pec(0, mctp->buffer, mctp->len);
        mctp->len++;

        trace_i2c_mctp_tx_start_send(mctp->len);

        i2c_start_send_async(i2c, pkt->i2c.dest >> 1);

        /* already "sent" the destination slave address */
        mctp->pos = 1;

        mctp->tx.state = I2C_MCTP_STATE_TX_SEND_BYTE;

        break;
    }
}

static void i2c_mctp_set_control_data(MCTPI2CEndpoint *mctp, const void * buf,
                                      size_t len)
{
    assert(i2c_mctp_control_data_offset < I2C_MCTP_MAX_LENGTH - len);
    memcpy(i2c_mctp_control_data(mctp->buffer), buf, len);

    assert(mctp->len < I2C_MCTP_MAX_LENGTH - len);
    mctp->len += len;
}

static void i2c_mctp_handle_control_set_eid(MCTPI2CEndpoint *mctp, uint8_t eid)
{
    mctp->my_eid = eid;

    uint8_t buf[] = {
        0x0, 0x0, eid, 0x0,
    };

    i2c_mctp_set_control_data(mctp, buf, sizeof(buf));
}

static void i2c_mctp_handle_control_get_eid(MCTPI2CEndpoint *mctp)
{
    uint8_t buf[] = {
        0x0, mctp->my_eid, 0x0, 0x0,
    };

    i2c_mctp_set_control_data(mctp, buf, sizeof(buf));
}

static void i2c_mctp_handle_control_get_version(MCTPI2CEndpoint *mctp)
{
    uint8_t buf[] = {
        0x0, 0x1, 0x0, 0x1, 0x3, 0x1,
    };

    i2c_mctp_set_control_data(mctp, buf, sizeof(buf));
}

static void i2c_mctp_handle_get_message_type_support(MCTPI2CEndpoint *mctp)
{
    MCTPI2CEndpointClass *mc = MCTP_I2C_ENDPOINT_GET_CLASS(mctp);
    const uint8_t *types;
    size_t len;

    len = mc->get_types(mctp, &types);
    assert(mctp->len <= MCTP_BASELINE_MTU - len);

    i2c_mctp_set_control_data(mctp, types, len);
}

static void i2c_mctp_handle_control(MCTPI2CEndpoint *mctp)
{
    MCTPControlMessage *msg = (MCTPControlMessage *)i2c_mctp_payload(mctp->buffer);

    /* clear Rq/D */
    msg->flags &= ~(MCTP_CONTROL_FLAGS_RQ | MCTP_CONTROL_FLAGS_D);

    mctp->len = sizeof(MCTPControlMessage);

    trace_i2c_mctp_handle_control(msg->command_code);

    switch (msg->command_code) {
    case MCTP_CONTROL_SET_EID:
        i2c_mctp_handle_control_set_eid(mctp, msg->data[1]);
        break;

    case MCTP_CONTROL_GET_EID:
        i2c_mctp_handle_control_get_eid(mctp);
        break;

    case MCTP_CONTROL_GET_VERSION:
        i2c_mctp_handle_control_get_version(mctp);
        break;

    case MCTP_CONTROL_GET_MESSAGE_TYPE_SUPPORT:
        i2c_mctp_handle_get_message_type_support(mctp);
        break;

    default:
        trace_i2c_mctp_unhandled_control(msg->command_code);

        msg->data[0] = MCTP_CONTROL_ERROR_UNSUPPORTED_CMD;
        mctp->len++;

        break;
    }

    assert(mctp->len <= MCTP_BASELINE_MTU);

    i2c_mctp_schedule_send(mctp);
}

static int i2c_mctp_event_cb(I2CSlave *i2c, enum i2c_event event)
{
    MCTPI2CEndpoint *mctp = MCTP_I2C_ENDPOINT(i2c);
    MCTPI2CEndpointClass *mc = MCTP_I2C_ENDPOINT_GET_CLASS(mctp);
    MCTPI2CPacket *pkt = (MCTPI2CPacket *)mctp->buffer;
    size_t payload_len;
    uint8_t pec, pktseq, msgtype;
    int ret;

    switch (event) {
    case I2C_START_SEND:
        if (mctp->state == I2C_MCTP_STATE_IDLE) {
            mctp->state = I2C_MCTP_STATE_RX_STARTED;
        } else if (mctp->state != I2C_MCTP_STATE_RX) {
            return -1;
        }

        /* the i2c core eats the slave address, so put it back in */
        pkt->i2c.dest = i2c->address << 1;
        mctp->len = 1;

        return 0;

    case I2C_FINISH:
        if (mctp->len < sizeof(MCTPI2CPacket) + 1) {
            trace_i2c_mctp_drop_short_packet(mctp->len);
            goto drop;
        }

        payload_len = mctp->len - (1 + offsetof(MCTPI2CPacket, mctp.payload));

        if (pkt->i2c.byte_count + 3 != mctp->len - 1) {
            trace_i2c_mctp_drop_invalid_length(pkt->i2c.byte_count + 3,
                                               mctp->len - 1);
            goto drop;
        }

        pec = i2c_smbus_pec(0, mctp->buffer, mctp->len - 1);
        if (mctp->buffer[mctp->len - 1] != pec) {
            trace_i2c_mctp_drop_invalid_pec(mctp->buffer[mctp->len - 1], pec);
            goto drop;
        }

        if (!(pkt->mctp.hdr.eid.dest == mctp->my_eid ||
              pkt->mctp.hdr.eid.dest == 0)) {
            trace_i2c_mctp_drop_invalid_eid(pkt->mctp.hdr.eid.dest,
                                            mctp->my_eid);
            goto drop;
        }

        pktseq = FIELD_EX8(pkt->mctp.hdr.flags, MCTP_H_FLAGS, PKTSEQ);

        if (FIELD_EX8(pkt->mctp.hdr.flags, MCTP_H_FLAGS, SOM)) {
            mctp->tx.is_control = false;

            if (mctp->state == I2C_MCTP_STATE_RX) {
                mc->reset(mctp);
            }

            mctp->state = I2C_MCTP_STATE_RX;

            mctp->tx.addr = pkt->i2c.source >> 1;
            mctp->tx.eid = pkt->mctp.hdr.eid.source;
            mctp->tx.tag = FIELD_EX8(pkt->mctp.hdr.flags, MCTP_H_FLAGS, TAG);
            mctp->tx.pktseq = pktseq;

            msgtype = FIELD_EX8(pkt->mctp.payload[0], MCTP_MESSAGE_H, TYPE);

            if (msgtype == MCTP_MESSAGE_TYPE_CONTROL) {
                mctp->tx.is_control = true;

                i2c_mctp_handle_control(mctp);

                return 0;
            }
        } else if (mctp->state == I2C_MCTP_STATE_RX_STARTED) {
            trace_i2c_mctp_drop_expected_som();
            goto drop;
        } else if (pktseq != (++mctp->tx.pktseq & 0x3)) {
            trace_i2c_mctp_drop_invalid_pktseq(pktseq, mctp->tx.pktseq & 0x3);
            goto drop;
        }

        ret = mc->put_buf(mctp, i2c_mctp_payload(mctp->buffer), payload_len);
        if (ret < 0) {
            goto drop;
        }

        if (FIELD_EX8(pkt->mctp.hdr.flags, MCTP_H_FLAGS, EOM)) {
            mc->handle(mctp);
            mctp->state = I2C_MCTP_STATE_WAIT_TX;
        }

        return 0;

    default:
        return -1;
    }

drop:
    mc->reset(mctp);

    mctp->state = I2C_MCTP_STATE_IDLE;

    return 0;
}

static int i2c_mctp_send_cb(I2CSlave *i2c, uint8_t data)
{
    MCTPI2CEndpoint *mctp = MCTP_I2C_ENDPOINT(i2c);

    if (mctp->len < I2C_MCTP_MAX_LENGTH) {
        mctp->buffer[mctp->len++] = data;
        return 0;
    }

    return -1;
}

static void i2c_mctp_instance_init(Object *obj)
{
    MCTPI2CEndpoint *mctp = MCTP_I2C_ENDPOINT(obj);

    mctp->tx.bh = qemu_bh_new(i2c_mctp_tx, mctp);
}

static Property mctp_i2c_props[] = {
    DEFINE_PROP_UINT8("eid", MCTPI2CEndpoint, my_eid, 0x9),
    DEFINE_PROP_END_OF_LIST(),
};

static void i2c_mctp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(oc);

    k->event = i2c_mctp_event_cb;
    k->send = i2c_mctp_send_cb;

    device_class_set_props(dc, mctp_i2c_props);
}

static const TypeInfo i2c_mctp_info = {
    .name = TYPE_MCTP_I2C_ENDPOINT,
    .parent = TYPE_I2C_SLAVE,
    .abstract = true,
    .instance_init = i2c_mctp_instance_init,
    .instance_size = sizeof(MCTPI2CEndpoint),
    .class_init = i2c_mctp_class_init,
    .class_size = sizeof(MCTPI2CEndpointClass),
};

static void register_types(void)
{
    type_register_static(&i2c_mctp_info);
}

type_init(register_types)
