/*
 * CXL Compliance Structure
 *
 * Copyright (C) 2021 Avery Design Systems, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef CXL_COMPL_H
#define CXL_COMPL_H

#include "hw/pci/pcie_doe.h"

/*
 * Reference:
 *   Compute Express Link (CXL) Specification, Rev. 2.0, Oct. 2020
 */
/* Compliance Mode Data Object Header - 14.16.4 Table 275 */
#define CXL_DOE_COMPLIANCE        0
#define CXL_DOE_PROTOCOL_COMPLIANCE ((CXL_DOE_COMPLIANCE << 16) | CXL_VENDOR_ID)

/* Compliance Mode Return Values - 14.16.4 Table 276 */
typedef enum {
    CXL_COMP_MODE_RET_SUCC,
    CXL_COMP_MODE_RET_NOT_AUTH,
    CXL_COMP_MODE_RET_UNKNOWN_FAIL,
    CXL_COMP_MODE_RET_UNSUP_INJ_FUNC,
    CXL_COMP_MODE_RET_INTERNAL_ERR,
    CXL_COMP_MODE_RET_BUSY,
    CXL_COMP_MODE_RET_NOT_INIT,
} CXLCompStatus;

/* Compliance Mode Types - 14.16.4 */
typedef enum {
    CXL_COMP_MODE_CAP,
    CXL_COMP_MODE_STATUS,
    CXL_COMP_MODE_HALT,
    CXL_COMP_MODE_MULT_WR_STREAM,
    CXL_COMP_MODE_PRO_CON,
    CXL_COMP_MODE_BOGUS,
    CXL_COMP_MODE_INJ_POISON,
    CXL_COMP_MODE_INJ_CRC,
    CXL_COMP_MODE_INJ_FC,
    CXL_COMP_MODE_TOGGLE_CACHE,
    CXL_COMP_MODE_INJ_MAC,
    CXL_COMP_MODE_INS_UNEXP_MAC,
    CXL_COMP_MODE_INJ_VIRAL,
    CXL_COMP_MODE_INJ_ALMP,
    CXL_COMP_MODE_IGN_ALMP,
    CXL_COMP_MODE_INJ_BIT_ERR,
} CXLCompType;

typedef struct CXLCompReqHeader {
    DOEHeader doe_header;
    uint8_t req_code;
    uint8_t version;
    uint16_t reserved;
} QEMU_PACKED CXLCompReqHeader;

typedef struct CXLCompRspHeader {
    DOEHeader doe_header;
    uint8_t rsp_code;
    uint8_t version;
    uint8_t length;
} QEMU_PACKED CXLCompRspHeader;

/* Special Patterns of response, do not use directly */
typedef struct __CXLCompStatusOnlyRsp {
    CXLCompRspHeader header;
    uint8_t status;
} QEMU_PACKED __CXLCompStatusOnlyRsp;

typedef struct __CXLCompLenRsvdRsp {
    /* The length field in header is reserved. */
    CXLCompRspHeader header;
    uint8_t reserved[5];
} QEMU_PACKED __CXLCompLenRsvdRsp;

/* 14.16.4.1 Table 277 */
typedef struct CXLCompCapReq {
    CXLCompReqHeader header;
} QEMU_PACKED CXLCompCapReq;

/* 14.16.4.1 Table 278 */
typedef struct CXLCompCapRsp {
    CXLCompRspHeader header;
    uint8_t status;
    uint64_t available_cap_bitmask;
    uint64_t enabled_cap_bitmask;
} QEMU_PACKED CXLCompCapRsp;

/* 14.16.4.2 Table 279 */
typedef struct CXLCompStatusReq {
    CXLCompReqHeader header;
} QEMU_PACKED CXLCompStatusReq;

/* 14.16.4.2 Table 280 */
typedef struct CXLCompStatusRsp {
    CXLCompRspHeader header;
    uint32_t cap_bitfield;
    uint16_t cache_size;
    uint8_t cache_size_units;
} QEMU_PACKED CXLCompStatusRsp;

/* 14.16.4.3 Table 281 */
typedef struct CXLCompHaltReq {
    CXLCompReqHeader header;
} QEMU_PACKED CXLCompHaltReq;

/* 14.16.4.3 Table 282 */
#define CXLCompHaltRsp __CXLCompStatusOnlyRsp

/* 14.16.4.4 Table 283 */
typedef struct CXLCompMultiWriteStreamingReq {
    CXLCompReqHeader header;
    uint8_t protocol;
    uint8_t virtual_addr;
    uint8_t self_checking;
    uint8_t verify_read_semantics;
    uint8_t num_inc;
    uint8_t num_sets;
    uint8_t num_loops;
    uint8_t reserved2;
    uint64_t start_addr;
    uint64_t write_addr;
    uint64_t writeback_addr;
    uint64_t byte_mask;
    uint32_t addr_incr;
    uint32_t set_offset;
    uint32_t pattern_p;
    uint32_t inc_pattern_b;
} QEMU_PACKED CXLCompMultiWriteStreamingReq;

/* 14.16.4.4 Table 284 */
#define CXLCompMultiWriteStreamingRsp __CXLCompStatusOnlyRsp

/* 14.16.4.5 Table 285 */
typedef struct CXLCompProducerConsumerReq {
    CXLCompReqHeader header;
    uint8_t protocol;
    uint8_t num_inc;
    uint8_t num_sets;
    uint8_t num_loops;
    uint8_t write_semantics;
    uint8_t reserved[3];
    uint64_t start_addr;
    uint64_t byte_mask;
    uint32_t addr_incr;
    uint32_t set_offset;
    uint32_t pattern;
} QEMU_PACKED CXLCompProducerConsumerReq;

/* 14.16.4.5 Table 286 */
#define CXLCompProducerConsumerRsp __CXLCompStatusOnlyRsp

/* 14.16.4.6 Table 287 */
typedef struct CXLCompBogusWritesReq {
    CXLCompReqHeader header;
    uint8_t count;
    uint8_t reserved;
    uint32_t pattern;
} QEMU_PACKED CXLCompBogusWritesReq;

/* 14.16.4.6 Table 288 */
#define CXLCompBogusWritesRsp __CXLCompStatusOnlyRsp

/* 14.16.4.7 Table 289 */
typedef struct CXLCompInjectPoisonReq {
    CXLCompReqHeader header;
    uint8_t protocol;
} QEMU_PACKED CXLCompInjectPoisonReq;

/* 14.16.4.7 Table 290 */
#define CXLCompInjectPoisonRsp __CXLCompStatusOnlyRsp

/* 14.16.4.8 Table 291 */
typedef struct CXLCompInjectCrcReq {
    CXLCompReqHeader header;
    uint8_t num_bits_flip;
    uint8_t num_flits_inj;
} QEMU_PACKED CXLCompInjectCrcReq;

/* 14.16.4.8 Table 292 */
#define CXLCompInjectCrcRsp __CXLCompStatusOnlyRsp

/* 14.16.4.9 Table 293 */
typedef struct CXLCompInjectFlowCtrlReq {
    CXLCompReqHeader header;
    uint8_t inj_flow_control;
} QEMU_PACKED CXLCompInjectFlowCtrlReq;

/* 14.16.4.9 Table 294 */
#define CXLCompInjectFlowCtrlRsp __CXLCompStatusOnlyRsp

/* 14.16.4.10 Table 295 */
typedef struct CXLCompToggleCacheFlushReq {
    CXLCompReqHeader header;
    uint8_t cache_flush_control;
} QEMU_PACKED CXLCompToggleCacheFlushReq;

/* 14.16.4.10 Table 296 */
#define CXLCompToggleCacheFlushRsp __CXLCompStatusOnlyRsp

/* 14.16.4.11 Table 297 */
typedef struct CXLCompInjectMacDelayReq {
    CXLCompReqHeader header;
    uint8_t enable;
    uint8_t mode;
    uint8_t delay;
} QEMU_PACKED CXLCompInjectMacDelayReq;

/* 14.16.4.11 Table 298 */
#define CXLCompInjectMacDelayRsp __CXLCompStatusOnlyRsp

/* 14.16.4.12 Table 299 */
typedef struct CXLCompInsertUnexpMacReq {
    CXLCompReqHeader header;
    uint8_t opcode;
    uint8_t mode;
} QEMU_PACKED CXLCompInsertUnexpMacReq;

/* 14.16.4.12 Table 300 */
#define CXLCompInsertUnexpMacRsp __CXLCompStatusOnlyRsp

/* 14.16.4.13 Table 301 */
typedef struct CXLCompInjectViralReq {
    CXLCompReqHeader header;
    uint8_t protocol;
} QEMU_PACKED CXLCompInjectViralReq;

/* 14.16.4.13 Table 302 */
#define CXLCompInjectViralRsp __CXLCompStatusOnlyRsp

/* 14.16.4.14 Table 303 */
typedef struct CXLCompInjectAlmpReq {
    CXLCompReqHeader header;
    uint8_t opcode;
    uint8_t reserved2[3];
} QEMU_PACKED CXLCompInjectAlmpReq;

/* 14.16.4.14 Table 304 */
#define CXLCompInjectAlmpRsp __CXLCompLenRsvdRsp

/* 14.16.4.15 Table 305 */
typedef struct CXLCompIgnoreAlmpReq {
    CXLCompReqHeader header;
    uint8_t opcode;
    uint8_t reserved2[3];
} QEMU_PACKED CXLCompIgnoreAlmpReq;

/* 14.16.4.15 Table 306 */
#define CXLCompIgnoreAlmpRsp __CXLCompLenRsvdRsp

/* 14.16.4.16 Table 307 */
typedef struct CXLCompInjectBitErrInFlitReq {
    CXLCompReqHeader header;
    uint8_t opcode;
} QEMU_PACKED CXLCompInjectBitErrInFlitReq;

/* 14.16.4.16 Table 308 */
#define CXLCompInjectBitErrInFlitRsp __CXLCompLenRsvdRsp

typedef union CXLCompRsp {
    CXLCompRspHeader header;

    CXLCompCapRsp cap_rsp;
    CXLCompStatusRsp status_rsp;
    CXLCompHaltRsp halt_rsp;
    CXLCompMultiWriteStreamingRsp multi_write_streaming_rsp;
    CXLCompProducerConsumerRsp producer_consumer_rsp;
    CXLCompBogusWritesRsp bogus_writes_rsp;
    CXLCompInjectPoisonRsp inject_poison_rsp;
    CXLCompInjectCrcRsp inject_crc_rsp;
    CXLCompInjectFlowCtrlRsp inject_flow_ctrl_rsp;
    CXLCompToggleCacheFlushRsp toggle_cache_flush_rsp;
    CXLCompInjectMacDelayRsp inject_mac_delay_rsp;
    CXLCompInsertUnexpMacRsp insert_unexp_mac_rsp;
    CXLCompInjectViralRsp inject_viral_rsp;
    CXLCompInjectAlmpRsp inject_almp_rsp;
    CXLCompIgnoreAlmpRsp ignore_almp_rsp;
    CXLCompInjectBitErrInFlitRsp inject_bit_err_in_flit_rsp;
} CXLCompRsp;

typedef struct CXLCompObject {
    CXLCompRsp response;
} QEMU_PACKED CXLCompObject;
#endif /* CXL_COMPL_H */
