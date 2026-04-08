package com.lnan.lanshare

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import com.lnan.lanshare.model.Device
import com.lnan.lanshare.model.TransferInfo
import com.lnan.lanshare.model.TransferState
import com.lnan.lanshare.model.TransferType
import com.lnan.lanshare.network.DeviceDiscoveryCallback
import com.lnan.lanshare.network.downloadServerStart
import com.lnan.lanshare.network.downloadServerStop
import com.lnan.lanshare.network.getLocalDeviceInfo
import com.lnan.lanshare.network.loadLibrary
import com.lnan.lanshare.network.registerDeviceCallback
import com.lnan.lanshare.network.startBroadcaster
import com.lnan.lanshare.network.startDiscoverer
import com.lnan.lanshare.network.stopBroadcaster
import com.lnan.lanshare.network.unregisterDeviceCallback
import com.lnan.lanshare.network.uploadCancel
import com.lnan.lanshare.network.uploadCreate
import com.lnan.lanshare.network.uploadCreateWithFd
import com.lnan.lanshare.network.uploadDestroy
import com.lnan.lanshare.network.uploadStart
import com.lnan.lanshare.network.setTransferDecision
import com.lnan.lanshare.network.getPendingTransfer
import com.lnan.lanshare.network.registerTransferProgressCallback
import com.lnan.lanshare.network.unregisterTransferProgressCallback
import com.lnan.lanshare.network.TransferProgressCallback
import com.lnan.lanshare.ui.DeviceListScreen
import com.lnan.lanshare.ui.IncomingTransferDialog
import com.lnan.lanshare.ui.SendFileScreen
import com.lnan.lanshare.ui.TransferProgressDialog
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
fun App() {
    MaterialTheme {
        Surface(
            modifier = Modifier.fillMaxSize()
        ) {
            val (currentScreen, setCurrentScreen) = remember { mutableStateOf("deviceList") }
            val (devices, setDevices) = remember { mutableStateOf(emptyList<Device>()) }
            val (selectedDevice, setSelectedDevice) = remember { mutableStateOf<Device?>(null) }
            
            // Discovery state
            val isDiscovering = remember { mutableStateOf(false) }
            val isServiceRunning = remember { mutableStateOf(false) }
            val scope = rememberCoroutineScope()
            
            // Transfer state
            val currentTransfer = remember { mutableStateOf<TransferInfo?>(null) }
            val showTransferDialog = remember { mutableStateOf(false) }
            val uploadHandle = remember { mutableStateOf<Long?>(null) }
            
            // Incoming transfer request state
            val incomingRequest = remember { mutableStateOf<IncomingRequest?>(null) }
            
            // Load library on app launch
            LaunchedEffect(Unit) {
                val loaded = loadLibrary()
                println("Native library loaded: $loaded")
                
                // Register device discovery callback for real-time updates
                registerDeviceCallback(object : DeviceDiscoveryCallback {
                    override fun onDeviceFound(device: Device) {
                        println("Real-time callback: Device found - ${device.name},${device.osName} (${device.ipAddress})")
                        val currentDevices = devices
                        if (currentDevices.none { it.id == device.id }) {
                            setDevices(currentDevices + device)
                        }
                    }
                })
            }
            
            // Cleanup callback when composable is disposed
            DisposableEffect(Unit) {
                onDispose {
                    unregisterDeviceCallback()
                    unregisterTransferProgressCallback()
                }
            }
            
            // Register transfer progress callback
            LaunchedEffect(Unit) {
                registerTransferProgressCallback(object : TransferProgressCallback {
                    override fun onTransferProgress(info: TransferInfo) {
                        println("Transfer progress: ${info.fileName}, state=${info.state}, progress=${info.progress}%, speed=${info.transferSpeed} B/s, isSender=${info.isSender}")
                        scope.launch(Dispatchers.Main) {
                            currentTransfer.value = info
                            // Only show TransferProgressDialog for sender with PENDING_CONFIRM state
                            // Receiver should show IncomingTransferDialog via polling mechanism
                            if (info.state == TransferState.PENDING_CONFIRM && !info.isSender) {
                                // Don't show TransferProgressDialog for receiver in PENDING_CONFIRM state
                                // The IncomingTransferDialog will be shown via polling in lines 158-181
                                showTransferDialog.value = false
                            } else {
                                // Show dialog for other states and for sender
                                showTransferDialog.value = true
                            }
                        }
                    }
                })
            }
            
            // Monitor transfer progress
            LaunchedEffect(uploadHandle.value) {
                uploadHandle.value?.let { handle ->
                    while (true) {
                        // Poll transfer status
                        // TODO: Add native function to get transfer info
                        delay(500)
                    }
                }
            }
            
            // Show transfer progress dialog
            if (showTransferDialog.value && currentTransfer.value != null) {
                TransferProgressDialog(
                    transferInfo = currentTransfer.value!!,
                    onCancel = {
                        uploadHandle.value?.let { handle ->
                            uploadCancel(handle) 
                        }
                        showTransferDialog.value = false
                        currentTransfer.value = null
                    },
                    onDismiss = {
                        // Close dialog and clear state when dismissed
                        showTransferDialog.value = false
                        currentTransfer.value = null
                    }
                )
            }
            
            // Show incoming transfer dialog
            incomingRequest.value?.let { request ->
                IncomingTransferDialog(
                    fileName = request.fileName,
                    fileSize = request.fileSize,
                    senderName = request.senderName,
                    onAccept = {
                        // Accept the transfer
                        setTransferDecision(true)
                        incomingRequest.value = null
                    },
                    onReject = {
                        // Reject the transfer
                        setTransferDecision(false)
                        incomingRequest.value = null
                    }
                )
            }
            
            // Poll for pending incoming transfers when service is running
            LaunchedEffect(isServiceRunning.value) {
                if (isServiceRunning.value) {
                    while (true) {
                        val pending = getPendingTransfer()
                        if (pending != "{}" && incomingRequest.value == null) {
                            // Parse pending transfer info
                            try {
                                val filename = pending.substringAfter("\"filename\":\"").substringBefore("\"")
                                val size = pending.substringAfter("\"size\":").substringBefore("}").toLongOrNull() ?: 0
                                incomingRequest.value = IncomingRequest(
                                    fileName = filename,
                                    fileSize = formatFileSize(size),
                                    senderName = "Unknown",
                                    senderIp = ""
                                )
                            } catch (e: Exception) {
                                println("Failed to parse pending transfer: $e")
                            }
                        }
                        delay(500)
                    }
                }
            }
            
            // Device list screen
            if (currentScreen == "deviceList") {
                DeviceListScreen(
                    devices = devices,
                    isScanning = isDiscovering.value,
                    isServiceRunning = isServiceRunning.value,
                    isReceiving = currentTransfer.value?.state == TransferState.TRANSFERRING && 
                                  currentTransfer.value?.isSender == false,
                    receiveProgress = currentTransfer.value?.progress ?: 0,
                    receiveFileName = currentTransfer.value?.fileName,
                    receiveSpeed = currentTransfer.value?.transferSpeed ?: 0.0,
                    onDeviceClick = { device ->
                        setSelectedDevice(device)
                        setCurrentScreen("sendFile")
                    },
                    onScan = {
                        setDevices(emptyList())
                        isDiscovering.value = true
                        
                        CoroutineScope(Dispatchers.IO).launch {
                            try {
                                println("Starting mDNS discovery...")
                                val result = startDiscoverer("_lanshare._tcp.local.", 17116)
                                if (result) {
                                    println("mDNS discovery started, waiting for callbacks...")
                                    delay(3000)
                                    isDiscovering.value = false
                                } else {
                                    println("Failed to start mDNS discovery")
                                    isDiscovering.value = false
                                }
                            } catch (e: Exception) {
                                println("Discovery error: ${e.message}")
                                e.printStackTrace()
                                isDiscovering.value = false
                            }
                        }
                    },
                    onStartService = {
                        scope.launch(Dispatchers.IO) {
                            println("Starting downloadServerStart...")
                            val serverResult = downloadServerStart(17116)
                            if (serverResult == 0) {
                                println("✓ Download server started on port 17116")
                            } else {
                                println("✗ Failed to start download server: $serverResult")
                            }
                        }

                        scope.launch(Dispatchers.IO) {
                            try {

                                val deviceInfo = getLocalDeviceInfo()
                                println("Starting services... ${deviceInfo?.id}, ${deviceInfo?.name}, ${deviceInfo?.osName}, ${deviceInfo?.ipAddress}")
                                if (deviceInfo != null) {
                                    val result = startBroadcaster(
                                        deviceInfo.id,
                                        deviceInfo.name,
                                        deviceInfo.osName,
                                        deviceInfo.ipAddress,
                                        17116
                                    )
                                    
                                    isServiceRunning.value = result
                                    if (result) {
                                        println("✓ All services started successfully")
                                    } else {
                                        println("✗ Failed to start mDNS service broadcaster")
                                    }
                                } else {
                                    println("Failed to get local device info")
                                    withContext(Dispatchers.Main) {
                                        isServiceRunning.value = false
                                    }
                                }
                            } catch (e: Exception) {
                                println("Error starting services: ${e.message}")
                                e.printStackTrace()
                                withContext(Dispatchers.Main) {
                                    isServiceRunning.value = false
                                }
                            }
                        }
                    },
                    onStopService = {
                        stopBroadcaster()
                        downloadServerStop()
                        isServiceRunning.value = false
                        println("All services stopped")
                    },
                    onAcceptReceive = {
                        // Accept handled in IncomingTransferDialog
                    },
                    onRejectReceive = {
                        // Reject handled in IncomingTransferDialog
                    }
                )
            }
            
            // Send file screen
            if (selectedDevice != null && currentScreen == "sendFile") {
                SendFileScreen(
                    peerDevice = selectedDevice,
                    onSend = { filePath, fileName ->
                        scope.launch(Dispatchers.IO) {
                            uploadFileWithProgress(
                                peerIp = selectedDevice.ipAddress,
                                port = 17116,
                                filePath = filePath,
                                fileName = fileName,
                                peerName = selectedDevice.name,
                                onTransferUpdate = { transferInfo ->
                                    scope.launch(Dispatchers.Main) {
                                        currentTransfer.value = transferInfo
                                        showTransferDialog.value = true
                                    }
                                },
                                onHandleCreated = { handle ->
                                    uploadHandle.value = handle
                                },
                                onTransferComplete = {
                                    // Reset upload handle when transfer is complete
                                    uploadHandle.value = null
                                }
                            )
                        }
                    },
                    onBack = {
                        setCurrentScreen("deviceList")
                        setSelectedDevice(null)
                    }
                )
            }
        }
    }
}

/**
 * Data class for incoming transfer request
 */
data class IncomingRequest(
    val fileName: String,
    val fileSize: String,
    val senderName: String,
    val senderIp: String
)

/**
 * Format file size to human readable string
 */
private fun formatFileSize(bytes: Long): String {
    return when {
        bytes < 1024 -> "$bytes B"
        bytes < 1024 * 1024 -> "%.2f KB".format(bytes / 1024.0)
        bytes < 1024 * 1024 * 1024 -> "%.2f MB".format(bytes / (1024.0 * 1024.0))
        else -> "%.2f GB".format(bytes / (1024.0 * 1024.0 * 1024.0))
    }
}

/**
 * Upload file with progress tracking
 */
private suspend fun uploadFileWithProgress(
    peerIp: String,
    port: Int,
    filePath: String,
    fileName: String,
    peerName: String,
    onTransferUpdate: (TransferInfo) -> Unit,
    onHandleCreated: (Long) -> Unit,
    onTransferComplete: () -> Unit
) {
    println("Uploading $filePath to $peerIp:$port")
    
    // Create upload handle based on file path type
    val handle = if (filePath.startsWith("fd:")) {
        // Extract file descriptor from path like "fd:123"
        val fdStr = filePath.substringAfter("fd:")
        val fd = fdStr.toIntOrNull() ?: -1
        if (fd >= 0) {
            uploadCreateWithFd(peerIp, port, fd, fileName, null)
        } else {
            0L
        }
    } else {
        uploadCreate(peerIp, port, filePath, null)
    }
    
    if (handle == 0L) {
        println("Failed to create upload handle")
        onTransferUpdate(
            TransferInfo(
                filePath = filePath,
                fileName = fileName,
                peerId = peerIp,
                peerName = peerName,
                state = TransferState.ERROR,
                type = TransferType.UPLOAD,
                progress = 0,
                dataSize = 0,
                bytesTransferred = 0,
                isSender = true
            )
        )
        onTransferComplete()
        return
    }
    
    onHandleCreated(handle)
    
    // Note: The initial state and progress updates are now handled by the transfer progress callback
    // from the native layer. We just need to call uploadStart which will block until complete.
    
    val result = uploadStart(handle)
    
    // uploadStart returns after the transfer is complete or failed
    // The final state should already be set by the callback, but we ensure cleanup here
    println("Upload completed with result: $result")
    
    uploadDestroy(handle)
    onTransferComplete()
}

