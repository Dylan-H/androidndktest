/*
    LAN-Share NNG-based Transfer Implementation
    Uses NNG PAIR protocol with application-layer protocol for reliable file transfer
    
    Protocol Design:
    - Message header stores control info (type, size, offset)
    - Message body stores actual data (filename, file content)
    
    Copyright (C) 2026 LAN-Share Project
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include "lanshare_transfer.h"
#include "lanshare_protocol.h"
#include "../include/lanshare_log.h"

#include <nng/nng.h>
#include <unistd.h>

/* ========== Protocol Constants ========== */

/* Message type definitions - stored in message header */
#define MSG_TYPE_FILE_INFO  0x01  /* File metadata (name, size, etc.) */
#define MSG_TYPE_FILE_DATA  0x02  /* File data chunk */
#define MSG_TYPE_FILE_END   0x03  /* File transfer complete */
#define MSG_TYPE_ACK        0x04  /* Acknowledgment */
#define MSG_TYPE_ERROR      0x05  /* Error message */
#define MSG_TYPE_PAUSE      0x06  /* Pause transfer */
#define MSG_TYPE_RESUME     0x07  /* Resume transfer */
#define MSG_TYPE_CANCEL     0x08  /* Cancel transfer */
#define MSG_TYPE_CONFIRM    0x09  /* Receiver confirms file receive */
#define MSG_TYPE_REJECT     0x0A  /* Receiver rejects file receive */

/* Confirmation timeout in milliseconds */
#define CONFIRM_TIMEOUT_MS 60000

/* Protocol version */
#define PROTOCOL_VERSION    0x01

/* Default chunk size: 64KB */
#define DEFAULT_CHUNK_SIZE (64 * 1024)

/* Speed calculation interval in milliseconds */
#define SPEED_CALC_INTERVAL_MS 1000

/* Maximum filename length */
#define MAX_FILENAME_LEN 256

/* Maximum folder name length */
#define MAX_FOLDER_LEN 128

/* NNG 2.0 initialization flag */
static int nng_initialized = 0;

/* Initialize NNG library (call once) */
static nng_err nng_init_once(void)
{
    if (!nng_initialized) {
        nng_err rv = nng_init(NULL);
        if (rv != NNG_OK) {
            return rv;
        }
        nng_initialized = 1;
    }
    return NNG_OK;
}

/* ========== Protocol Helper Functions ========== */

/* Pack uint32_t to network byte order (big-endian) */
static void pack_u32(uint8_t* buf, uint32_t val)
{
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

/* Pack uint64_t to network byte order (big-endian) */
static void pack_u64(uint8_t* buf, uint64_t val)
{
    buf[0] = (val >> 56) & 0xFF;
    buf[1] = (val >> 48) & 0xFF;
    buf[2] = (val >> 40) & 0xFF;
    buf[3] = (val >> 32) & 0xFF;
    buf[4] = (val >> 24) & 0xFF;
    buf[5] = (val >> 16) & 0xFF;
    buf[6] = (val >> 8) & 0xFF;
    buf[7] = val & 0xFF;
}

/* Unpack uint32_t from network byte order */
static uint32_t unpack_u32(const uint8_t* buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) |
           (uint32_t)buf[3];
}

/* Unpack uint64_t from network byte order */
static uint64_t unpack_u64(const uint8_t* buf)
{
    return ((uint64_t)buf[0] << 56) |
           ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) |
           ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) |
           ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8) |
           (uint64_t)buf[7];
}

/* Get current time in milliseconds */
static uint64_t get_current_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ========== NNG Message Header Operations ========== */

/* 
 * NNG message header layout:
 * [0]      - Protocol version (1 byte)
 * [1]      - Message type (1 byte)
 * [2-5]    - Flags/Reserved (4 bytes)
 * [6-13]   - Data field 1 (uint64_t: file_size or chunk_offset)
 * [14-17]  - Data field 2 (uint32_t: chunk_size or error_code)
 * [18-21]  - Data field 3 (uint32_t: reserved)
 * Total: 22 bytes header
 */

#define HEADER_SIZE 22

/* Build message header */
static void build_header(uint8_t* header, uint8_t msg_type, 
                         uint64_t field1, uint32_t field2, uint32_t field3)
{
    header[0] = PROTOCOL_VERSION;
    header[1] = msg_type;
    pack_u32(&header[2], 0);           /* Flags */
    pack_u64(&header[6], field1);      /* Field 1: file_size or offset */
    pack_u32(&header[14], field2);     /* Field 2: chunk_size or error_code */
    pack_u32(&header[18], field3);     /* Field 3: reserved */
}

/* Parse message header, returns message type */
static uint8_t parse_header(const uint8_t* header, uint64_t* field1, 
                            uint32_t* field2, uint32_t* field3)
{
    if (header[0] != PROTOCOL_VERSION) {
        return 0; /* Invalid version */
    }
    *field1 = unpack_u64(&header[6]);
    *field2 = unpack_u32(&header[14]);
    *field3 = unpack_u32(&header[18]);
    return header[1]; /* Message type */
}

/* ========== Transfer Context ========== */

typedef struct {
    nng_socket socket;
    char peer_ip[64];
    int port;
    
    /* File handling */
    FILE* file_handle;
    int file_descriptor;
    char file_name[MAX_FILENAME_LEN];
    char file_path[512];
    char folder[MAX_FOLDER_LEN];
    char save_dir[512];
    uint64_t file_size;
    uint64_t bytes_transferred;
    
    /* Buffer */
    uint8_t* buffer;
    size_t buffer_size;
    
    /* State Management */
    lanshare_transfer_state_t state;
    lanshare_transfer_type_t type;
    
    /* Callbacks */
    lanshare_transfer_callback_t callback;
    void* user_data;
    
    /* Protocol state */
    int header_sent;
    int header_received;
    uint32_t chunk_index;
    
    /* Timing for speed calculation */
    uint64_t start_time_ms;
    uint64_t last_update_time_ms;
    uint64_t last_bytes_transferred;
    
    /* Confirmation state (for sender) */
    int waiting_confirm;
    uint64_t confirm_start_time_ms;
    
    /* Is sender flag */
    int is_sender;
} nng_transfer_ctx_t;

/* Helper: Create URL from IP and port */
static void make_url(char* url, size_t url_size, const char* ip, int port)
{
    snprintf(url, url_size, "tcp://%s:%d", ip, port);
}

/* ========== File Operations (from transfer_file.c) ========== */

extern FILE* lanshare_file_open_read(const char* path);
extern FILE* lanshare_file_open_from_fd(int fd);
extern FILE* lanshare_file_open_write(const char* path);
extern void lanshare_file_close(FILE* file);
extern size_t lanshare_file_read(FILE* file, void* buffer, size_t size);
extern size_t lanshare_file_write(FILE* file, const void* buffer, size_t size);
extern long long lanshare_file_get_size(FILE* file);

/* ========== Upload Context Management ========== */

lanshare_upload_handle_t lanshare_upload_create(const char* peer_ip, int port,
                                                  const char* local_file_path,
                                                  const char* folder_name)
{
    if (!peer_ip || !local_file_path) {
        return NULL;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)calloc(1, sizeof(nng_transfer_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    strncpy(ctx->peer_ip, peer_ip, sizeof(ctx->peer_ip) - 1);
    ctx->port = port;
    strncpy(ctx->file_path, local_file_path, sizeof(ctx->file_path) - 1);
    
    /* Extract filename from path */
    const char* last_slash = strrchr(local_file_path, '/');
    if (!last_slash) {
        last_slash = strrchr(local_file_path, '\\');
    }
    strncpy(ctx->file_name, last_slash ? last_slash + 1 : local_file_path, sizeof(ctx->file_name) - 1);
    
    if (folder_name) {
        strncpy(ctx->folder, folder_name, sizeof(ctx->folder) - 1);
    }
    
    ctx->type = TRANSFER_TYPE_UPLOAD;
    ctx->state = TRANSFER_STATE_WAITING;
    ctx->file_descriptor = -1;
    ctx->buffer_size = DEFAULT_CHUNK_SIZE;
    ctx->buffer = (uint8_t*)malloc(ctx->buffer_size);
    ctx->is_sender = 1;
    ctx->waiting_confirm = 1;
    ctx->start_time_ms = 0;
    ctx->last_update_time_ms = 0;
    ctx->last_bytes_transferred = 0;
    
    if (!ctx->buffer) {
        free(ctx);
        return NULL;
    }
    
    return (lanshare_upload_handle_t)ctx;
}

lanshare_upload_handle_t lanshare_upload_create_with_fd(const char* peer_ip, int port,
                                                          int file_descriptor,
                                                          const char* file_name,
                                                          const char* folder_name)
{
    if (!peer_ip || file_descriptor < 0 || !file_name) {
        return NULL;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)calloc(1, sizeof(nng_transfer_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    strncpy(ctx->peer_ip, peer_ip, sizeof(ctx->peer_ip) - 1);
    ctx->port = port;
    ctx->file_descriptor = file_descriptor;
    strncpy(ctx->file_name, file_name, sizeof(ctx->file_name) - 1);
    
    if (folder_name) {
        strncpy(ctx->folder, folder_name, sizeof(ctx->folder) - 1);
    }
    
    ctx->type = TRANSFER_TYPE_UPLOAD;
    ctx->state = TRANSFER_STATE_WAITING;
    ctx->buffer_size = DEFAULT_CHUNK_SIZE;
    ctx->buffer = (uint8_t*)malloc(ctx->buffer_size);
    ctx->is_sender = 1;
    ctx->waiting_confirm = 1;
    ctx->start_time_ms = 0;
    ctx->last_update_time_ms = 0;
    ctx->last_bytes_transferred = 0;
    
    if (!ctx->buffer) {
        free(ctx);
        return NULL;
    }
    
    return (lanshare_upload_handle_t)ctx;
}

void lanshare_upload_destroy(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)handle;
    
    if (ctx->file_handle) {
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
    } else if (ctx->file_descriptor >= 0) {
        /* If file_handle was never opened but fd was set, close it directly */
        close(ctx->file_descriptor);
        ctx->file_descriptor = -1;
    }
    
    if (ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
    
    /* Only close socket if it was initialized (socket is valid after nng_pair0_open succeeds) */
    /* Check if socket was created by looking at state - it's set to PENDING_CONFIRM after socket creation */
    if (ctx->state == TRANSFER_STATE_PENDING_CONFIRM || 
        ctx->state == TRANSFER_STATE_CONFIRMED ||
        ctx->state == TRANSFER_STATE_TRANSFERRING ||
        ctx->state == TRANSFER_STATE_PAUSED ||
        ctx->state == TRANSFER_STATE_FINISHED ||
        ctx->state == TRANSFER_STATE_CANCELLED) {
        nng_socket_close(ctx->socket);
    }
    
    free(ctx);
}

/* ========== Upload Implementation ========== */

/* Send file info message */
static nng_err send_file_info(nng_socket sock, const char* filename, 
                                const char* folder, uint64_t file_size)
{
    nng_msg* msg = NULL;
    nng_err rv = nng_msg_alloc(&msg, 0);
    if (rv != NNG_OK) {
        return rv;
    }
    
    /* Build header: type=FILE_INFO, field1=file_size, field2=0, field3=0 */
    uint8_t header[HEADER_SIZE];
    build_header(header, MSG_TYPE_FILE_INFO, file_size, (uint32_t)0, (uint32_t)0);
    
    /* Append header to message body (we use body for protocol compatibility) */
    rv = nng_msg_append(msg, header, HEADER_SIZE);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    /* Append filename and folder as JSON in body */
    char metadata[512];
    int len = snprintf(metadata, sizeof(metadata), 
                       "{\"name\":\"%s\",\"folder\":\"%s\"}", 
                       filename, folder ? folder : "");
    if (len < 0 || len >= (int)sizeof(metadata)) {
        nng_msg_free(msg);
        return NNG_EINVAL;
    }
    
    rv = nng_msg_append(msg, metadata, len);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    rv = nng_sendmsg(sock, msg, 0);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    return NNG_OK;
}

/* Send file data chunk */
static nng_err send_file_data(nng_socket sock, uint32_t chunk_index,
                                uint64_t offset, const uint8_t* data, 
                                uint32_t data_len)
{
    nng_msg* msg = NULL;
    nng_err rv = nng_msg_alloc(&msg, 0);
    if (rv != NNG_OK) {
        return rv;
    }
    
    /* Build header: type=FILE_DATA, field1=offset, field2=data_len, field3=chunk_index */
    uint8_t header[HEADER_SIZE];
    build_header(header, MSG_TYPE_FILE_DATA, offset, data_len, chunk_index);
    
    rv = nng_msg_append(msg, header, HEADER_SIZE);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    rv = nng_msg_append(msg, data, data_len);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    rv = nng_sendmsg(sock, msg, 0);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    return NNG_OK;
}

/* Send end marker */
static nng_err send_file_end(nng_socket sock, uint64_t total_bytes)
{
    nng_msg* msg = NULL;
    nng_err rv = nng_msg_alloc(&msg, 0);
    if (rv != NNG_OK) {
        return rv;
    }
    
    /* Build header: type=FILE_END, field1=total_bytes, field2=0, field3=0 */
    uint8_t header[HEADER_SIZE];
    build_header(header, MSG_TYPE_FILE_END, total_bytes, (uint32_t)0, (uint32_t)0);
    
    LOGI("NNG", "send_file_end: total_bytes=%llu, header[0]=%u, header[1]=%u", 
         total_bytes, header[0], header[1]);
    
    rv = nng_msg_append(msg, header, HEADER_SIZE);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    rv = nng_sendmsg(sock, msg, 0);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    return NNG_OK;
}

/* Send confirm message (receiver accepts file) */
static nng_err send_confirm(nng_socket sock)
{
    nng_msg* msg = NULL;
    nng_err rv = nng_msg_alloc(&msg, 0);
    if (rv != NNG_OK) {
        return rv;
    }
    
    uint8_t header[HEADER_SIZE];
    build_header(header, MSG_TYPE_CONFIRM, 0, 0, 0);
    
    rv = nng_msg_append(msg, header, HEADER_SIZE);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    rv = nng_sendmsg(sock, msg, 0);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    LOGI("NNG", "Sent CONFIRM message");
    return NNG_OK;
}

/* Send reject message (receiver rejects file) */
static nng_err send_reject(nng_socket sock)
{
    nng_msg* msg = NULL;
    nng_err rv = nng_msg_alloc(&msg, 0);
    if (rv != NNG_OK) {
        return rv;
    }
    
    uint8_t header[HEADER_SIZE];
    build_header(header, MSG_TYPE_REJECT, 0, 0, 0);
    
    rv = nng_msg_append(msg, header, HEADER_SIZE);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    rv = nng_sendmsg(sock, msg, 0);
    if (rv != NNG_OK) {
        nng_msg_free(msg);
        return rv;
    }
    
    LOGI("NNG", "Sent REJECT message");
    return NNG_OK;
}

int lanshare_upload_start(lanshare_upload_handle_t handle,
                           lanshare_transfer_callback_t callback,
                           void* user_data)
{
    if (!handle) {
        return -1;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)handle;
    ctx->callback = callback;
    ctx->user_data = user_data;
    
    /* Open file */
    if (ctx->file_descriptor >= 0) {
        ctx->file_handle = lanshare_file_open_from_fd(ctx->file_descriptor);
        LOGI("NNG", "Opened file from fd: %d", ctx->file_descriptor);
        /* fd is now owned by FILE*, mark as -1 to avoid double close */
        ctx->file_descriptor = -1;
    } else {
        ctx->file_handle = lanshare_file_open_read(ctx->file_path);
    }
    
    if (!ctx->file_handle) {
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    long long file_size_ll = lanshare_file_get_size(ctx->file_handle);
    LOGI("NNG", "File size from lanshare_file_get_size: %lld", file_size_ll);
    if (file_size_ll < 0) {
        LOGE("NNG", "Failed to get file size");
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    ctx->file_size = (uint64_t)file_size_ll;
    LOGI("NNG", "File opened: size=%llu, file_handle=%p", ctx->file_size, (void*)ctx->file_handle);
    
    /* Initialize NNG */
    nng_err rv = nng_init_once();
    if (rv != NNG_OK) {
        LOGE("NNG", "nng_init failed: %s", nng_strerror(rv));
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    /* Create NNG PAIR socket - NNG 2.0 uses nng_pair0_open */
    rv = nng_pair0_open(&ctx->socket);
    if (rv != NNG_OK) {
        LOGE("NNG", "nng_pair0_open: %s", nng_strerror(rv));
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    /* Set timeouts - NNG 2.0 uses nng_socket_set_ms */
    nng_socket_set_ms(ctx->socket, NNG_OPT_SENDTIMEO, 30000);
    nng_socket_set_ms(ctx->socket, NNG_OPT_RECVTIMEO, 30000);
    
    /* Connect to peer */
    char url[128];
    make_url(url, sizeof(url), ctx->peer_ip, ctx->port);
    LOGI("NNG", "Connecting to %s", url);
    
    rv = nng_dial(ctx->socket, url, NULL, 0);
    if (rv != NNG_OK) {
        LOGE("NNG", "nng_dial: %s", nng_strerror(rv));
        nng_socket_close(ctx->socket);
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    ctx->state = TRANSFER_STATE_PENDING_CONFIRM;
    ctx->confirm_start_time_ms = get_current_time_ms();
    LOGI("NNG", "Connected, waiting for receiver confirmation: %s (%llu bytes)", ctx->file_name, ctx->file_size);
    
    /* Send file info message */
    LOGI("NNG", "Sending FILE_INFO: file_name=%s, file_size=%llu", ctx->file_name, ctx->file_size);
    rv = send_file_info(ctx->socket, ctx->file_name, ctx->folder, ctx->file_size);
    if (rv != NNG_OK) {
        LOGE("NNG", "Failed to send file info: %s", nng_strerror(rv));
        ctx->state = TRANSFER_STATE_ERROR;
        goto cleanup;
    }
    
    ctx->header_sent = 1;
    LOGI("NNG", "File info sent, waiting for confirmation...");
    
    /* Wait for confirmation with timeout */
    nng_msg* confirm_msg = NULL;
    int confirmed = 0;
    while (!confirmed) {
        /* Check for timeout */
        uint64_t current_time = get_current_time_ms();
        if (current_time - ctx->confirm_start_time_ms > CONFIRM_TIMEOUT_MS) {
            LOGE("NNG", "Confirmation timeout after %d ms", CONFIRM_TIMEOUT_MS);
            ctx->state = TRANSFER_STATE_TIMEOUT;
            goto cleanup;
        }
        
        /* Try to receive confirmation with short timeout */
        rv = nng_recvmsg(ctx->socket, &confirm_msg, 0);
        if (rv == NNG_ETIMEDOUT) {
            /* Update callback to show waiting state */
            if (callback) {
                lanshare_transfer_info_t info;
                memset(&info, 0, sizeof(info));
                strncpy(info.file_path, ctx->file_path, sizeof(info.file_path) - 1);
                strncpy(info.file_name, ctx->file_name, sizeof(info.file_name) - 1);
                strncpy(info.peer_id, ctx->peer_ip, sizeof(info.peer_id) - 1);
                info.state = ctx->state;
                info.type = ctx->type;
                info.data_size = (long long)ctx->file_size;
                info.bytes_transferred = 0;
                info.progress = 0;
                info.transfer_speed = 0.0;
                info.is_sender = ctx->is_sender;
                callback(&info, user_data);
            }
            continue;
        }
        if (rv != NNG_OK) {
            LOGE("NNG", "Failed to receive confirmation: %s", nng_strerror(rv));
            ctx->state = TRANSFER_STATE_ERROR;
            goto cleanup;
        }
        
        /* Parse the message */
        size_t msg_len = nng_msg_len(confirm_msg);
        if (msg_len >= HEADER_SIZE) {
            uint8_t* msg_data = (uint8_t*)nng_msg_body(confirm_msg);
            uint64_t field1 = 0;
            uint32_t field2 = 0, field3 = 0;
            uint8_t msg_type = parse_header(msg_data, &field1, &field2, &field3);
            
            if (msg_type == MSG_TYPE_CONFIRM) {
                LOGI("NNG", "Received CONFIRM message");
                confirmed = 1;
                ctx->state = TRANSFER_STATE_CONFIRMED;
            } else if (msg_type == MSG_TYPE_REJECT) {
                LOGI("NNG", "Received REJECT message");
                ctx->state = TRANSFER_STATE_REJECTED;
                nng_msg_free(confirm_msg);
                goto cleanup;
            } else if (msg_type == MSG_TYPE_CANCEL) {
                LOGI("NNG", "Received CANCEL message");
                ctx->state = TRANSFER_STATE_CANCELLED;
                nng_msg_free(confirm_msg);
                goto cleanup;
            }
        }
        nng_msg_free(confirm_msg);
    }
    
    /* Start transfer */
    ctx->state = TRANSFER_STATE_TRANSFERRING;
    ctx->start_time_ms = get_current_time_ms();
    ctx->last_update_time_ms = ctx->start_time_ms;
    ctx->last_bytes_transferred = 0;
    LOGI("NNG", "Receiver confirmed, starting file transfer");
    
    /* Send file data in chunks */
    uint64_t bytes_remaining = ctx->file_size;
    uint64_t offset = 0;
    ctx->chunk_index = 0;
    
    LOGI("NNG", "Starting file transfer loop: bytes_remaining=%llu", bytes_remaining);
    
    while (bytes_remaining > 0 && ctx->state == TRANSFER_STATE_TRANSFERRING) {
        size_t chunk_size = (size_t)(bytes_remaining < (uint64_t)ctx->buffer_size ? 
                                     bytes_remaining : ctx->buffer_size);
        
        size_t bytes_read = lanshare_file_read(ctx->file_handle, ctx->buffer, chunk_size);
        LOGI("NNG", "File read: chunk_size=%zu, bytes_read=%zu", chunk_size, bytes_read);
        if (bytes_read == 0) {
            LOGE("NNG", "File read returned 0, breaking loop");
            break;
        }
        
        LOGI("NNG", "Sending FILE_DATA: chunk=%u, offset=%llu, len=%zu", 
             ctx->chunk_index, offset, bytes_read);
        rv = send_file_data(ctx->socket, ctx->chunk_index, offset, 
                            ctx->buffer, (uint32_t)bytes_read);
        if (rv != NNG_OK) {
            LOGE("NNG", "Failed to send chunk %u: %s", ctx->chunk_index, nng_strerror(rv));
            ctx->state = TRANSFER_STATE_ERROR;
            break;
        }
        
        ctx->bytes_transferred += bytes_read;
        offset += bytes_read;
        bytes_remaining -= bytes_read;
        ctx->chunk_index++;
        
        /* Calculate transfer speed - use sliding window for smooth updates */
        uint64_t current_time = get_current_time_ms();
        double speed = 0.0;
        if (current_time > ctx->start_time_ms) {
            uint64_t elapsed = current_time - ctx->start_time_ms;
            if (elapsed > 0) {
                /* Calculate average speed since transfer started */
                speed = (double)ctx->bytes_transferred * 1000.0 / (double)elapsed;
            }
        }
        
        /* Also update instant speed every interval for more responsive display */
        if (current_time > ctx->last_update_time_ms) {
            uint64_t time_diff = current_time - ctx->last_update_time_ms;
            if (time_diff >= SPEED_CALC_INTERVAL_MS) {
                ctx->last_update_time_ms = current_time;
                ctx->last_bytes_transferred = ctx->bytes_transferred;
            }
        }
        
        /* Update progress */
        if (callback) {
            lanshare_transfer_info_t info;
            memset(&info, 0, sizeof(info));
            strncpy(info.file_path, ctx->file_path, sizeof(info.file_path) - 1);
            strncpy(info.file_name, ctx->file_name, sizeof(info.file_name) - 1);
            strncpy(info.peer_id, ctx->peer_ip, sizeof(info.peer_id) - 1);
            info.state = ctx->state;
            info.type = ctx->type;
            info.data_size = (long long)ctx->file_size;
            info.bytes_transferred = (long long)ctx->bytes_transferred;
            info.progress = (int)((ctx->bytes_transferred * 100) / ctx->file_size);
            info.transfer_speed = speed;
            info.is_sender = ctx->is_sender;
            callback(&info, user_data);
        }
    }
    
    /* Send end marker if successful */
    if (ctx->state == TRANSFER_STATE_TRANSFERRING) {
        LOGI("NNG", "Sending FILE_END: bytes_transferred=%llu", ctx->bytes_transferred);
        rv = send_file_end(ctx->socket, ctx->bytes_transferred);
        if (rv != NNG_OK) {
            LOGE("NNG", "Failed to send end marker: %s", nng_strerror(rv));
        } else {
            LOGI("NNG", "Upload finished: %llu bytes", ctx->bytes_transferred);
        }
    }
    
cleanup:
    if (ctx->state == TRANSFER_STATE_TRANSFERRING) {
        ctx->state = TRANSFER_STATE_FINISHED;
    }
    
    nng_socket_close(ctx->socket);
    
    if (ctx->file_handle) {
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
    }
    
    /* Clean up file descriptor if still open (in case fdopen failed) */
    if (ctx->file_descriptor >= 0) {
        close(ctx->file_descriptor);
        ctx->file_descriptor = -1;
    }
    
    return (ctx->state == TRANSFER_STATE_FINISHED) ? 0 : -1;
}

/* ========== Upload Control ========== */

int lanshare_upload_pause(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)handle;
    ctx->state = TRANSFER_STATE_PAUSED;
    return 0;
}

int lanshare_upload_resume(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)handle;
    if (ctx->state == TRANSFER_STATE_PAUSED) {
        ctx->state = TRANSFER_STATE_TRANSFERRING;
    }
    return 0;
}

int lanshare_upload_cancel(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)handle;
    ctx->state = TRANSFER_STATE_CANCELLED;
    return 0;
}

/* ========== Download Server ========== */

static nng_socket g_server_socket;
static int g_server_socket_valid = 0;
static int g_server_port = 0;
static char g_save_dir[512] = {0};
static lanshare_transfer_callback_t g_server_callback = NULL;
static void* g_server_user_data = NULL;

/* Parse JSON metadata to extract filename and folder */
static int parse_metadata(const char* json, char* filename, size_t filename_size,
                          char* folder, size_t folder_size)
{
    /* Simple JSON parsing - find fields by key */
    const char* name_key = "\"name\":\"";
    const char* name_start = strstr(json, name_key);
    if (name_start) {
        name_start += strlen(name_key);
        const char* name_end = strchr(name_start, '"');
        if (name_end) {
            size_t len = (size_t)(name_end - name_start);
            if (len < filename_size) {
                strncpy(filename, name_start, len);
                filename[len] = '\0';
            }
        }
    }
    
    const char* folder_key = "\"folder\":\"";
    const char* folder_start = strstr(json, folder_key);
    if (folder_start) {
        folder_start += strlen(folder_key);
        const char* folder_end = strchr(folder_start, '"');
        if (folder_end) {
            size_t len = (size_t)(folder_end - folder_start);
            if (len < folder_size) {
                strncpy(folder, folder_start, len);
                folder[len] = '\0';
            }
        }
    }
    
    return (filename[0] != '\0') ? 0 : -1;
}

/* Global state for pending confirmation */
static volatile int g_pending_confirm = 0;
static volatile int g_user_decision = 0; /* 0 = pending, 1 = accept, 2 = reject */
static char g_pending_filename[MAX_FILENAME_LEN] = {0};
static uint64_t g_pending_file_size = 0;

/* Set user decision for pending transfer (called from UI thread) */
void lanshare_set_transfer_decision(int accept)
{
    g_user_decision = accept ? 1 : 2;
}

/* Get pending transfer info */
int lanshare_get_pending_transfer(char* filename, size_t filename_size, uint64_t* file_size)
{
    if (!g_pending_confirm) {
        return -1;
    }
    if (filename && filename_size > 0) {
        strncpy(filename, g_pending_filename, filename_size - 1);
        filename[filename_size - 1] = '\0';
    }
    if (file_size) {
        *file_size = g_pending_file_size;
    }
    return 0;
}

/* Clear pending transfer state */
static void clear_pending_transfer(void)
{
    g_pending_confirm = 0;
    g_user_decision = 0;
    g_pending_filename[0] = '\0';
    g_pending_file_size = 0;
}

/* Handle incoming connection - NNG 2.0 version using application protocol */
static void handle_connection(nng_socket sock)
{
    char filename[MAX_FILENAME_LEN] = {0};
    char folder[MAX_FOLDER_LEN] = {0};
    uint64_t file_size = 0;
    uint64_t bytes_received = 0;
    nng_msg* msg = NULL;
    FILE* file = NULL;
    int transfer_active = 0;
    int waiting_user_confirm = 0;
    uint64_t recv_start_time = 0;  /* Per-connection start time for speed calculation */
    uint64_t last_update_time = 0; /* Last speed update time */
    uint64_t last_bytes = 0;       /* Bytes transferred at last update */
    
    LOGI("NNG", "Waiting for file transfer...");
    
    while (1) {
        nng_err rv = nng_recvmsg(sock, &msg, 0);
        if (rv != NNG_OK) {
            LOGE("NNG", "Failed to receive message: %s", nng_strerror(rv));
            break;
        }
        
        size_t msg_len = nng_msg_len(msg);
        if (msg_len < HEADER_SIZE) {
            LOGE("NNG", "Message too small: %zu", msg_len);
            nng_msg_free(msg);
            continue;
        }
        
        uint8_t* msg_data = (uint8_t*)nng_msg_body(msg);
        uint64_t field1 = 0;
        uint32_t field2 = 0, field3 = 0;
        uint8_t msg_type = parse_header(msg_data, &field1, &field2, &field3);
        
        LOGI("NNG", "Received msg type=0x%02X, len=%zu, field1=%llu, field2=%u, field3=%u",
             msg_type, msg_len, field1, field2, field3);
        
        switch (msg_type) {
            case MSG_TYPE_FILE_INFO: {
                /* Parse file info */
                file_size = field1;
                LOGI("NNG", "FILE_INFO: file_size=%llu, field2=%u, field3=%u, metadata_len=%zu",
                     file_size, field2, field3, msg_len - HEADER_SIZE);
                size_t metadata_len = msg_len - HEADER_SIZE;
                
                if (metadata_len > 0) {
                    char metadata[512];
                    size_t copy_len = metadata_len < sizeof(metadata) - 1 ? 
                                      metadata_len : sizeof(metadata) - 1;
                    memcpy(metadata, msg_data + HEADER_SIZE, copy_len);
                    metadata[copy_len] = '\0';
                    
                    if (parse_metadata(metadata, filename, sizeof(filename),
                                       folder, sizeof(folder)) != 0) {
                        LOGE("NNG", "Failed to parse metadata");
                        nng_msg_free(msg);
                        continue;
                    }
                }
                
                LOGI("NNG", "Receiving file: %s, size: %llu", filename, file_size);
                
                /* Set pending transfer state */
                clear_pending_transfer();
                strncpy(g_pending_filename, filename, sizeof(g_pending_filename) - 1);
                g_pending_file_size = file_size;
                g_pending_confirm = 1;
                waiting_user_confirm = 1;
                
                /* Notify UI about incoming transfer */
                if (g_server_callback) {
                    lanshare_transfer_info_t info;
                    memset(&info, 0, sizeof(info));
                    strncpy(info.file_path, filename, sizeof(info.file_path) - 1);
                    strncpy(info.file_name, filename, sizeof(info.file_name) - 1);
                    info.state = TRANSFER_STATE_PENDING_CONFIRM;
                    info.type = TRANSFER_TYPE_DOWNLOAD;
                    info.data_size = (long long)file_size;
                    info.bytes_transferred = 0;
                    info.progress = 0;
                    info.is_sender = 0;
                    g_server_callback(&info, g_server_user_data);
                }
                
                /* Wait for user decision with timeout */
                uint64_t confirm_start = get_current_time_ms();
                while (waiting_user_confirm && g_pending_confirm) {
                    /* Check for timeout */
                    if (get_current_time_ms() - confirm_start > CONFIRM_TIMEOUT_MS) {
                        LOGE("NNG", "User confirmation timeout");
                        send_reject(sock);
                        clear_pending_transfer();
                        waiting_user_confirm = 0;
                        nng_msg_free(msg);
                        goto transfer_end;
                    }
                    
                    /* Check user decision */
                    if (g_user_decision == 1) {
                        /* User accepted */
                        LOGI("NNG", "User accepted transfer");
                        rv = send_confirm(sock);
                        if (rv != NNG_OK) {
                            LOGE("NNG", "Failed to send confirm: %s", nng_strerror(rv));
                            clear_pending_transfer();
                            waiting_user_confirm = 0;
                            nng_msg_free(msg);
                            goto transfer_end;
                        }
                        waiting_user_confirm = 0;
                    } else if (g_user_decision == 2) {
                        /* User rejected */
                        LOGI("NNG", "User rejected transfer");
                        send_reject(sock);
                        clear_pending_transfer();
                        waiting_user_confirm = 0;
                        nng_msg_free(msg);
                        goto transfer_end;
                    }
                    
                    /* Small delay to avoid busy waiting */
                    nng_msleep(100);
                }
                
                /* Build full path */
                char full_path[1024];
                if (g_save_dir[0] != '\0') {
                    snprintf(full_path, sizeof(full_path), "%s/%s", g_save_dir, filename);
                } else {
                    strncpy(full_path, filename, sizeof(full_path) - 1);
                }
                
                /* Open file for writing */
                file = lanshare_file_open_write(full_path);
                if (!file) {
                    LOGE("NNG", "Failed to create file: %s", full_path);
                    nng_msg_free(msg);
                    return;
                }
                
                transfer_active = 1;
                bytes_received = 0;
                clear_pending_transfer();
                
                /* Notify UI that transfer is starting */
                if (g_server_callback) {
                    lanshare_transfer_info_t info;
                    memset(&info, 0, sizeof(info));
                    strncpy(info.file_path, filename, sizeof(info.file_path) - 1);
                    strncpy(info.file_name, filename, sizeof(info.file_name) - 1);
                    info.state = TRANSFER_STATE_TRANSFERRING;
                    info.type = TRANSFER_TYPE_DOWNLOAD;
                    info.data_size = (long long)file_size;
                    info.bytes_transferred = 0;
                    info.progress = 0;
                    info.is_sender = 0;
                    g_server_callback(&info, g_server_user_data);
                }
                break;
            }
            
            case MSG_TYPE_FILE_DATA: {
                if (!transfer_active || !file) {
                    LOGE("NNG", "Received data before file info");
                    nng_msg_free(msg);
                    continue;
                }
                
                uint64_t offset = field1;
                uint32_t data_len = field2;
                uint32_t chunk_idx = field3;
                
                LOGI("NNG", "FILE_DATA: chunk=%u, offset=%llu, data_len=%u, payload=%zu",
                     chunk_idx, offset, data_len, msg_len - HEADER_SIZE);
                (void)offset;  /* TODO: Support out-of-order chunks */
                (void)chunk_idx;
                
                size_t payload_len = msg_len - HEADER_SIZE;
                size_t to_write = (payload_len < data_len) ? payload_len : data_len;
                
                if (to_write > 0) {
                    size_t written = lanshare_file_write(file, msg_data + HEADER_SIZE, to_write);
                    if (written != to_write) {
                        LOGE("NNG", "Failed to write file data");
                        nng_msg_free(msg);
                        goto transfer_end;
                    }
                    bytes_received += written;
                }
                
                /* Calculate transfer speed - use cumulative average */
                uint64_t current_time = get_current_time_ms();
                double speed = 0.0;
                
                /* Initialize start time on first data chunk */
                if (recv_start_time == 0) {
                    recv_start_time = current_time;
                    last_update_time = current_time;
                    last_bytes = 0;
                }
                
                /* Calculate average speed since transfer started */
                uint64_t elapsed = current_time - recv_start_time;
                if (elapsed > 0) {
                    speed = (double)bytes_received * 1000.0 / (double)elapsed;
                }
                
                /* Update interval tracking */
                if (current_time - last_update_time >= SPEED_CALC_INTERVAL_MS) {
                    last_update_time = current_time;
                    last_bytes = bytes_received;
                }
                
                /* Update progress */
                if (g_server_callback && file_size > 0) {
                    lanshare_transfer_info_t info;
                    memset(&info, 0, sizeof(info));
                    strncpy(info.file_path, filename, sizeof(info.file_path) - 1);
                    strncpy(info.file_name, filename, sizeof(info.file_name) - 1);
                    info.state = TRANSFER_STATE_TRANSFERRING;
                    info.type = TRANSFER_TYPE_DOWNLOAD;
                    info.data_size = (long long)file_size;
                    info.bytes_transferred = (long long)bytes_received;
                    info.progress = (int)((bytes_received * 100) / file_size);
                    info.transfer_speed = speed;
                    info.is_sender = 0;
                    LOGI("NNG", "Download progress: %s, %llu/%llu bytes, %d%%, speed=%.2f B/s",
                         filename, bytes_received, file_size, info.progress, speed);
                    g_server_callback(&info, g_server_user_data);
                }
                break;
            }
            
            case MSG_TYPE_FILE_END: {
                uint64_t total_sent = field1;
                LOGI("NNG", "Transfer complete: %llu bytes received (expected %llu, field2=%u, field3=%u)", 
                     bytes_received, total_sent, field2, field3);
                
                /* Send finished callback */
                if (g_server_callback) {
                    lanshare_transfer_info_t info;
                    memset(&info, 0, sizeof(info));
                    strncpy(info.file_path, filename, sizeof(info.file_path) - 1);
                    strncpy(info.file_name, filename, sizeof(info.file_name) - 1);
                    info.state = TRANSFER_STATE_FINISHED;
                    info.type = TRANSFER_TYPE_DOWNLOAD;
                    info.data_size = (long long)file_size;
                    info.bytes_transferred = (long long)bytes_received;
                    info.progress = 100;
                    info.transfer_speed = 0.0;
                    info.is_sender = 0;
                    g_server_callback(&info, g_server_user_data);
                }
                
                transfer_active = 0;
                nng_msg_free(msg);
                goto transfer_end;
            }
            
            case MSG_TYPE_CANCEL: {
                LOGI("NNG", "Transfer cancelled by peer");
                transfer_active = 0;
                nng_msg_free(msg);
                goto transfer_end;
            }
            
            default:
                LOGW("NNG", "Unknown message type: 0x%02X", msg_type);
                break;
        }
        
        nng_msg_free(msg);
    }
    
transfer_end:
    if (file) {
        lanshare_file_close(file);
    }
    
    LOGI("NNG", "Download finished: %llu bytes", bytes_received);
}

/* Helper function to create and listen on a socket */
static int server_socket_create_and_listen(nng_socket* socket, const char* url) {
    nng_err rv;
    
    /* Create NNG PAIR socket - NNG 2.0 uses nng_pair0_open */
    rv = nng_pair0_open(socket);
    if (rv != NNG_OK) {
        LOGE("NNG", "nng_pair0_open: %s", nng_strerror(rv));
        return -1;
    }
    
    /* Set timeouts - NNG 2.0 uses nng_socket_set_ms */
    /* Use longer timeout for file transfers (5 minutes) */
    nng_socket_set_ms(*socket, NNG_OPT_SENDTIMEO, 300000);
    nng_socket_set_ms(*socket, NNG_OPT_RECVTIMEO, 300000);
    
    /* Listen on the specified URL */
    rv = nng_listen(*socket, url, NULL, 0);
    if (rv != NNG_OK) {
        LOGE("NNG", "nng_listen: %s", nng_strerror(rv));
        nng_socket_close(*socket);
        return -1;
    }
    
    return 0;
}

int lanshare_download_server_start(int port,
                                    const char* save_dir,
                                    lanshare_transfer_callback_t callback,
                                    void* user_data)
{
    if (g_server_port != 0) {
        /* Server already running */
        return -1;
    }
    
    /* Initialize NNG */
    nng_err rv = nng_init_once();
    if (rv != NNG_OK) {
        LOGE("NNG", "nng_init failed: %s", nng_strerror(rv));
        return -1;
    }
    
    /* Build URL */
    char url[128];
    snprintf(url, sizeof(url), "tcp://0.0.0.0:%d", port);
    
    /* Create and listen on socket (only once) */
    if (server_socket_create_and_listen(&g_server_socket, url) != 0) {
        return -1;
    }
    g_server_socket_valid = 1;
    
    g_server_port = port;
    if (save_dir) {
        strncpy(g_save_dir, save_dir, sizeof(g_save_dir) - 1);
    }
    g_server_callback = callback;
    g_server_user_data = user_data;
    
    LOGI("NNG", "Download server started on port %d", port);
    
    /* Accept and handle connections in a loop */
    /* Note: In a real implementation, this should run in a separate thread */
    while (g_server_port != 0) {
        handle_connection(g_server_socket);
        LOGI("NNG", "Connection closed, waiting for next connection...");
        
        /* Socket remains open, just wait for next connection */
        /* Only rebuild socket on error */
    }
    
    /* Cleanup when server stops */
    if (g_server_socket_valid) {
        nng_socket_close(g_server_socket);
        g_server_socket_valid = 0;
    }
    
    return 0;
}

int lanshare_download_server_stop(void)
{
    if (g_server_port == 0) {
        return -1;
    }
    
    if (g_server_socket_valid) {
        nng_socket_close(g_server_socket);
        g_server_socket_valid = 0;
    }
    g_server_port = 0;
    
    return 0;
}

/* ========== Common API ========== */

int lanshare_get_transfer_info(void* handle, int is_upload, lanshare_transfer_info_t* info)
{
    if (!handle || !info) {
        return -1;
    }
    
    nng_transfer_ctx_t* ctx = (nng_transfer_ctx_t*)handle;
    
    memset(info, 0, sizeof(*info));
    strncpy(info->file_path, ctx->file_path, sizeof(info->file_path) - 1);
    strncpy(info->file_name, ctx->file_name, sizeof(info->file_name) - 1);
    strncpy(info->peer_id, ctx->peer_ip, sizeof(info->peer_id) - 1);
    info->state = ctx->state;
    info->type = is_upload ? TRANSFER_TYPE_UPLOAD : TRANSFER_TYPE_DOWNLOAD;
    info->data_size = (long long)ctx->file_size;
    info->bytes_transferred = (long long)ctx->bytes_transferred;
    info->is_sender = ctx->is_sender;
    if (ctx->file_size > 0) {
        info->progress = (int)((ctx->bytes_transferred * 100) / ctx->file_size);
    }
    
    /* Calculate current speed */
    if (ctx->state == TRANSFER_STATE_TRANSFERRING && ctx->start_time_ms > 0) {
        uint64_t current_time = get_current_time_ms();
        uint64_t elapsed = current_time - ctx->start_time_ms;
        if (elapsed > 0) {
            info->transfer_speed = (double)ctx->bytes_transferred * 1000.0 / (double)elapsed;
        }
    }
    
    return 0;
}