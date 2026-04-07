/*
    LAN-Share Native Protocol Definitions
    Pure C protocol structs and constants for cross-platform compatibility
    
    Copyright (C) 2026 LAN-Share Project
*/

#ifndef LANSHARE_PROTOCOL_H
#define LANSHARE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif



/* Protocol version */
#define LANSHARE_PROTOCOL_VERSION 1




/* Device info structure */
typedef struct {
    char id[64];           /* Device unique ID */
    char name[128];        /* Device name */
    char os_name[64];      /* Operating system name */
    char ip_address[64];   /* IP address string */
} lanshare_device_t;

/* Transfer states */
typedef enum {
    TRANSFER_STATE_IDLE = 0,
    TRANSFER_STATE_WAITING,           /* Waiting for connection */
    TRANSFER_STATE_PENDING_CONFIRM,   /* Waiting for receiver confirmation */
    TRANSFER_STATE_CONFIRMED,         /* Receiver confirmed, ready to transfer */
    TRANSFER_STATE_REJECTED,          /* Receiver rejected */
    TRANSFER_STATE_TIMEOUT,           /* Confirmation timeout */
    TRANSFER_STATE_DISCONNECTED,
    TRANSFER_STATE_PAUSED,
    TRANSFER_STATE_CANCELLED,
    TRANSFER_STATE_TRANSFERRING,
    TRANSFER_STATE_FINISHED,
    TRANSFER_STATE_ERROR
} lanshare_transfer_state_t;

/* Transfer types */
typedef enum {
    TRANSFER_TYPE_NONE = 0,
    TRANSFER_TYPE_DOWNLOAD,
    TRANSFER_TYPE_UPLOAD
} lanshare_transfer_type_t;

/* Transfer info structure */
typedef struct {
    char file_path[512];
    char file_name[256];
    char peer_id[64];
    char peer_name[128];
    lanshare_transfer_state_t state;
    lanshare_transfer_type_t type;
    int progress;               /* Percentage 0-100 */
    long long data_size;
    long long bytes_transferred;
    double transfer_speed;      /* Bytes per second */
    int is_sender;              /* 1 if this device is sender, 0 if receiver */
} lanshare_transfer_info_t;

/* Protocol constants */
#define LANSHARE_FRAME_HEADER_SIZE 24
#define LANSHARE_MAX_PACKET_SIZE (1024 * 1024)  /* 1MB */
#define LANSHARE_DEFAULT_PORT 17116
#define LANSHARE_MDNS_SERVICE_TYPE "_lanshare._tcp"

/* Confirmation timeout in milliseconds (1 minute) */
#define LANSHARE_CONFIRM_TIMEOUT_MS 60000

#ifdef __cplusplus
}
#endif

#endif /* LANSHARE_PROTOCOL_H */
