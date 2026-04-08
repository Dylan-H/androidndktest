package com.lnan.lanshare.model

/**
 * Represents a device on the local network
 */
data class Device(
    val id: String,
    val name: String,
    val osName: String,
    val ipAddress: String
) {
    override fun toString(): String = "Device(id=$id, name=$name, os=$osName, ip=$ipAddress)"
}

/**
 * Transfer state enum
 */
enum class TransferState {
    IDLE,
    WAITING,           // Waiting for connection
    PENDING_CONFIRM,   // Waiting for receiver confirmation
    CONFIRMED,         // Receiver confirmed, ready to transfer
    REJECTED,          // Receiver rejected
    TIMEOUT,           // Confirmation timeout
    DISCONNECTED,
    PAUSED,
    CANCELLED,
    TRANSFERRING,
    FINISHED,
    ERROR
}

/**
 * Transfer type enum
 */
enum class TransferType {
    NONE,
    DOWNLOAD,
    UPLOAD
}

/**
 * Transfer information model
 */
data class TransferInfo(
    val filePath: String,
    val fileName: String,
    val peerId: String,
    val peerName: String,
    val state: TransferState,
    val type: TransferType,
    val progress: Int,
    val dataSize: Long,
    val bytesTransferred: Long,
    val transferSpeed: Double = 0.0,  // Bytes per second
    val isSender: Boolean = true
) {
    fun formattedSize(): String = formatBytes(dataSize)
    fun formattedTransferred(): String = formatBytes(bytesTransferred)
    
    fun formattedSpeed(): String {
        return when {
            transferSpeed < 1024 -> "%.0f B/s".format(transferSpeed)
            transferSpeed < 1024 * 1024 -> "%.2f KB/s".format(transferSpeed / 1024.0)
            else -> "%.2f MB/s".format(transferSpeed / (1024.0 * 1024.0))
        }
    }
    
    fun formattedProgress(): String {
        return "$progress% (${formattedTransferred()}/${formattedSize()})"
    }
    
    private fun formatBytes(bytes: Long): String {
        return when {
            bytes < 1024 -> "$bytes B"
            bytes < 1024 * 1024 -> "%.2f KB".format(bytes / 1024.0)
            bytes < 1024 * 1024 * 1024 -> "%.2f MB".format(bytes / (1024.0 * 1024.0))
            else -> "%.2f GB".format(bytes / (1024.0 * 1024.0 * 1024.0))
        }
    }
}

/**
 * Pending transfer request (for receiver)
 */
data class PendingTransferRequest(
    val id: String,
    val fileName: String,
    val fileSize: Long,
    val senderName: String,
    val senderIp: String,
    val timestamp: Long = System.currentTimeMillis()
)
