/*
 * Copyright (c) 2021, DMTF. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Code from spdm_emu. http://github.com/dmtf/spdm-emu
 *
 * Minimal changes to aid keeping this in sync.
 * - SOCKET replaced with int.
 * - uintn replaced with size_t where appropriate.
 * - Most logging removed. spdm_responder provides easy access to the
 *   logs so we don't need them on QEMU side of the link.
 * - Basic style and type tidying up.
 */

#include "qemu/osdep.h"
#include "hw/pci/spdm.h"
#include "qapi/error.h"

#define SOCKET_TRANSPORT_TYPE_MCTP     0x01
#define SOCKET_TRANSPORT_TYPE_PCI_DOE  0x02

#define SOCKET_SPDM_COMMAND_NORMAL                0x0001
#define SOCKET_SPDM_COMMAND_OOB_ENCAP_KEY_UPDATE  0x8001
#define SOCKET_SPDM_COMMAND_CONTINUE              0xFFFD
#define SOCKET_SPDM_COMMAND_SHUTDOWN              0xFFFE
#define SOCKET_SPDM_COMMAND_UNKOWN                0xFFFF
#define SOCKET_SPDM_COMMAND_TEST                  0xDEAD

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

#define MAX_SPDM_MESSAGE_BUFFER_SIZE      0x1200

#define DWORD_BYTE 4

/* localhost */
struct in_addr mIpAddress = {0x0100007F};

/* spdm_emu_common/command.c */
static bool read_bytes(const int socket, uint8_t *buffer,
                       size_t number_of_bytes)
{
    ssize_t number_received = 0;
    ssize_t result;

    while (number_received < number_of_bytes) {
        result = recv(socket, buffer + number_received,
                      number_of_bytes - number_received, 0);
        if (result == -1) {
            return false;
        }
        if (result == 0) {
            return false;
        }
        number_received += result;
    }
    return true;
}

static bool read_data32(const int socket, uint32_t *data)
{
    bool result;

    result = read_bytes(socket, (uint8_t *)data, sizeof(uint32_t));
    if (!result) {
        return result;
    }
    *data = ntohl(*data);
    return true;
}

static bool read_multiple_bytes(const int socket, uint8_t *buffer,
                                uint32_t *bytes_received,
                                uint32_t max_buffer_length)
{
    uint32_t length;
    bool result;

    result = read_data32(socket, &length);
    if (!result) {
        return result;
    }

    *bytes_received = length;
    if (*bytes_received > max_buffer_length) {
        return false;
    }
    if (length == 0) {
        return true;
    }

    return read_bytes(socket, buffer, length);
}

static bool receive_platform_data(const int socket, uint32_t *command,
                                  uint8_t *receive_buffer,
                                  uint32_t *bytes_to_receive)
{
    bool result;
    uint32_t response;
    uint32_t transport_type;
    uint32_t bytes_received;

    result = read_data32(socket, &response);
    if (!result) {
        return result;
    }
    *command = response;

    result = read_data32(socket, &transport_type);
    if (!result) {
        return result;
    }
    if (transport_type != SOCKET_TRANSPORT_TYPE_PCI_DOE) {
        return false;
    }

    bytes_received = 0;
    result = read_multiple_bytes(socket, receive_buffer, &bytes_received,
                                 *bytes_to_receive);
    if (!result) {
        return result;
    }
    *bytes_to_receive = bytes_received;

    return result;
}

static bool write_bytes(const int socket, const uint8_t *buffer,
                        uint32_t number_of_bytes)
{
    ssize_t number_sent = 0;
    ssize_t result;

    while (number_sent < number_of_bytes) {
        result = send(socket, buffer + number_sent,
                      number_of_bytes - number_sent, 0);
        if (result == -1) {
            return false;
        }
        number_sent += result;
    }
    return true;
}

static bool write_data32(const int socket, uint32_t data)
{
    data = htonl(data);
    return write_bytes(socket, (uint8_t *)&data, sizeof(uint32_t));
}

static bool write_multiple_bytes(const int socket, const uint8_t *buffer,
                                 uint32_t bytes_to_send)
{
    bool result;

    result = write_data32(socket, bytes_to_send);
    if (!result) {
        return result;
    }

    return write_bytes(socket, buffer, bytes_to_send);
}

static bool send_platform_data(const int socket, uint32_t command,
                               const uint8_t *send_buffer, size_t bytes_to_send)
{
    bool result;

    result = write_data32(socket, command);
    if (!result) {
        return result;
    }

    result = write_data32(socket, SOCKET_TRANSPORT_TYPE_PCI_DOE);
    if (!result) {
        return result;
    }

    return write_multiple_bytes(socket, send_buffer, bytes_to_send);
}

int spdm_sock_init(uint16_t port, Error **errp)
{
    int result, client_socket;
    struct sockaddr_in server_addr;

    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        error_setg(errp, "Openspdm: %s", strerror(errno));
        return -1;
    }

    server_addr.sin_family = AF_INET;
    memcpy(&server_addr.sin_addr.s_addr, &mIpAddress, sizeof(struct in_addr));
    server_addr.sin_port = htons(port);
    memset(server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

    result = connect(client_socket, (struct sockaddr *)&server_addr,
                     sizeof(server_addr));
    if (result == SOCKET_ERROR) {
        error_setg(errp, "Openspdm: %s", strerror(errno));
        close(client_socket);
        return -1;
    }
    return client_socket;
}

bool pcie_doe_spdm_rsp(DOECap *doe_cap)
{
    void *req = pcie_doe_get_write_mbox_ptr(doe_cap);
    uint32_t len = pcie_doe_get_obj_len(req);
    uint32_t rsp_len = MAX_SPDM_MESSAGE_BUFFER_SIZE, Command;
    int socket = doe_cap->socket;
    bool result;

    result = send_platform_data(socket, SOCKET_SPDM_COMMAND_NORMAL,
                                req, len * DWORD_BYTE);
    if (!result) {
        return result;
    }

    result = receive_platform_data(socket, &Command,
                                   (uint8_t *)doe_cap->read_mbox, &rsp_len);
    if (!result) {
        return result;
    }

    assert(Command != 0);
    doe_cap->read_mbox_len += DIV_ROUND_UP(rsp_len, DWORD_BYTE);

    return true;
}

void spdm_sock_fini(int socket)
{
    send_platform_data(socket, SOCKET_SPDM_COMMAND_SHUTDOWN, NULL, 0);
}
