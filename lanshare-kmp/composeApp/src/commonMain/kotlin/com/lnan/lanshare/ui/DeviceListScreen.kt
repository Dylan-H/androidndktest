package com.lnan.lanshare.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.lnan.lanshare.model.Device

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceListScreen(
    devices: List<Device> = emptyList(),
    isScanning: Boolean = false,
    isServiceRunning: Boolean = false,
    isReceiving: Boolean = false,
    receiveProgress: Int = 0,
    receiveFileName: String? = null,
    receiveSpeed: Double = 0.0,
    onDeviceClick: (Device) -> Unit = {},
    onScan: () -> Unit = {},
    onStartService: () -> Unit = {},
    onStopService: () -> Unit = {},
    onAcceptReceive: () -> Unit = {},
    onRejectReceive: () -> Unit = {}
) {
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("LAN Share - 设备列表") },
                actions = {
                    // Service status indicator and toggle button
                    IconButton(onClick = {
                        if (isServiceRunning) {
                            onStopService()
                        } else {
                            onStartService()
                        }
                    }) {
                        if (isServiceRunning) {
                            Icon(
                                imageVector = androidx.compose.material.icons.Icons.Default.Refresh,
                                contentDescription = "服务运行中",
                                tint = MaterialTheme.colorScheme.primary
                            )
                        } else {
                            Icon(
                                imageVector = androidx.compose.material.icons.Icons.Default.Refresh,
                                contentDescription = "启动服务",
                                tint = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                }
            )
        },
        floatingActionButton = {
            FloatingActionButton(
                onClick = onScan,
                containerColor = MaterialTheme.colorScheme.primary,
                contentColor = MaterialTheme.colorScheme.onPrimary,
            ) {
                if (isScanning) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(24.dp),
                        color = MaterialTheme.colorScheme.onPrimary,
                        strokeWidth = 2.dp
                    )
                } else {
                    Icon(
                        imageVector = androidx.compose.material.icons.Icons.Default.Refresh,
                        contentDescription = "扫描设备"
                    )
                }
            }
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .padding(paddingValues)
                .padding(16.dp)
        ) {
            // Service status banner
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(
                    containerColor = if (isServiceRunning) {
                        MaterialTheme.colorScheme.primaryContainer
                    } else {
                        MaterialTheme.colorScheme.surfaceVariant
                    }
                )
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(12.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = if (isServiceRunning) "✓ mDNS 服务运行中" else "○ mDNS 服务未启动",
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Bold
                        )
                        Text(
                            text = if (isServiceRunning) "其他设备可以发现你" else "点击图标启动服务",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Button(
                        onClick = {
                            if (isServiceRunning) {
                                onStopService()
                            } else {
                                onStartService()
                            }
                        },
                        enabled = !isScanning
                    ) {
                        Text(if (isServiceRunning) "停止" else "启动")
                    }
                }
            }
            Spacer(modifier = Modifier.height(16.dp))
            
            // Show receive dialog if receiving
            if (isReceiving) {
                println("show ReceiveDialog")
                ReceiveDialog(
                    fileName = receiveFileName,
                    progress = receiveProgress,
                    transferSpeed = receiveSpeed,
                    onAccept = onAcceptReceive,
                    onReject = onRejectReceive
                )
            }
            
            if (isScanning && devices.isEmpty()) {
                ScanningState()
            } else if (devices.isEmpty()) {
                EmptyState()
            } else {
                if (isScanning) {
                    LinearProgressIndicator(
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                }
                DeviceList(devices, onDeviceClick)
            }
        }
    }
}

@Composable
fun ReceiveDialog(
    fileName: String?,
    progress: Int,
    transferSpeed: Double = 0.0,
    onAccept: () -> Unit,
    onReject: () -> Unit
) {
    AlertDialog(
        onDismissRequest = { },
        title = { Text("接收文件") },
        text = {
            Column {
                if (fileName != null) {
                    Text("文件名: $fileName")
                    Spacer(modifier = Modifier.height(8.dp))
                }
                if (progress > 0) {
                    Text("接收进度: $progress%")
                    Spacer(modifier = Modifier.height(8.dp))
                    LinearProgressIndicator(
                        progress = { progress / 100f },
                        modifier = Modifier.fillMaxWidth()
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    // Show transfer speed
                    val speedText = when {
                        transferSpeed < 1024 -> "%.0f B/s".format(transferSpeed)
                        transferSpeed < 1024 * 1024 -> "%.2f KB/s".format(transferSpeed / 1024.0)
                        else -> "%.2f MB/s".format(transferSpeed / (1024.0 * 1024.0))
                    }
                    Text(
                        text = "传输速率: $speedText",
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium
                    )
                } else {
                    Text("有设备想要发送文件给你")
                }
            }
        },
        confirmButton = {
            Button(onClick = onAccept) {
                Text("接受")
            }
        },
        dismissButton = {
            OutlinedButton(onClick = onReject) {
                Text("拒绝")
            }
        }
    )
}

@Composable
private fun DeviceList(
    devices: List<Device>,
    onDeviceClick: (Device) -> Unit
) {
    LazyColumn(
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        items(devices) { device ->
            DeviceItem(device = device, onClick = { onDeviceClick(device) })
        }
    }
}

@Composable
private fun DeviceItem(
    device: Device,
    onClick: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
    ) {
        Column(
            modifier = Modifier
                .padding(16.dp)
                .fillMaxWidth()
        ) {
            Text(
                text = device.name,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "IP: ${device.ipAddress}",
                style = MaterialTheme.typography.bodyMedium
            )
            Spacer(modifier = Modifier.height(4.dp))
            Text(
                text = "平台: ${device.osName}",
                style = MaterialTheme.typography.bodySmall
            )
        }
    }
}

@Composable
private fun EmptyState() {
    Column(
        modifier = Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "未找到设备",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = "点击右下角按钮扫描设备",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "请确保桌面端应用正在运行",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@Composable
private fun ScanningState() {
    Column(
        modifier = Modifier.fillMaxSize(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        CircularProgressIndicator(
            modifier = Modifier.size(48.dp),
            strokeWidth = 4.dp
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = "正在扫描设备...",
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text(
            text = "请稍候",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
expect fun SendFileScreen(
    peerDevice: Device,
    onSend: (filePath: String, fileName: String) -> Unit = { _, _ -> },
    onBack: () -> Unit = {}
)

