/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Copyright (c) 2023 MemVerge Inc.
 * Copyright (c) 2023 SK hynix Inc.
 *
 */

#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

struct niagara_state {
    uint8_t nr_heads;
    uint8_t nr_lds;
    uint8_t ldmap[65536];
    uint32_t total_sections;
    uint32_t free_sections;
    uint32_t section_size;
    uint32_t sections[];
};

int main(int argc, char *argv[])
{
    int shmid = 0;
    uint32_t sections = 0;
    uint32_t section_size = 0;
    uint32_t heads = 0;
    struct niagara_state *niagara_state = NULL;
    size_t state_size;
    uint8_t i;

    if (argc != 5) {
        printf("usage: init_niagara <heads> <sections> <section_size> <shmid>\n"
                "\theads         : number of heads on the device\n"
                "\tsections      : number of sections\n"
                "\tsection_size  : size of a section in 128mb increments\n"
                "\tshmid         : /tmp/mytoken.tmp\n\n"
                "It is recommended your shared memory region is at least 128kb\n");
        return -1;
    }

    /* must have at least 1 head */
    heads = (uint32_t)atoi(argv[1]);
    if (heads == 0 || heads > 32) {
        printf("bad heads argument (1-32)\n");
        return -1;
    }

    /* Get number of sections */
    sections = (uint32_t)atoi(argv[2]);
    if (sections == 0) {
        printf("bad sections argument\n");
        return -1;
    }

    section_size = (uint32_t)atoi(argv[3]);
    if (sections == 0) {
        printf("bad section size argument\n");
        return -1;
    }

    shmid = (uint32_t)atoi(argv[4]);
    if (shmid == 0) {
        printf("bad shmid argument\n");
        return -1;
    }

    niagara_state = shmat(shmid, NULL, 0);
    if (niagara_state == (void *)-1) {
        printf("Unable to attach to shared memory\n");
        return -1;
    }

    /* Initialize the niagara_state */
    state_size = sizeof(struct niagara_state) + (sizeof(uint32_t) * sections);
    memset(niagara_state, 0, state_size);
    niagara_state->nr_heads = heads;
    niagara_state->nr_lds = heads;
    niagara_state->total_sections = sections;
    niagara_state->free_sections = sections;
    niagara_state->section_size = section_size;

    memset(&niagara_state->ldmap, '\xff', sizeof(niagara_state->ldmap));
    for (i = 0; i < heads; i++) {
        niagara_state->ldmap[i] = i;
    }

    printf("niagara initialized\n");
    shmdt(niagara_state);
    return 0;
}
