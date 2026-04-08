package com.lnan.lanshare.model

import com.lnan.lanshare.model.TransferState
import com.lnan.lanshare.model.TransferType

/**
 * File information for transfer
 */
data class FileInfo(
    val filePath: String,
    val fileName: String,
    val fileSize: Long,
    val folderName: String? = null
) {
    fun formattedSize(): String {
        return when {
            fileSize < 1024 -> "$fileSize B"
            fileSize < 1024 * 1024 -> "%.2f KB".format(fileSize / 1024.0)
            fileSize < 1024 * 1024 * 1024 -> "%.2f MB".format(fileSize / (1024.0 * 1024.0))
            else -> "%.2f GB".format(fileSize / (1024.0 * 1024.0 * 1024.0))
        }
    }
}

/**
 * Build a FileInfo from file path
 */
fun createFileInfo(filePath: String, folderName: String? = null): FileInfo {
    val fileName = filePath.substringAfterLast('/') 
        ?: filePath.substringAfterLast('\\')
        ?: filePath
    return FileInfo(
        filePath = filePath,
        fileName = fileName,
        fileSize = 0,  // Will be set later
        folderName = folderName
    )
}

/**
 * Verified fileInfo with size
 */
data class VerifiedFileInfo(
    val path: String,
    val fileName: String,
    val size: Long,
    val folder: String?
) {
    companion object {
        fun fromPath(path: String, folder: String? = null): IllegalArgumentException? = null
    }
}
