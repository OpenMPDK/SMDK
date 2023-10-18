#ifndef QEMU_I2C_MCTP_H
#define QEMU_I2C_MCTP_H

#include "qom/object.h"
#include "hw/qdev-core.h"

#define TYPE_MCTP_I2C_ENDPOINT "mctp-i2c-endpoint"
OBJECT_DECLARE_TYPE(MCTPI2CEndpoint, MCTPI2CEndpointClass, MCTP_I2C_ENDPOINT)

struct MCTPI2CEndpointClass {
    I2CSlaveClass parent_class;

    /**
     * put_buf() - receive incoming message fragment
     *
     * Return 0 for success or negative for error.
     */
    int (*put_buf)(MCTPI2CEndpoint *mctp, uint8_t *buf, size_t len);

    /**
     * get_buf() - provide pointer to message fragment
     *
     * Called by the mctp subsystem to request a pointer to the next message
     * fragment. Subsequent calls MUST return next fragment (if any).
     *
     * Must return the number of bytes in message fragment.
     */
    size_t (*get_buf)(MCTPI2CEndpoint *mctp, const uint8_t **buf,
                      size_t maxlen, uint8_t *mctp_flags);

    /**
     * handle() - handle an MCTP message
     *
     * Called by the mctp subsystem when a full message has been delivered and
     * may be parsed and processed.
     */
    void (*handle)(MCTPI2CEndpoint *mctp);

    /**
     * reset() - reset internal state
     *
     * Called by the mctp subsystem in the event of some transport error.
     * Implementation must reset its internal state and drop any fragments
     * previously receieved.
     */
    void (*reset)(MCTPI2CEndpoint *mctp);

    /**
     * get_types() - provide supported mctp message types
     *
     * Must provide a buffer with a full MCTP supported message types payload
     * (i.e. `0x0(SUCCESS),0x1(COUNT),0x4(NMI)`).
     *
     * Returns the size of the response.
     */
    size_t (*get_types)(MCTPI2CEndpoint *mctp, const uint8_t **data);
};

/*
 * Maximum value of the SMBus Block Write "Byte Count" field (8 bits).
 *
 * This is the count of bytes that follow the Byte Count field and up to, but
 * not including, the PEC byte.
 */
#define I2C_MCTP_MAXBLOCK 255

/*
 * Maximum Transmission Unit under I2C.
 *
 * This is for the MCTP Packet Payload (255, subtracting the 4 byte MCTP Packet
 * Header and the 1 byte MCTP/I2C piggy-backed source address).
 */
#define I2C_MCTP_MAXMTU (I2C_MCTP_MAXBLOCK - (sizeof(MCTPPacketHeader) + 1))

/*
 * Maximum length of an MCTP/I2C packet.
 *
 * This is the sum of the three I2C header bytes (Destination target address,
 * Command Code and Byte Count), the maximum number of bytes in a message (255)
 * and the 1 byte Packet Error Code.
 */
#define I2C_MCTP_MAX_LENGTH (3 + I2C_MCTP_MAXBLOCK + 1)

typedef enum {
    I2C_MCTP_STATE_IDLE,
    I2C_MCTP_STATE_RX_STARTED,
    I2C_MCTP_STATE_RX,
    I2C_MCTP_STATE_WAIT_TX,
    I2C_MCTP_STATE_TX,
} MCTPState;

typedef enum {
    I2C_MCTP_STATE_TX_START_SEND,
    I2C_MCTP_STATE_TX_SEND_BYTE,
} MCTPTxState;

typedef struct MCTPI2CEndpoint {
    I2CSlave parent_obj;
    I2CBus *i2c;

    MCTPState state;

    /* mctp endpoint identifier */
    uint8_t my_eid;

    uint8_t buffer[I2C_MCTP_MAX_LENGTH];
    uint64_t pos;
    size_t len;

    struct {
        MCTPTxState state;
        bool is_control;

        uint8_t eid;
        uint8_t addr;
        uint8_t pktseq;
        uint8_t tag;

        QEMUBH *bh;
    } tx;
} MCTPI2CEndpoint;

void i2c_mctp_schedule_send(MCTPI2CEndpoint *mctp);

#endif /* QEMU_I2C_MCTP_H */
