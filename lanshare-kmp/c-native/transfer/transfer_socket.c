/*
    LAN-Share Socket-based Transfer Implementation
    Standard BSD socket implementation for cross-platform compatibility
    Compatible with Qt's QTcpSocket
    
    Copyright (C) 2026 LAN-Share Project
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "lanshare_transfer.h"
#include "../include/lanshare_log.h"

/* Platform-specific socket includes */
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32")
    #define SHUT_RDWR SD_BOTH
    /* Windows doesn't have ssize_t, define it */
    typedef int ssize_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <errno.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define closesocket close
#endif

/* Socket transfer context */
typedef struct {
    SOCKET socket_fd;
    struct sockaddr_in addr;
    int is_client;  /* 1 for client (upload), 0 for server (download) */
    
    /* File handling */
    FILE* file_handle;
    int file_descriptor;  /* File descriptor for content:// URI support */
    char file_name[256];  /* File name for fd-based uploads */
    char save_dir[512];   /* Directory to save downloaded files */
    long long file_size;
    long long bytes_transferred;
    long long bytes_to_send;
    
    /* Buffer */
    char buffer[65536];
    size_t buffer_size;
    
    /* State Management */
    lanshare_transfer_state_t state;
    lanshare_transfer_type_t type;
    char peer_ip[64];
    int port;
    char file_path[512];
    char folder[128];
    
    /* Callbacks */
    lanshare_transfer_callback_t callback;
    void* user_data;
    
    /* Protocol state */
    int header_sent;
    int header_received;
} socket_context_t;

/* Initialize socket library */
static int socket_library_init(void)
{
#ifdef _WIN32
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data);
#else
    return 0;
#endif
}

/* Cleanup socket library */
static void socket_library_cleanup(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

/* Create socket context for upload (client) */
static socket_context_t* socket_upload_create(const char* peer_ip, int port,
                                               const char* file_path,
                                               const char* folder_name)
{
    if (!peer_ip || !file_path) {
        return NULL;
    }
    LOGI("Socket", "socket_upload_create %d, %s", port, file_path);
    socket_context_t* ctx = (socket_context_t*)malloc(sizeof(socket_context_t));
    if (!ctx) {
        return NULL;
    }
    
    memset(ctx, 0, sizeof(socket_context_t));
    ctx->socket_fd = INVALID_SOCKET;
    ctx->is_client = 1;
    ctx->port = port;
    ctx->state = TRANSFER_STATE_IDLE;
    ctx->type = TRANSFER_TYPE_UPLOAD;
    
    strncpy(ctx->peer_ip, peer_ip, sizeof(ctx->peer_ip) - 1);
    strncpy(ctx->file_path, file_path, sizeof(ctx->file_path) - 1);
    
    if (folder_name) {
        strncpy(ctx->folder, folder_name, sizeof(ctx->folder) - 1);
    }
    
    ctx->buffer_size = sizeof(ctx->buffer);
    
    /* Initialize socket library if needed */
    socket_library_init();
    
    return ctx;
}

/* Create socket context for download (server) */
static socket_context_t* socket_download_create(int port)
{
    socket_context_t* ctx = (socket_context_t*)malloc(sizeof(socket_context_t));
    if (!ctx) {
        return NULL;
    }
    LOGI("Socket", "Creating download context");
    memset(ctx, 0, sizeof(socket_context_t));
    ctx->socket_fd = INVALID_SOCKET;
    ctx->is_client = 0;
    ctx->port = port;
    ctx->state = TRANSFER_STATE_IDLE;
    ctx->type = TRANSFER_TYPE_DOWNLOAD;
    ctx->buffer_size = sizeof(ctx->buffer);
    
    /* Initialize socket library if needed */
    socket_library_init();
    
    return ctx;
}

/* Create upload handle */
lanshare_upload_handle_t lanshare_upload_create(const char* peer_ip, int port,
                                                  const char* local_file_path,
                                                  const char* folder_name)
{
    return (lanshare_upload_handle_t)socket_upload_create(peer_ip, port, local_file_path, folder_name);
}

/* Create upload handle using file descriptor */
lanshare_upload_handle_t lanshare_upload_create_with_fd(const char* peer_ip, int port,
                                                          int file_descriptor,
                                                          const char* file_name,
                                                          const char* folder_name)
{
    if (!peer_ip || file_descriptor < 0 || !file_name) {
        return NULL;
    }
    
    socket_context_t* ctx = (socket_context_t*)malloc(sizeof(socket_context_t));
    if (!ctx) {
        return NULL;
    }
    
    memset(ctx, 0, sizeof(socket_context_t));
    ctx->socket_fd = INVALID_SOCKET;
    ctx->is_client = 1;
    ctx->port = port;
    ctx->state = TRANSFER_STATE_IDLE;
    ctx->type = TRANSFER_TYPE_UPLOAD;
    ctx->file_descriptor = file_descriptor;  /* Store the duplicated fd */
    strncpy(ctx->file_name, file_name, sizeof(ctx->file_name) - 1);
    strncpy(ctx->peer_ip, peer_ip, sizeof(ctx->peer_ip) - 1);
    
    if (folder_name) {
        strncpy(ctx->folder, folder_name, sizeof(ctx->folder) - 1);
    }
    
    ctx->buffer_size = sizeof(ctx->buffer);
    
    /* Initialize socket library if needed */
    socket_library_init();
    
    LOGI("Socket", "Created upload handle with fd: %d, filename: %s", file_descriptor, file_name);
    
    return (lanshare_upload_handle_t)ctx;
}

/* Create download handle */
lanshare_download_handle_t lanshare_download_create(const char* peer_ip, int port,
                                                      const char* remote_file_info)
{
    /* For client download, we need to know what file to request */
    /* remote_file_info can be filename or JSON with file info */
    return (lanshare_download_handle_t)socket_upload_create(peer_ip, port, remote_file_info, NULL);
}

/* Connect to peer (upload client) */
static int socket_connect(socket_context_t* ctx)
{
    if (!ctx || ctx->socket_fd != INVALID_SOCKET) {
        return -1;
    }
    
    /* Create socket */
    ctx->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->socket_fd == INVALID_SOCKET) {
        return -1;
    }
    
    /* Set up server address */
    memset(&ctx->addr, 0, sizeof(ctx->addr));
    ctx->addr.sin_family = AF_INET;
    ctx->addr.sin_port = htons((u_short)ctx->port);
    
    if (inet_pton(AF_INET, ctx->peer_ip, &ctx->addr.sin_addr) <= 0) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
        return -1;
    }
    
    /* Connect to server */
    if (connect(ctx->socket_fd, (struct sockaddr*)&ctx->addr, sizeof(ctx->addr)) < 0) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
        return -1;
    }
    
    return 0;
}

/* Bind and listen (download server) */
static int socket_bind_listen(socket_context_t* ctx)
{
    if (!ctx || ctx->socket_fd != INVALID_SOCKET) {
        return -1;
    }
    LOGI("Socket", "Binding and listening on port %d", ctx->port);
    /* Create socket */
    ctx->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->socket_fd == INVALID_SOCKET) {
        return -1;
    }

    /* Set up server address */
    memset(&ctx->addr, 0, sizeof(ctx->addr));
    ctx->addr.sin_family = AF_INET;
    ctx->addr.sin_port = htons((u_short)ctx->port);
    ctx->addr.sin_addr.s_addr = INADDR_ANY;
    
    /* Allow port reuse */
    int reuse = 1;
    setsockopt(ctx->socket_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));
    LOGI("Socket", "Binding to socket_fd %d", ctx->socket_fd);
    /* Bind */
    if (bind(ctx->socket_fd, (struct sockaddr*)&ctx->addr, sizeof(ctx->addr)) < 0) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
        return -1;
    }
    
    /* Listen */
    if (listen(ctx->socket_fd, 1) < 0) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
        return -1;
    }
    
    return 0;
}

/* Accept connection (download server) */
static int socket_accept(socket_context_t* ctx)
{
    if (!ctx || ctx->socket_fd == INVALID_SOCKET) {
        return -1;
    }
    
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    ctx->socket_fd = accept(ctx->socket_fd, (struct sockaddr*)&client_addr, &client_len);
    if (ctx->socket_fd == INVALID_SOCKET) {
        return -1;
    }
    
    /* Store client info */
    inet_ntop(AF_INET, &client_addr.sin_addr, ctx->peer_ip, sizeof(ctx->peer_ip));
    ctx->port = ntohs(client_addr.sin_port);
    
    return 0;
}

/* Send data through socket */
static int socket_send(socket_context_t* ctx, const char* data, size_t size)
{
    if (!ctx || ctx->socket_fd == INVALID_SOCKET || !data || size == 0) {
        return -1;
    }
    
    ssize_t sent = send(ctx->socket_fd, data, size, 0);
    if (sent < 0) {
        return -1;
    }
    
    return (int)sent;
}

/* Receive data through socket */
static int socket_recv(socket_context_t* ctx, char* buffer, size_t buffer_size, size_t* actual_size)
{
    if (!ctx || ctx->socket_fd == INVALID_SOCKET || !buffer || !actual_size) {
        return -1;
    }
    
    ssize_t received = recv(ctx->socket_fd, buffer, buffer_size, 0);
    if (received < 0) {
        return -1;
    }
    
    *actual_size = (size_t)received;
    return 0;
}

/* Send complete message with length prefix */
static int socket_send_message(socket_context_t* ctx, const char* data, size_t size)
{
    if (!ctx || !data) {
        return -1;
    }
    
    /* Send length (4 bytes) in network byte order */
    uint32_t host_length = (uint32_t)size;
    uint32_t network_length = htonl(host_length);
    LOGI("Socket", "Sending message length: %u (host: %u, network: %u, hex: 0x%08X)", size, host_length, network_length, network_length);
    if (socket_send(ctx, (char*)&network_length, sizeof(network_length)) != sizeof(network_length)) {
        return -1;
    }
    
    /* Send data (only if size > 0) */
    if (size > 0) {
        size_t total_sent = 0;
        while (total_sent < size) {
            int sent = socket_send(ctx, data + total_sent, size - total_sent);
            if (sent < 0) {
                return -1;
            }
            total_sent += sent;
        }
        return (int)total_sent;
    }
    
    return 0;  /* Successfully sent zero-length message */
}

/* Receive complete message with length prefix */
static int socket_recv_message(socket_context_t* ctx, char* buffer, size_t buffer_size, size_t* actual_size)
{
    if (!ctx || !buffer || !actual_size) {
        return -1;
    }
    
    /* Receive length in network byte order */
    uint32_t network_length;
    size_t header_received = 0;
    if (socket_recv(ctx, (char*)&network_length, sizeof(network_length), &header_received) != 0 || header_received != sizeof(network_length)) {
        LOGE("Socket", "Failed to receive message header. Expected %zu bytes, got %zu", sizeof(network_length), header_received);
        return -1;
    }
    
    /* Log raw bytes for debugging */
    unsigned char* raw_bytes = (unsigned char*)&network_length;
    LOGI("Socket", "Raw header bytes: %02X %02X %02X %02X", raw_bytes[0], raw_bytes[1], raw_bytes[2], raw_bytes[3]);
    
    /* Convert from network byte order to host byte order */
    uint32_t length = ntohl(network_length);
    LOGI("Socket", "Received message length: %u (network: %u, hex: 0x%08X)", length, network_length, network_length);
    
    /* Validate length */
    if (length > buffer_size || length > 1024 * 1024 * 1024) {  /* Max 1GB */
        LOGE("Socket", "Invalid length: %u", length);
        return -1;
    }
    
    /* Receive data */
    size_t total_received = 0;
    while (total_received < (size_t)length) {
        size_t received;
        if (socket_recv(ctx, buffer + total_received, buffer_size - total_received, &received) != 0 || received == 0) {
            LOGI("Socket", "Error receiving data: %s", strerror(errno));
          return -1;
        }
        total_received += received;
    }
    
    *actual_size = total_received;
    return 0;
}

/* Send file header */
static int socket_send_header(socket_context_t* ctx)
{
    /* Build JSON header */
    char filename[256];
    char folder[128];
    
    /* Extract filename from path */
    const char* last_slash = strrchr(ctx->file_path, '/');
    if (!last_slash) {
        last_slash = strrchr(ctx->file_path, '\\');
    }
    const char* fname = last_slash ? last_slash + 1 : ctx->file_path;
    
    strncpy(filename, fname, sizeof(filename) - 1);
    strncpy(folder, ctx->folder, sizeof(folder) - 1);
    
    /* Create JSON header */
    char header[1024];
    int len = snprintf(header, sizeof(header),
        "{\"name\":\"%s\",\"folder\":\"%s\",\"size\":%lld}",
                       ctx->file_name, folder, ctx->file_size);
    
    if (len < 0 || len >= (int)sizeof(header)) {
        return -1;
    }
    LOGI("Socket", "Sending header: %s", header);
    /* Send header with length prefix */
    return socket_send_message(ctx, header, (size_t)len);
}

/* receiving header */
static int socket_recv_header(socket_context_t* ctx)
{
    char header[1024];
    size_t received;
    
    if (socket_recv_message(ctx, header, sizeof(header), &received) != 0 || received == 0) {
        return -1;
    }
    
    /* Parse JSON (simplified) */
    /* TODO: Implement proper JSON parsing */
    /* For now, extract basic fields manually */
    LOGI("Socket", "Received header: %s", header);
    /* Extract filename */
    const char* name_key = "\"name\"";
    const char* name_start = strstr(header, name_key);
    if (name_start) {
        name_start = strchr(name_start + strlen(name_key), '\"');
        if (name_start) {
            name_start++;
            const char* name_end = strchr(name_start, '\"');
            if (name_end) {
                size_t name_len = (size_t)(name_end - name_start);
                if (name_len < sizeof(ctx->file_path)) {
                    strncpy(ctx->file_path, name_start, name_len);
                    ctx->file_path[name_len] = '\0';
                }
            }
        }
    }
    
    /* Extract size */
    const char* size_key = "\"size\"";
    const char* size_start = strstr(header, size_key);
    if (size_start) {
        size_start = strchr(size_start + strlen(size_key), ':');
        if (size_start) {
            ctx->file_size = atoll(size_start + 1);
        }
    }
    
    return 0;
}

/* Start upload */
int lanshare_upload_start(lanshare_upload_handle_t handle,
                           lanshare_transfer_callback_t callback,
                           void* user_data)
{
    if (!handle) {
        return -1;
    }
    
    socket_context_t* ctx = (socket_context_t*)handle;

    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->state = TRANSFER_STATE_WAITING;
    
    /* Open file - either from path or file descriptor */
    if (ctx->file_descriptor >= 0) {
        /* Use file descriptor */
        ctx->file_handle = lanshare_file_open_from_fd(ctx->file_descriptor);
        LOGI("Socket", "Opened file from fd: %d", ctx->file_descriptor);
        /* Note: We don't close the original fd here, as it was duplicated in JNI */
    } else {
        /* Use file path */
        ctx->file_handle = lanshare_file_open_read(ctx->file_path);
    }
    
    if (!ctx->file_handle) {
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    ctx->file_size = lanshare_file_get_size(ctx->file_handle);
    
    /* Connect to peer */
    if (socket_connect(ctx) != 0) {
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    ctx->state = TRANSFER_STATE_TRANSFERRING;
    LOGI("Socket", "Transfer started");
    /* Send header */
    if (socket_send_header(ctx) < 0) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
        ctx->state = TRANSFER_STATE_ERROR;
        return -1;
    }
    
    ctx->header_sent = 1;
    ctx->bytes_to_send = ctx->file_size;

    /* Send data in chunks */
    while (ctx->bytes_to_send > 0 && ctx->state == TRANSFER_STATE_TRANSFERRING) {
        size_t chunk_size = ctx->buffer_size;
        LOGI("Socket", "Sending chunk of size %zu", chunk_size);
        if ((long long)chunk_size > ctx->bytes_to_send) {
            chunk_size = (size_t)ctx->bytes_to_send;
        }
        
        size_t bytes_read = lanshare_file_read(ctx->file_handle, ctx->buffer, chunk_size);
        if (bytes_read == 0) {
            break;
        }
        
        int sent = socket_send_message(ctx, ctx->buffer, bytes_read);
        if (sent < 0) {
            ctx->state = TRANSFER_STATE_ERROR;
            break;
        }
        
        ctx->bytes_transferred += bytes_read;
        ctx->bytes_to_send -= bytes_read;
        
        /* Update progress */
        if (ctx->callback) {
            lanshare_transfer_info_t info;
            memset(&info, 0, sizeof(info));
            strncpy(info.file_path, ctx->file_path, sizeof(info.file_path) - 1);
            strncpy(info.peer_id, ctx->peer_ip, sizeof(info.peer_id) - 1);
            info.state = ctx->state;
            info.type = ctx->type;
            info.data_size = ctx->file_size;
            info.bytes_transferred = ctx->bytes_transferred;
            info.progress = (int)((ctx->bytes_transferred * 100) / ctx->file_size);
            ctx->callback(&info, ctx->user_data);
        }
    }
    
    /* Send finish message */
    socket_send_message(ctx, "", 0);
    LOGI("Socket", "Transfer finished");
    /* Cleanup */
    closesocket(ctx->socket_fd);
    ctx->socket_fd = INVALID_SOCKET;
    lanshare_file_close(ctx->file_handle);
    ctx->file_handle = NULL;
    
    if (ctx->state == TRANSFER_STATE_TRANSFERRING) {
        ctx->state = TRANSFER_STATE_FINISHED;
    }
    
    return (ctx->state == TRANSFER_STATE_FINISHED) ? 0 : -1;
}

/* Start download (client) */
int lanshare_download_start(lanshare_download_handle_t handle)
{

}

/* Start download server */
int lanshare_download_server_start(int port,
                                    const char* save_dir,
                                    lanshare_transfer_callback_t callback,
                                    void* user_data)
{
    socket_context_t* ctx = socket_download_create(port);
    if (!ctx) {
        return -1;
    }
    
    /* Set save directory if provided */
    if (save_dir && save_dir[0] != '\0') {
        strncpy(ctx->save_dir, save_dir, sizeof(ctx->save_dir) - 1);
        LOGI("Socket", "Download server will save to: %s", save_dir);
    } else {
        ctx->save_dir[0] = '\0';
        LOGI("Socket", "Download server using default save location");
    }
    
    if (socket_bind_listen(ctx) != 0) {
        free(ctx);
        return -1;
    }
    
    /* Accept connection (blocking) */
    if (socket_accept(ctx) != 0) {
        closesocket(ctx->socket_fd);
        free(ctx);
        return -1;
    }
    LOGI("Socket", "download Connection accepted");
    /* Receive header */
    if (socket_recv_header(ctx) != 0) {
        closesocket(ctx->socket_fd);
        free(ctx);
        return -1;
    }
    LOGI("Socket", "download Header received %s", ctx->file_path);
    
    /* Build full file path with save directory */
    char full_path[1024];
    if (ctx->save_dir[0] != '\0') {
        /* Combine save_dir with filename */
        const char* filename = ctx->file_path;
        // Extract just the filename from the path
        const char* last_slash = strrchr(filename, '/');
        if (!last_slash) {
            last_slash = strrchr(filename, '\\');
        }
        if (last_slash) {
            filename = last_slash + 1;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", ctx->save_dir, filename);
        LOGI("Socket", "Saving file to: %s", full_path);
    } else {
        /* Use original file path */
        strncpy(full_path, ctx->file_path, sizeof(full_path) - 1);
    }
    
    /* Create download file */
    ctx->file_handle = lanshare_file_open_write(full_path);
    if (!ctx->file_handle) {
        LOGE("Socket", "Failed to create file: %s", full_path);
        closesocket(ctx->socket_fd);
        free(ctx);
        return -1;
    }
    LOGI("Socket", "download File created");
    ctx->state = TRANSFER_STATE_TRANSFERRING;
    
    /* Receive data */
    while (ctx->state == TRANSFER_STATE_TRANSFERRING) {
        size_t received;
        int ret = socket_recv_message(ctx, ctx->buffer, ctx->buffer_size, &received);
        LOGI("Socket", "download Data received: %zu bytes", received);
        if (ret != 0 || received == 0) {
            /* End of transfer */
            break;
        }
        
        size_t written = lanshare_file_write(ctx->file_handle, ctx->buffer, received);
        if (written != received) {
            LOGE("Socket", "Failed to write file: %s", full_path);
            ctx->state = TRANSFER_STATE_ERROR;
            break;
        }
        
        ctx->bytes_transferred += received;
        
        /* Update progress */
        if (callback) {
            lanshare_transfer_info_t info;
            memset(&info, 0, sizeof(info));
            strncpy(info.file_path, full_path, sizeof(info.file_path) - 1);
            info.state = ctx->state;
            info.type = TRANSFER_TYPE_DOWNLOAD;
            info.data_size = ctx->file_size;
            info.bytes_transferred = ctx->bytes_transferred;
            info.progress = (int)((ctx->bytes_transferred * 100) / ctx->file_size);
            callback(&info, user_data);
        }
    }
    LOGI("Socket", "download Transfer finished");
    /* Cleanup */
    closesocket(ctx->socket_fd);
    ctx->socket_fd = INVALID_SOCKET;
    lanshare_file_close(ctx->file_handle);
    ctx->file_handle = NULL;
    
    free(ctx);
    
    return (ctx->state == TRANSFER_STATE_FINISHED) ? 0 : -1;
}

/* Stop download server */
int lanshare_download_server_stop(void)
{
    /* TODO: Implement server shutdown */
    return -1;
}

/* Pause transfer */
int lanshare_upload_pause(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    
    socket_context_t* ctx = (socket_context_t*)handle;
    ctx->state = TRANSFER_STATE_PAUSED;
    
    return 0;
}

/* Resume transfer */
int lanshare_upload_resume(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    
    socket_context_t* ctx = (socket_context_t*)handle;
    
    if (ctx->state == TRANSFER_STATE_PAUSED) {
        ctx->state = TRANSFER_STATE_TRANSFERRING;
        /* Resume logic would go here */
    }
    
    return 0;
}

/* Cancel transfer */
int lanshare_upload_cancel(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return -1;
    }
    
    socket_context_t* ctx = (socket_context_t*)handle;
    
    /* Send cancel message */
    if (ctx->socket_fd != INVALID_SOCKET) {
        char cancel_msg[] = "{\"type\":\"cancel\"}";
        socket_send_message(ctx, cancel_msg, strlen(cancel_msg));
    }
    
    ctx->state = TRANSFER_STATE_CANCELLED;
    
    /* Cleanup */
    if (ctx->socket_fd != INVALID_SOCKET) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
    }
    
    if (ctx->file_handle) {
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
    }
    
    return 0;
}

/* Destroy upload handle */
void lanshare_upload_destroy(lanshare_upload_handle_t handle)
{
    if (!handle) {
        return;
    }
    
    socket_context_t* ctx = (socket_context_t*)handle;
    
    /* Cleanup */
    if (ctx->socket_fd != INVALID_SOCKET) {
        closesocket(ctx->socket_fd);
        ctx->socket_fd = INVALID_SOCKET;
    }
    
    if (ctx->file_handle) {
        lanshare_file_close(ctx->file_handle);
        ctx->file_handle = NULL;
    }
    
    /* Close file descriptor if it was stored (for fd-based uploads) */
    if (ctx->file_descriptor >= 0) {
        close(ctx->file_descriptor);
        ctx->file_descriptor = -1;
        LOGI("Socket", "Closed file descriptor: %d", ctx->file_descriptor);
    }
    
    socket_library_cleanup();
    free(ctx);
}

/* Destroy download handle */
void lanshare_download_destroy(lanshare_download_handle_t handle)
{
    lanshare_upload_destroy((lanshare_upload_handle_t)handle);
}

/* Get transfer info */
int lanshare_get_transfer_info(void* handle, int is_upload, lanshare_transfer_info_t* info)
{
    if (!handle || !info) {
        return -1;
    }
    
    socket_context_t* ctx = (socket_context_t*)handle;
    
    memset(info, 0, sizeof(lanshare_transfer_info_t));
    
    /* Use file_name for fd-based uploads, otherwise use file_path */
    const char* path_to_use = (ctx->file_descriptor >= 0 && ctx->file_name[0] != '\0') 
                               ? ctx->file_name 
                               : ctx->file_path;
    strncpy(info->file_path, path_to_use, sizeof(info->file_path) - 1);
    strncpy(info->peer_id, ctx->peer_ip, sizeof(info->peer_id) - 1);
    info->state = ctx->state;
    info->type = ctx->type;
    info->progress = (int)((ctx->bytes_transferred * 100) / (ctx->file_size > 0 ? ctx->file_size : 1));
    info->data_size = ctx->file_size;
    info->bytes_transferred = ctx->bytes_transferred;
    
    return 0;
}

/* Version string */
const char* lanshare_transfer_version(void)
{
    return "1.0.0-socket";
}

/* Download pause (same as upload) */
int lanshare_download_pause(lanshare_download_handle_t handle)
{
    return lanshare_upload_pause((lanshare_upload_handle_t)handle);
}

/* Download resume (same as upload) */
int lanshare_download_resume(lanshare_download_handle_t handle)
{
    return lanshare_upload_resume((lanshare_upload_handle_t)handle);
}

/* Download cancel (same as upload) */
int lanshare_download_cancel(lanshare_download_handle_t handle)
{
    return lanshare_upload_cancel((lanshare_upload_handle_t)handle);
}
