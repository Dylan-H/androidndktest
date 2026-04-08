package com.lnan.lanshare.network

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import com.lnan.lanshare.MainActivity
import com.lnan.lanshare.ndk.NativeLibrary
import com.lnan.lanshare.model.Device
import com.lnan.lanshare.model.TransferInfo
import com.lnan.lanshare.ndk.DeviceDiscoveryCallback as NativeDeviceDiscoveryCallback
import com.lnan.lanshare.ndk.TransferProgressCallback as NativeTransferProgressCallback

actual fun loadLibrary(): Boolean {
    return try {
        System.loadLibrary("lanshare_native")
        true
    } catch (e: UnsatisfiedLinkError) {
        false
    }
}

actual fun startDiscoverer(serviceType: String, port: Int): Boolean {
    return NativeLibrary.startDiscoverer(serviceType, port)
}

actual fun stopDiscoverer(): Boolean {
    return NativeLibrary.stopDiscoverer()
}

actual fun getDiscoveredDevices(): List<Device> {
    val json = NativeLibrary.getDiscoveredDevices()
    return parseDevicesJson(json)
}

actual fun registerDeviceCallback(callback: DeviceDiscoveryCallback?) {
    if (callback != null) {
        // Wrap the common callback in a NativeLibrary callback
        val nativeCallback = object : NativeDeviceDiscoveryCallback {
            override fun onDeviceFound(device: Device) {
                callback.onDeviceFound(device)
            }
        }
        NativeLibrary.registerDeviceCallback(nativeCallback)
    } else {
        NativeLibrary.registerDeviceCallback(null)
    }
}

actual fun unregisterDeviceCallback() {
    NativeLibrary.unregisterDeviceCallback()
}

actual fun registerTransferProgressCallback(callback: com.lnan.lanshare.network.TransferProgressCallback?) {
    if (callback != null) {
        val nativeCallback = object : NativeTransferProgressCallback {
            override fun onTransferProgress(info: TransferInfo) {
                callback.onTransferProgress(info)
            }
        }
        NativeLibrary.registerTransferProgressCallback(nativeCallback)
    } else {
        NativeLibrary.registerTransferProgressCallback(null)
    }
}

actual fun unregisterTransferProgressCallback() {
    NativeLibrary.unregisterTransferProgressCallback()
}

private fun parseDevicesJson(json: String): List<Device> {
    val devices = mutableListOf<Device>()
    
    // Simple JSON parsing for device list
    // Format: [{"id":"xxx","name":"xxx","os":"xxx","ip":"xxx"},...]
    val trimmed = json.trim()
    if (trimmed.isEmpty() || trimmed == "[]") {
        return emptyList()
    }
    
    // Remove outer brackets
    val content = trimmed.substring(1, trimmed.length - 1)
    if (content.isEmpty()) {
        return emptyList()
    }
    
    // Split by },{ to get individual device objects
    val deviceStrings = content.split("},").map { 
        if (it.endsWith("}")) it else it + "}" 
    }
    
    for (deviceStr in deviceStrings) {
        val device = parseSingleDevice(deviceStr)
        if (device != null) {
            devices.add(device)
        }
    }
    
    return devices
}

private fun parseSingleDevice(json: String): Device? {
    val id = extractValue(json, "\"id\"")
    val name = extractValue(json, "\"name\"")
    val os = extractValue(json, "\"os\"")
    val ip = extractValue(json, "\"ip\"")
    
    if (id == null || name == null || os == null || ip == null) {
        return null
    }
    
    return Device(id, name, os, ip)
}

private fun extractValue(json: String, key: String): String? {
    val startIndex = json.indexOf(key)
    if (startIndex == -1) return null
    
    val colonIndex = json.indexOf(":", startIndex)
    if (colonIndex == -1) return null
    
    var valueStart = json.indexOf("\"", colonIndex)
    if (valueStart == -1) return null
    
    valueStart++ // Skip the opening quote
    val valueEnd = json.indexOf("\"", valueStart)
    if (valueEnd == -1) return null
    
    return json.substring(valueStart, valueEnd)
}

actual fun startBroadcaster(id: String, name: String, osName: String, ipAddress: String, port: Int): Boolean {
    return NativeLibrary.startBroadcaster(id, name, osName, ipAddress, port)
}

actual fun stopBroadcaster() {
    NativeLibrary.stopBroadcaster()
}

actual fun uploadCreate(peerIp: String, port: Int, filePath: String, folder: String?): Long {
    return NativeLibrary.uploadCreate(peerIp, port, filePath, folder)
}

actual fun uploadCreateWithFd(peerIp: String, port: Int, fileDescriptor: Int, fileName: String, folder: String?): Long {
    return NativeLibrary.uploadCreateWithFd(peerIp, port, fileDescriptor, fileName, folder)
}

actual fun uploadStart(handle: Long): Int {
    return NativeLibrary.uploadStart(handle)
}

actual fun uploadPause(handle: Long): Int {
    return NativeLibrary.uploadPause(handle)
}

actual fun uploadResume(handle: Long): Int {
    return NativeLibrary.uploadResume(handle)
}

actual fun uploadCancel(handle: Long): Boolean {
    return NativeLibrary.uploadCancel(handle)
}

actual fun uploadDestroy(handle: Long) {
    NativeLibrary.uploadDestroy(handle)
}

actual fun uploadGetStatus(handle: Long): Int {
    return NativeLibrary.uploadGetStatus(handle)
}

actual fun downloadServerStart(port: Int, saveDir: String?): Int {
    // If saveDir is not provided, use Android's external files directory (no permission needed)
    val actualSaveDir = saveDir ?: getDefaultDownloadDirectory()
    println("Starting download server on port $port, save directory: $actualSaveDir")
    return NativeLibrary.downloadServerStart(port, actualSaveDir)
}

/**
 * Get default download directory for Android
 * Uses external files directory which doesn't require storage permissions
 */
private fun getDefaultDownloadDirectory(): String {
    return try {
        val context = getApplicationContext()
        // Use external files directory - no permission required for Android 10+
        // Path: /storage/emulated/0/Android/data/<package_name>/files/Downloads
        val downloadsDir = context.getExternalFilesDir("Downloads")
        
        if (downloadsDir != null) {
            val path = downloadsDir.absolutePath
            println("Using download directory: $path")
            path
        } else {
            // Fallback to cache directory
            val cacheDir = context.cacheDir.absolutePath
            println("External files dir not available, using cache: $cacheDir")
            cacheDir
        }
    } catch (e: Exception) {
        println("Failed to get download directory: ${e.message}")
        // Last resort fallback
        "/data/data/${getApplicationContext().packageName}/cache"
    }
}

actual fun downloadServerStop(): Int {
    return NativeLibrary.downloadServerStop()
}

actual fun setTransferDecision(accept: Boolean) {
    NativeLibrary.setTransferDecision(accept)
}

actual fun getPendingTransfer(): String {
    return NativeLibrary.getPendingTransfer()
}

actual fun getLocalDeviceInfo(): Device? {
    return try {
        // Get device info directly from Android system APIs
        val deviceId = generateDeviceId()
        val deviceName = Build.MODEL
        val osName = "Android ${Build.VERSION.RELEASE}"
        val ipAddress = getLocalIpAddress2()
        
        Device(
            id = deviceId,
            name = deviceName,
            osName = osName,
            ipAddress = ipAddress
        )
    } catch (e: Exception) {
        println("Failed to get device info: ${e.message}")
        // Return fallback device info
        Device(
            id = "android-${System.currentTimeMillis()}",
            name = Build.MODEL,
            osName = "Android ${Build.VERSION.RELEASE}",
            ipAddress = "127.0.0.1"
        )
    }
}

/**
 * Generate a unique device ID using Android ID
 */
private fun generateDeviceId(): String {
    return try {
        val context = getApplicationContext()
        val androidId = android.provider.Settings.Secure.getString(
            context.contentResolver,
            android.provider.Settings.Secure.ANDROID_ID
        )
        "android-${androidId ?: System.currentTimeMillis()}"
    } catch (e: Exception) {
        "android-${System.currentTimeMillis()}"
    }
}

/**
 * Get local IP address from WiFi connection
 */
private fun getLocalIpAddress2(): String {
    return try {
        val context = getApplicationContext()
        val wifiManager = context.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val wifiInfo = wifiManager.connectionInfo
        val ipInt = wifiInfo.ipAddress
        
        if (ipInt != 0) {
            // Convert integer IP address to string format
            @Suppress("DEPRECATION")
            val ip = intIpToString(ipInt)
            if (!ip.isNullOrBlank()) ip else "127.0.0.1"
        } else {
            "127.0.0.1"
        }
    } catch (e: Exception) {
        println("Failed to get IP address: ${e.message}")
        "127.0.0.1"
    }
}

/**
 * Convert integer IP to string format
 */
private fun intIpToString(ip: Int): String? {
    return try {
        "${ip and 0xFF}.${(ip shr 8) and 0xFF}.${(ip shr 16) and 0xFF}.${(ip shr 24) and 0xFF}"
    } catch (e: Exception) {
        null
    }
}

/**
 * Get application context
 */
private fun getApplicationContext(): Context {
    return MainActivity.appContext
        ?: throw IllegalStateException("Application context not available")
}
