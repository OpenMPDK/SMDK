#ifndef QEMU_MCTP_H
#define QEMU_MCTP_H

#include "hw/registerfields.h"

/* DSP0236 1.3.0, Section 8.3.1 */
#define MCTP_BASELINE_MTU 64

/* DSP0236 1.3.0, Table 1, Message body */
FIELD(MCTP_MESSAGE_H, TYPE, 0, 7)
FIELD(MCTP_MESSAGE_H, IC,   7, 1)

/* DSP0236 1.3.0, Table 1, MCTP transport header */
FIELD(MCTP_H_FLAGS, TAG,    0, 3);
FIELD(MCTP_H_FLAGS, TO,     3, 1);
FIELD(MCTP_H_FLAGS, PKTSEQ, 4, 2);
FIELD(MCTP_H_FLAGS, EOM,    6, 1);
FIELD(MCTP_H_FLAGS, SOM,    7, 1);

/* DSP0236 1.3.0, Figure 4 */
typedef struct MCTPPacketHeader {
    uint8_t version;
    struct {
        uint8_t dest;
        uint8_t source;
    } eid;
    uint8_t flags;
} MCTPPacketHeader;

typedef struct MCTPPacket {
    MCTPPacketHeader hdr;
    uint8_t          payload[];
} MCTPPacket;

#endif /* QEMU_MCTP_H */
