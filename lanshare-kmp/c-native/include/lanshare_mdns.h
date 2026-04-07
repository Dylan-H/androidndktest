/*
    LAN-Share mDNS Device Discovery Interface
    Pure C interface for mDNS-based device discovery
    
    Copyright (C) 2026 LAN-Share Project
*/

#ifndef LANSHARE_MDNS_H
#define LANSHARE_MDNS_H

#include "lanshare_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callback type for device discovery */
typedef void (*lanshare_device_callback_t)(const lanshare_device_t* device, void* user_data);

/* Context handle for mDNS operations */
typedef void* lanshare_mdns_handle_t;

/**
 * Initialize mDNS discovery module
 * @param service_type Service type (e.g., "_lanshare._tcp")
 * @param port Service port
 * @return mDNS context handle, NULL on failure
 */
lanshare_mdns_handle_t lanshare_mdns_init();

/**
 * Start broadcasting device information
 * @param handle mDNS context handle
 * @param device Device information to broadcast
 * @return 0 on success, -1 on failure
 */
int lanshare_mdns_start_broadcaster(lanshare_mdns_handle_t handle, const lanshare_device_t* device, const char* service_type, int port);

/**
 * Stop broadcasting device information
 * @param handle mDNS context handle
 */
void lanshare_mdns_stop_broadcaster(lanshare_mdns_handle_t handle);

/**
 * Start discovering other devices
 * @param handle mDNS context handle
 * @param callback Callback function to call when device found
 * @param user_data User data passed to callback
 * @return 0 on success, -1 on failure
 */
int lanshare_mdns_start_discoverer(lanshare_mdns_handle_t handle, 
                                    lanshare_device_callback_t callback, 
                                    void* user_data);

/**
 * Stop discovering other devices
 * @param handle mDNS context handle
 */
void lanshare_mdns_stop_discoverer(lanshare_mdns_handle_t handle);

/**
 * Cleanup mDNS resources
 * @param handle mDNS context handle
 */
void lanshare_mdns_cleanup(lanshare_mdns_handle_t handle);

/**
 * Get mDNS module version
 * @return Version string
 */
const char* lanshare_mdns_version(void);

/**
 * Get discovered devices as JSON string (Android-specific)
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return Pointer to buffer, or NULL on error
 */
const char* lanshare_mdns_get_discovered_devices(char* buffer, size_t buffer_size);

/**
 * Add a discovered device (Android-specific, called from Java)
 * @param device Device information
 * @return 0 on success, -1 on error
 */
int lanshare_mdns_add_device(const lanshare_device_t* device);

/**
 * Clear discovered devices (Android-specific)
 */
void lanshare_mdns_clear_devices(void);

/**
 * Get number of discovered devices (Android-specific)
 * @return Number of discovered devices
 */
int lanshare_mdns_get_device_count(void);

/**
 * Get discovered device by index (Android-specific)
 * @param index Device index
 * @return Device info, or NULL if index out of range
 */
const lanshare_device_t* lanshare_mdns_get_device(int index);

/**
 * Process mDNS responses (Android-specific)
 * Should be called periodically to process incoming mDNS packets
 * @return 0 on success, -1 on error
 */
int lanshare_mdns_process_responses(void);

/**
 * Get discovered devices as JSON string (internal implementation)
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return Pointer to buffer, or NULL on error
 */
const char* lanshare_mdns_get_discovered_devices_impl(char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* LANSHARE_MDNS_H */

