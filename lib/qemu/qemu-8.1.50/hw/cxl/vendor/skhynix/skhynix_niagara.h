#ifndef CXL_SKH_NIAGARA_H
#define CXL_SKH_NIAGARA_H
#include <stdint.h>
#include "hw/cxl/cxl.h"
#include "hw/cxl/cxl_mailbox.h"
#include "hw/cxl/cxl_device.h"
#include "qemu/units.h"

#define NIAGARA_MIN_MEMBLK (128 * MiB)

/*
 * The shared state cannot have 2 variable sized regions
 * so we have to max out the ldmap.
 */
typedef struct NiagaraSharedState {
    uint8_t nr_heads;
    uint8_t nr_lds;
    uint8_t ldmap[65536];
    uint32_t total_sections;
    uint32_t free_sections;
    uint32_t section_size;
    uint32_t sections[];
} NiagaraSharedState;

struct CXLNiagaraState {
    CXLType3Dev ct3d;
    uint32_t mhd_head;
    uint32_t mhd_shmid;
    NiagaraSharedState *mhd_state;
};

struct CXLNiagaraClass {
    CXLType3Class parent_class;
};

enum {
    NIAGARA_CMD = 0xC0
        #define GET_SECTION_STATUS 0x0
        #define SET_SECTION_ALLOC 0x1
        #define SET_SECTION_RELEASE 0x2
        #define SET_SECTION_SIZE 0x3
        /* Future: MOVE_DATA 0x4 */
        #define GET_SECTION_MAP 0x5
        /* Future: CLEAR_SECTION 0x99 */
};

typedef struct NiagaraExtent {
    uint32_t start_section_id;
    uint32_t section_count;
    uint8_t reserved[8];
} QEMU_PACKED NiagaraExtent;

/*
 * MHD Get Info Command
 * Returns information the LD's associated with this head
 */
typedef struct NiagaraMHDGetInfoInput {
    uint8_t start_ld;
    uint8_t ldmap_len;
} QEMU_PACKED NiagaraMHDGetInfoInput;

typedef struct NiagaraMHDGetInfoOutput {
    uint8_t nr_lds;
    uint8_t nr_heads;
    uint16_t resv1;
    uint8_t start_ld;
    uint8_t ldmap_len;
    uint16_t resv2;
    uint8_t ldmap[];
} QEMU_PACKED NiagaraMHDGetInfoOutput;

/*
 * Niagara Section Status Command
 *
 * Returns the total sections and number of free sections
 */
typedef struct NiagaraGetSectionStatusOutput {
    uint32_t total_section_count;
    uint32_t free_section_count;
} QEMU_PACKED NiagaraGetSectionStatusOutput;

/*
 * Niagara Set Section Alloc Command
 *
 * Policies:
 *    All or nothing - if fail to allocate any section, nothing is allocated
 *    Best effort - Allocate as many as possible
 *    Manual - allocate the provided set of extents
 *
 * Policies can be combined.
 *
 * Returns: The allocated sections in extents
 */
#define NIAGARA_SECTION_ALLOC_POLICY_ALL_OR_NOTHING 0
#define NIAGARA_SECTION_ALLOC_POLICY_BEST_EFFORT 1
#define NIAGARA_SECTION_ALLOC_POLICY_MANUAL 2

typedef struct NiagaraAllocInput {
    uint8_t policy;
    uint8_t reserved1[3];
    uint32_t section_count;
    uint8_t reserved2[4];
    uint32_t extent_count;
    NiagaraExtent extents[];
} QEMU_PACKED NiagaraAllocInput;

typedef struct NiagaraAllocOutput {
    uint32_t section_count;
    uint32_t extent_count;
    NiagaraExtent extents[];
} QEMU_PACKED NiagaraAllocOutput;

/*
 * Niagara Set Section Release Command
 *
 * Releases the provided extents
 */
typedef struct NiagaraReleaseInput {
    uint32_t extent_count;
    uint8_t policy;
    uint8_t reserved[3];
    NiagaraExtent extents[];
} QEMU_PACKED NiagaraReleaseInput;

/*
 * Niagara Set Section Size
 *
 * Changes the section size to 128 * (1 << section_unit)
 */
typedef struct NiagaraSetSectionSizeInput {
    uint8_t section_unit;
    uint8_t reserved[7];
} QEMU_PACKED NiagaraSetSectionSizeInput;

typedef struct {
    uint8_t section_unit;
    uint8_t reserved[7];
} QEMU_PACKED NiagaraSetSectionSizeOutput;

/*
 * Niagara Get Section Map Command
 * query type:
 *     Free - Map of free sections
 *     Allocated - What sections are allocated for this head
 * Returns a map of the requested type of sections
 */
#define NIAGARA_GSM_QUERY_FREE 0
#define NIAGARA_GSM_QUERY_ALLOCATED 1

typedef struct NiagaraGetSectionMapInput {
    uint8_t query_type;
    uint8_t reserved[7];
} QEMU_PACKED NiagaraGetSectionMapInput;

typedef struct NiagaraGetSectionMapOutput {
    uint32_t ttl_section_count;
    uint32_t qry_section_count;
    uint8_t bitset[];
} QEMU_PACKED NiagaraGetSectionMapOutput;

#endif
