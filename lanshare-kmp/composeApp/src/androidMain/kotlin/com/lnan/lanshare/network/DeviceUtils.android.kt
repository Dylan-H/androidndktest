package com.lnan.lanshare.network

import android.content.Context
import android.net.wifi.WifiManager
import android.os.Build
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext

/**
 * Get the local IP address of the device
 * @return IP address string or null if failed
 */
@Composable
fun getLocalIpAddress(): String? {
    val context = LocalContext.current
    
    return try {
        val wifiManager = context.getSystemService(Context.WIFI_SERVICE) as WifiManager
        val wifiInfo = wifiManager.connectionInfo
        
        // Get IP address from WifiInfo
        val ipAddress = wifiInfo.ipAddress
        if (ipAddress != 0) {
            // Convert integer IP to string
            String.format(
                "%d.%d.%d.%d",
                ipAddress and 0xff,
                ipAddress shr 8 and 0xff,
                ipAddress shr 16 and 0xff,
                ipAddress shr 24 and 0xff
            )
        } else {
            null
        }
    } catch (e: Exception) {
        e.printStackTrace()
        null
    }
}

/**
 * Get the local device name
 */
@Composable
fun getLocalDeviceName(): String {
    return Build.MODEL
}

/**
 * Acquire Wi-Fi multicast lock for mDNS
 */
fun acquireWifiMulticastLock(context: Context): Any? {
    return try {
        val wifiManager = context.getSystemService(Context.WIFI_SERVICE) as WifiManager
        wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "LANShare mDNS")
    } catch (e: Exception) {
        e.printStackTrace()
        null
    }
}
