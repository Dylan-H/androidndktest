package com.lnan.lanshare.ndk

import com.lnan.lanshare.model.Device

/**
 * 设备发现回调接口
 */
interface DeviceDiscoveryCallback {
    /**
     * 当发现新设备时调用
     * @param device 发现的设备信息
     */
    fun onDeviceFound(device: Device)
}

/**
 * 传输进度回调接口
 */
interface TransferProgressCallback {
    /**
     * 当传输进度更新时调用
     * @param info 传输信息
     */
    fun onTransferProgress(info: com.lnan.lanshare.model.TransferInfo)
}

object NativeLibrary {
    init {
        System.loadLibrary("lanshare_native")
    }

    // mDNS discovery
    external fun startDiscoverer(serviceType: String, port: Int): Boolean
    external fun stopDiscoverer(): Boolean
    external fun getDiscoveredDevices(): String
    external fun startBroadcaster(id: String, name: String, osName: String, ipAddress: String, port: Int): Boolean
    external fun stopBroadcaster()

    // Device discovery callback registration
    external fun registerDeviceCallback(callback: DeviceDiscoveryCallback?)
    external fun unregisterDeviceCallback()
    
    // Transfer progress callback registration
    external fun registerTransferProgressCallback(callback: TransferProgressCallback?)
    external fun unregisterTransferProgressCallback()

    // File transfer - upload
    external fun uploadCreate(peerIp: String, port: Int, filePath: String, folder: String?): Long
    external fun uploadCreateWithFd(peerIp: String, port: Int, fileDescriptor: Int, fileName: String, folder: String?): Long
    external fun uploadStart(handle: Long): Int
    external fun uploadPause(handle: Long): Int
    external fun uploadResume(handle: Long): Int
    external fun uploadCancel(handle: Long): Boolean
    external fun uploadDestroy(handle: Long)
    external fun uploadGetStatus(handle: Long): Int

    // File transfer - download
    external fun downloadServerStart(port: Int, saveDir: String): Int
    external fun downloadServerStop(): Int
    external fun setTransferDecision(accept: Boolean)
    external fun getPendingTransfer(): String

    external fun downloadStart(handle: Long): Int
    external fun downloadPause(handle: Long): Int
    external fun downloadResume(handle: Long): Int
    external fun downloadCancel(handle: Long): Boolean
    external fun downloadDestroy(handle: Long)
    external fun downloadGetStatus(handle: Long): Int

    // Utility
    external fun getVersion(): String
    
    // Device info (暂时返回空，后续添加)
//    external fun getDeviceId(): String
//    external fun getDeviceName(): String
//    external fun getDeviceOs(): String
//    external fun getDeviceIpAddress(): String
}
