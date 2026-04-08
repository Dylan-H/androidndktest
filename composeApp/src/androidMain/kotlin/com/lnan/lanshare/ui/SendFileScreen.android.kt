package com.lnan.lanshare.ui

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AccountCircle
import androidx.compose.material.icons.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.lnan.lanshare.model.Device
import com.lnan.lanshare.ndk.NativeLibrary
import com.lnan.lanshare.network.uploadDestroy
import com.lnan.lanshare.network.uploadStart


fun getFileNameFromUri(context: Context, uri: Uri): String? {
    var fileName: String? = null
    // 查询返回的两列信息：DISPLAY_NAME（文件名）和 SIZE（文件大小）
    try {
        context.getContentResolver().query(uri, null, null, null, null).use { cursor ->
            if (cursor != null && cursor.moveToFirst()) {
                // 获取文件名的列索引并取值
                val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                fileName = cursor.getString(nameIndex)
            }
        }
    } catch (e: java.lang.Exception) {
        e.printStackTrace()
    }


    // 备用方案：如果查询失败，从 URI 路径中提取最后一段
    if (fileName == null && uri.getPath() != null) {
        val path = uri.getPath()
        fileName = path!!.substring(path.lastIndexOf('/') + 1)
    }

    return fileName
}
@OptIn(ExperimentalMaterial3Api::class)
@Composable
actual fun SendFileScreen(
    peerDevice: Device,
    onSend: (filePath: String, fileName: String) -> Unit,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var selectedFilePath by remember { mutableStateOf<String?>(null) }
    var selectedFileName by remember { mutableStateOf<String?>(null) }
    var selectedUri by remember { mutableStateOf<Uri?>(null) }
    var fileDescriptor by remember { mutableStateOf<ParcelFileDescriptor?>(null) }
    
    // Cleanup file descriptor when composable is disposed or when selecting new file
    DisposableEffect(Unit) {
        onDispose {
            fileDescriptor?.close()
        }
    }
    
    // File picker launcher using Android native API
    val filePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            result.data?.data?.let { uri ->
                // Close previous file descriptor if exists
                fileDescriptor?.close()
                fileDescriptor = null
                
                selectedUri = uri
                val name =  getFileNameFromUri(context, uri)
                // Get file name from URI
               // val name = uri.lastPathSegment?.substringAfterLast('/') ?: "Unknown"
                selectedFileName = name
                
                // Try to get file descriptor for content:// URIs
                try {
                    val parcelFd = context.contentResolver.openFileDescriptor(uri, "r")
                    if (parcelFd != null) {
                        fileDescriptor = parcelFd
                        val fd = parcelFd.fd
                        // Store the file descriptor as a string path for compatibility
                        selectedFilePath = "fd:$fd"
                    } else {
                        // Fallback to regular path if available
                        selectedFilePath = uri.path
                    }
                } catch (e: Exception) {
                    e.printStackTrace()
                    // Fallback to regular path
                    selectedFilePath = uri.path
                }
            }
        }
    }
    
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("发送文件到: ${peerDevice.name}") },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            imageVector = Icons.Default.ArrowBack,
                            contentDescription = "返回"
                        )
                    }
                }
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .padding(paddingValues)
                .padding(16.dp)
                .fillMaxSize()
        ) {
            Text(
                text = "目标设备: ${peerDevice.name} (${peerDevice.ipAddress})",
                style = MaterialTheme.typography.bodyLarge
            )
            Spacer(modifier = Modifier.height(32.dp))
            
            // Selected file info
            if (selectedFileName != null) {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.primaryContainer
                    )
                ) {
                    Column(
                        modifier = Modifier.padding(16.dp)
                    ) {
                        Text(
                            text = "已选择文件:",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                        Spacer(modifier = Modifier.height(4.dp))
                        Text(
                            text = selectedFileName ?: "",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.Bold,
                            color = MaterialTheme.colorScheme.onPrimaryContainer
                        )
                    }
                }
                Spacer(modifier = Modifier.height(16.dp))
            }
            
            // Select file button
            Button(
                onClick = { 
                    val intent = Intent(Intent.ACTION_GET_CONTENT).apply {
                        type = "*/*"
                        addCategory(Intent.CATEGORY_OPENABLE)
                    }
                    filePicker.launch(intent)
                },
                modifier = Modifier.fillMaxWidth()
            ) {
                Icon(
                    imageVector = Icons.Default.AccountCircle,
                    contentDescription = null,
                    modifier = Modifier.padding(end = 8.dp)
                )
                Text(if (selectedFileName != null) "重新选择文件" else "选择文件")
            }
            
            Spacer(modifier = Modifier.height(16.dp))
            
            // Send button (only enabled when file is selected)
            Button(
                onClick = { 
                    selectedFilePath?.let { path ->
                        val fileName = selectedFileName ?: "unknown"
                        
                        if (path.startsWith("fd:")) {
                            // For file descriptor, we pass the fd path and let the caller handle it
                            onSend(path, fileName)
                        } else {
                            // Use traditional path-based method
                            onSend(path, fileName)
                        }
                        
                        // Reset state after sending
                        selectedFilePath = null
                        selectedFileName = null
                        selectedUri = null
                        // Note: fileDescriptor will be closed when composable is disposed via DisposableEffect
                    }
                },
                modifier = Modifier.fillMaxWidth(),
                enabled = selectedFilePath != null
            ) {
                Text("发送文件")
            }
        }
    }
}
