package com.lnan.lanshare.network

import com.lnan.lanshare.model.Device
import com.lnan.lanshare.model.TransferInfo

/**
 * Native library interface for LAN-Share protocol
 * This provides bindings to the C native library (lanshare)
 */

/**
 * Device discovery callback interface
 * Called when a new device is discovered
 */
interface DeviceDiscoveryCallback {
    fun onDeviceFound(device: Device)
}

/**
 * Load the native library
 * Must be called before using any native functions
 */
expect fun loadLibrary(): Boolean

/**
 * Start device discovery
 * @param serviceType mDNS service type (default: "_lanshare._tcp")
 * @param port Service port (default: 17116)
 * @return true if discovery started successfully
 */
expect fun startDiscoverer(serviceType: String = "_lanshare._tcp", port: Int = 17116): Boolean

/**
 * Stop device discovery
 * @return true if stopped successfully
 */
expect fun stopDiscoverer(): Boolean
expect fun getDiscoveredDevices(): List<Device>

/**
 * Register device discovery callback
 * @param callback Callback to be invoked when a device is discovered
 */
expect fun registerDeviceCallback(callback: DeviceDiscoveryCallback?)

/**
 * Unregister device discovery callback
 */
expect fun unregisterDeviceCallback()

/**
 * Transfer progress callback interface
 */
interface TransferProgressCallback {
    fun onTransferProgress(info: com.lnan.lanshare.model.TransferInfo)
}

/**
 * Register transfer progress callback
 * @param callback Callback to be invoked when transfer progress updates
 */
expect fun registerTransferProgressCallback(callback: TransferProgressCallback?)

/**
 * Unregister transfer progress callback
 */
expect fun unregisterTransferProgressCallback()
/**
 * Broadcast own device information
 * @param id Device ID
 * @param name Device name
 * @param osName Operating system name
 * @param ipAddress IP address
 * @param port Service port (default: 17116)
 * @return true if broadcasting started successfully
 */

/**
 * Broadcast own device information
 * @param id Device ID
 * @param name Device name
 * @param osName Operating system name
 * @param ipAddress IP address
 * @param port Service port (default: 17116)
 * @return true if broadcasting started successfully
 */
expect fun startBroadcaster(
    id: String,
    name: String,
    osName: String,
    ipAddress: String,
    port: Int = 17116
): Boolean

/**
 * Stop broadcasting
 */
expect fun stopBroadcaster()

/**
 * Upload file to peer
 * @param peerIp Peer IP address
 * @param port Peer port
 * @param filePath Local file path to upload
 * @param folder Folder name on peer (null for default)
 * @return Upload handle (non-zero on success)
 */
expect fun uploadCreate(
    peerIp: String,
    port: Int,
    filePath: String,
    folder: String? = null
): Long

/**
 * Create upload task using file descriptor (Android only)
 * @param peerIp Peer IP address
 * @param port Peer port
 * @param fileDescriptor Android file descriptor
 * @param fileName Name of the file
 * @param folder Folder name on peer (null for default)
 * @return Upload handle (non-zero on success)
 */
expect fun uploadCreateWithFd(
    peerIp: String,
    port: Int,
    fileDescriptor: Int,
    fileName: String,
    folder: String? = null
): Long

/**
 * Start upload
 * @param handle Upload handle from uploadCreate()
 * @return 0 on success
 */
expect fun uploadStart(handle: Long): Int

/**
 * Pause upload
 * @param handle Upload handle
 * @return 0 on success
 */
expect fun uploadPause(handle: Long): Int

/**
 * Resume upload
 * @param handle Upload handle
 * @return 0 on success
 */
expect fun uploadResume(handle: Long): Int

/**
 * Cancel upload
 * @param handle Upload handle
 * @return true if cancelled successfully
 */
expect fun uploadCancel(handle: Long): Boolean

/**
 * Destroy upload handle
 * @param handle Upload handle
 */
expect fun uploadDestroy(handle: Long)

/**
 * Get upload status
 * @param handle Upload handle
 * @return Status code
 */
expect fun uploadGetStatus(handle: Long): Int

/**
 * Start download server
 * @param port Port to listen on
 * @param saveDir Directory to save downloaded files (null for default)
 * @return 0 on success
 */
expect fun downloadServerStart(port: Int = 17116, saveDir: String? = null): Int

/**
 * Stop download server
 * @return 0 on success
 */
expect fun downloadServerStop(): Int

/**
 * Set transfer decision for pending incoming transfer
 * @param accept true to accept, false to reject
 */
expect fun setTransferDecision(accept: Boolean)

/**
 * Get pending transfer info
 * @return JSON string with filename and size, or empty object if no pending transfer
 */
expect fun getPendingTransfer(): String

/**
 * Start download
 * @param handle Download handle
 * @return 0 on success
 */
//expect fun downloadStart(handle: Long): Int
//
///**
// * Pause download
// * @param handle Download handle
// * @return 0 on success
// */
//expect fun downloadPause(handle: Long): Int
//
///**
// * Resume download
// * @param handle Download handle
// * @return 0 on success
// */
//expect fun downloadResume(handle: Long): Int
//
///**
// * Cancel download
// * @param handle Download handle
// * @return true if cancelled successfully
// */
//expect fun downloadCancel(handle: Long): Boolean
//
///**
// * Destroy download handle
// * @param handle Download handle
// */
//expect fun downloadDestroy(handle: Long)
//
///**
// * Get download status
// * @param handle Download handle
// * @return Status code
// */
//expect fun downloadGetStatus(handle: Long): Int

/**
 * Get current transfer information
 * @param handle Transfer handle
 * @param isUpload true for upload, false for download
 * @return TransferInfo object or null on error
 */
//expect fun getTransferInfo(handle: Long, isUpload: Boolean = true): TransferInfo?

/**
 * Get current device information
 * @return Device object with current device info, or null on error
 */
expect fun getLocalDeviceInfo(): Device?
