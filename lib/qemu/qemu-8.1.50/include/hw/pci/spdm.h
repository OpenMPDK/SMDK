#ifndef SPDM_REQUESTER_H
#define SPDM_REQUESTER_H
#include "qemu/osdep.h"
#include "hw/pci/pcie_doe.h"

int spdm_sock_init(uint16_t port, Error **errp);
bool pcie_doe_spdm_rsp(DOECap *doe_cap);
void spdm_sock_fini(int socket);

#endif
