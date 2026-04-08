/*
    LAN-Share Data Transfer Interface
    Pure C interface for NNG-based data transfer
    
    Copyright (C) 2026 LAN-Share Project
*/

#ifndef LANSHARE_TRANSFER_H
#define LANSHARE_TRANSFER_H
 
#include "lanshare_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callback type for transfer events */
typedef void (*lanshare_transfer_callback_t)(const lanshare_transfer_info_t* info, void* user_data);

/* Transfer handles */
typedef void* lanshare_upload_handle_t;
typedef void* lanshare_download_handle_t;

/* ========== Upload API ========== */

/**
 * Create upload task
 * @param peer_ip Peer device IP address
 * @param port Peer device port
 * @param local_file_path Local file path to upload
 * @param folder_name Folder name on peer (can be NULL or empty)
 * @return Upload handle, NULL on failure
 */
lanshare_upload_handle_t lanshare_upload_create(const char* peer_ip, int port,
                                                  const char* local_file_path,
                                                  const char* folder_name);

/**
 * Create upload task using file descriptor
 * @param peer_ip Peer device IP address
 * @param port Peer device port
 * @param file_descriptor File descriptor of the file to upload
 * @param file_name Name of the file (for display purposes)
 * @param folder_name Folder name on peer (can be NULL or empty)
 * @return Upload handle, NULL on failure
 */
lanshare_upload_handle_t lanshare_upload_create_with_fd(const char* peer_ip, int port,
                                                          int file_descriptor,
                                                          const char* file_name,
                                                          const char* folder_name);

/**
 * Start upload
 * @param handle Upload handle
 * @param callback Callback function for transfer events
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on failure
 */
int lanshare_upload_start(lanshare_upload_handle_t handle,
                           lanshare_transfer_callback_t callback,
                           void* user_data);

/**
 * Pause upload
 * @param handle Upload handle
 * @return 0 on success, -1 on failure
 */
int lanshare_upload_pause(lanshare_upload_handle_t handle);

/**
 * Resume upload
 * @param handle Upload handle
 * @return 0 on success, -1 on failure
 */
int lanshare_upload_resume(lanshare_upload_handle_t handle);

/**
 * Cancel upload
 * @param handle Upload handle
 * @return 0 on success, -1 on failure
 */
int lanshare_upload_cancel(lanshare_upload_handle_t handle);

/**
 * Destroy upload handle and cleanup resources
 * @param handle Upload handle
 */
void lanshare_upload_destroy(lanshare_upload_handle_t handle);

/* ========== Download API (Server Mode) ========== */

/**
 * Start download server (listen for incoming transfers)
 * @param port Port to listen on
 * @param save_dir Directory to save downloaded files (NULL for default location)
 * @param callback Callback function for transfer events
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on failure
 */
int lanshare_download_server_start(int port,
                                    const char* save_dir,
                                    lanshare_transfer_callback_t callback,
                                    void* user_data);

/**
 * Set user decision for pending transfer
 * @param accept 1 to accept, 0 to reject
 */
void lanshare_set_transfer_decision(int accept);

/**
 * Get pending transfer info
 * @param filename Buffer to store filename
 * @param filename_size Size of filename buffer
 * @param file_size Pointer to store file size
 * @return 0 if there is a pending transfer, -1 otherwise
 */
int lanshare_get_pending_transfer(char* filename, size_t filename_size, uint64_t* file_size);

/**
 * Stop download server
 * @return 0 on success, -1 on failure
 */
int lanshare_download_server_stop(void);



/* ========== Common API ========== */

/**
 * Get transfer information
 * @param handle Transfer handle (upload or download)
 * @param is_upload 1 for upload, 0 for download
 * @param info Pointer to store transfer info
 * @return 0 on success, -1 on failure
 */
int lanshare_get_transfer_info(void* handle, int is_upload, lanshare_transfer_info_t* info);



/* ========== File Operations ========== */

/**
 * Open file for reading
 * @param path File path
 * @return FILE pointer, NULL on failure
 */
FILE* lanshare_file_open_read(const char* path);

/**
 * Open file from file descriptor for reading
 * @param fd File descriptor
 * @return FILE pointer, NULL on failure
 */
FILE* lanshare_file_open_from_fd(int fd);

/**
 * Open file for writing
 * @param path File path
 * @return FILE pointer, NULL on failure
 */
FILE* lanshare_file_open_write(const char* path);

/**
 * Close file
 * @param file FILE pointer
 */
void lanshare_file_close(FILE* file);

/**
 * Read data from file
 * @param file FILE pointer
 * @param buffer Buffer to read into
 * @param size Number of bytes to read
 * @return Number of bytes read
 */
size_t lanshare_file_read(FILE* file, void* buffer, size_t size);

/**
 * Write data to file
 * @param file FILE pointer
 * @param buffer Buffer to write
 * @param size Number of bytes to write
 * @return Number of bytes written
 */
size_t lanshare_file_write(FILE* file, const void* buffer, size_t size);

/**
 * Get file size
 * @param file FILE pointer
 * @return File size in bytes, -1 on failure
 */
long long lanshare_file_get_size(FILE* file);

/**
 * Set file position
 * @param file FILE pointer
 * @param offset Offset from beginning
 * @return 0 on success, -1 on failure
 */
int lanshare_file_set_pos(FILE* file, long long offset);

/**
 * Get current file position
 * @param file FILE pointer
 * @return Current position, -1 on failure
 */
long long lanshare_file_get_pos(FILE* file);

/**
 * Create directories recursively
 * @param path Directory path
 * @return 0 on success, -1 on failure
 */
int lanshare_file_mkdirs(const char* path);

/**
 * Check if file exists
 * @param path File path
 * @return 1 if exists, 0 otherwise
 */
int lanshare_file_exists(const char* path);

/**
 * Get file name from path
 * @param path File path
 * @return File name, or path if no separator found
 */
const char* lanshare_file_get_name(const char* path);

/**
 * Get directory from path
 * @param path File path
 * @param dir Buffer to store directory
 * @param size Buffer size
 * @return 0 on success, -1 on failure
 */
int lanshare_file_get_dir(const char* path, char* dir, size_t size);

/**
 * Get file extension
 * @param path File path
 * @return Extension without dot, or NULL if no extension
 */
const char* lanshare_file_get_ext(const char* path);

/**
 * Generate unique file path if file exists
 * @param dir_path Directory path
 * @param filename Original filename
 * @param out_path Output buffer for unique path
 * @param out_size Output buffer size
 * @return 0 on success, -1 if failed to generate unique path
 */
int lanshare_file_get_unique_path(const char* dir_path, const char* filename,
                                   char* out_path, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* LANSHARE_TRANSFER_H */
