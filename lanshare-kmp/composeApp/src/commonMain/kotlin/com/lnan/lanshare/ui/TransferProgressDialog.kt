package com.lnan.lanshare.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Divider
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.lnan.lanshare.model.TransferInfo
import com.lnan.lanshare.model.TransferState

/**
 * Dialog to show transfer progress with speed and status
 */
@Composable
fun TransferProgressDialog(
    transferInfo: TransferInfo,
    onCancel: () -> Unit,
    onDismiss: () -> Unit = {}
) {
    // Determine if the dialog can be dismissed (completed, cancelled, error states)
    val canDismiss = transferInfo.state == TransferState.FINISHED ||
                     transferInfo.state == TransferState.CANCELLED ||
                     transferInfo.state == TransferState.ERROR ||
                     transferInfo.state == TransferState.REJECTED ||
                     transferInfo.state == TransferState.TIMEOUT
    
    AlertDialog(
        onDismissRequest = { 
            if (canDismiss) {
                onDismiss()
            }
        },
        title = {
            Text(
                text = if (transferInfo.isSender) "发送文件" else "接收文件",
                style = MaterialTheme.typography.titleLarge
            )
        },
        text = {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                // File name
                Text(
                    text = transferInfo.fileName,
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Medium
                )
                
                // Status text
                val statusText = when (transferInfo.state) {
                    TransferState.PENDING_CONFIRM -> "等待对方确认接收..."
                    TransferState.CONFIRMED -> "对方已确认，准备传输..."
                    TransferState.REJECTED -> "对方已拒绝接收"
                    TransferState.TIMEOUT -> "等待确认超时"
                    TransferState.TRANSFERRING -> "正在传输..."
                    TransferState.PAUSED -> "传输已暂停"
                    TransferState.CANCELLED -> "传输已取消"
                    TransferState.FINISHED -> "传输完成"
                    TransferState.ERROR -> "传输出错"
                    else -> "准备中..."
                }
                
                Text(
                    text = statusText,
                    style = MaterialTheme.typography.bodyMedium,
                    color = when (transferInfo.state) {
                        TransferState.REJECTED, TransferState.TIMEOUT, TransferState.ERROR -> 
                            MaterialTheme.colorScheme.error
                        TransferState.FINISHED -> MaterialTheme.colorScheme.primary
                        else -> MaterialTheme.colorScheme.onSurfaceVariant
                    }
                )
                
                // Progress bar (only show when transferring or finished)
                if (transferInfo.state == TransferState.TRANSFERRING || 
                    transferInfo.state == TransferState.FINISHED ||
                    transferInfo.state == TransferState.PAUSED) {
                    LinearProgressIndicator(
                        progress = { transferInfo.progress / 100f },
                        modifier = Modifier.fillMaxWidth()
                    )
                    
                    // Progress text
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Text(
                            text = transferInfo.formattedProgress(),
                            style = MaterialTheme.typography.bodySmall
                        )
                        // Only show speed when transferring (not when finished)
                        if (transferInfo.state == TransferState.TRANSFERRING) {
                            Text(
                                text = transferInfo.formattedSpeed(),
                                style = MaterialTheme.typography.bodySmall,
                                fontWeight = FontWeight.Medium
                            )
                        }
                    }
                } else if (transferInfo.state == TransferState.PENDING_CONFIRM) {
                    // Show indeterminate progress for waiting state
                    LinearProgressIndicator(
                        modifier = Modifier.fillMaxWidth()
                    )
                    Text(
                        text = "文件大小: ${transferInfo.formattedSize()}",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        },
        confirmButton = {
            if (transferInfo.state == TransferState.TRANSFERRING || 
                transferInfo.state == TransferState.PENDING_CONFIRM) {
                TextButton(
                    onClick = onCancel,
                    colors = ButtonDefaults.textButtonColors(
                        contentColor = MaterialTheme.colorScheme.error
                    )
                ) {
                    Text("取消")
                }
            } else {
                TextButton(onClick = onDismiss) {
                    Text("确定")
                }
            }
        },
        dismissButton = null
    )
}

/**
 * Dialog to show incoming transfer request (for receiver)
 */
@Composable
fun IncomingTransferDialog(
    fileName: String,
    fileSize: String,
    senderName: String,
    onAccept: () -> Unit,
    onReject: () -> Unit
) {
    AlertDialog(
        onDismissRequest = { /* Prevent dismiss by clicking outside */ },
        title = {
            Text(
                text = "接收文件",
                style = MaterialTheme.typography.titleLarge
            )
        },
        text = {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = "$senderName 想要发送文件给你",
                    style = MaterialTheme.typography.bodyMedium
                )
                
                Divider(modifier = Modifier.padding(vertical = 8.dp))
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "文件名:",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = fileName,
                        style = MaterialTheme.typography.bodyMedium,
                        fontWeight = FontWeight.Medium
                    )
                }
                
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text(
                        text = "文件大小:",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Text(
                        text = fileSize,
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
                
                Spacer(modifier = Modifier.height(8.dp))
                
                Text(
                    text = "请在1分钟内做出选择",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
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
