/*
    LAN-Share JNI Bridge
    Java Native Interface for Android integration
    
    Copyright (C) 2026 LAN-Share Project
    
    This file provides JNI bindings for Android Java/Kotlin code
    to access the native LAN-Share library.
*/

#include "lanshare_log.h"
#include "lanshare_mdns.h"
#include "lanshare_transfer.h"
#include <android/log.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Global references for callbacks */
static jobject g_transfer_callback = NULL;
static JavaVM* g_vm = NULL;
static jobject g_device_callback = NULL;
static jmethodID g_device_callback_method = NULL;

/* Global reference for transfer progress callback */
static jobject g_transfer_progress_callback = NULL;
static jmethodID g_transfer_progress_method = NULL;


/* Android log level mapping */
static android_LogPriority lanshare_level_to_android(lanshare_log_level_t level) {
    switch (level) {
        case LOG_LEVEL_VERBOSE: return ANDROID_LOG_VERBOSE;
        case LOG_LEVEL_DEBUG:   return ANDROID_LOG_DEBUG;
        case LOG_LEVEL_INFO:    return ANDROID_LOG_INFO;
        case LOG_LEVEL_WARN:    return ANDROID_LOG_WARN;
        case LOG_LEVEL_ERROR:   return ANDROID_LOG_ERROR;
        case LOG_LEVEL_FATAL:   return ANDROID_LOG_FATAL;
        default:                return ANDROID_LOG_DEBUG;
    }
}

/* Android logcat callback implementation */
static void android_log_callback(lanshare_log_level_t level, const char* tag, const char* message) {
    android_LogPriority priority = lanshare_level_to_android(level);
    __android_log_write(priority, tag, message);
}

/* Initialize logging for Android */
static void init_android_logging(void) {
    lanshare_log_init(android_log_callback);
}

/* Converts Java String to C string (must be freed with ReleaseStringUTFChars) */
static const char* jstring_to_cstring(JNIEnv* env, jstring str)
{
    if (!str) {
        return NULL;
    }
    
    return (*env)->GetStringUTFChars(env, str, NULL);
}



/* mDNS device callback - called from native thread */
static void mdns_device_callback(const lanshare_device_t* device, void* user_data)
{
    (void)user_data;
    
    if (!g_vm || !g_device_callback || !g_device_callback_method) {
        LOGW("JNI", "Device callback not registered");
        return;
    }
    
    /* Get JNIEnv from native thread */
    JNIEnv* env;
    jint get_env_result = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    int need_detach = 0;
    
    /* If not attached, attach this thread to JVM */
    if (get_env_result == JNI_EDETACHED) {
        jint attach_result = (*g_vm)->AttachCurrentThread(g_vm, &env, NULL);
        if (attach_result != JNI_OK) {
            LOGE("JNI", "Failed to attach thread to JVM");
            return;
        }
        need_detach = 1;
    } else if (get_env_result != JNI_OK) {
        LOGE("JNI", "Failed to get JNIEnv");
        return;
    }
    
    /* Create Device object */
    jclass device_class = (*env)->FindClass(env, "com/lnan/lanshare/model/Device");
    if (!device_class) {
        LOGE("JNI", "Failed to find Device class");
        if (need_detach) {
            (*g_vm)->DetachCurrentThread(g_vm);
        }
        return;
    }
    
    /* Get Device constructor: Device(String, String, String, String) */
    jmethodID device_ctor = (*env)->GetMethodID(env, device_class, "<init>", 
                                                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    if (!device_ctor) {
        LOGE("JNI", "Failed to get Device constructor");
        (*env)->DeleteLocalRef(env, device_class);
        if (need_detach) {
            (*g_vm)->DetachCurrentThread(g_vm);
        }
        return;
    }
    
    /* Create Java strings from C strings */
    jstring j_id = (*env)->NewStringUTF(env, device->id);
    jstring j_name = (*env)->NewStringUTF(env, device->name);
    jstring j_os = (*env)->NewStringUTF(env, device->os_name);
    jstring j_ip = (*env)->NewStringUTF(env, device->ip_address);
    
    /* Create Device object */
    jobject j_device = (*env)->NewObject(env, device_class, device_ctor, 
                                         j_id, j_name, j_os, j_ip);
    
    /* Call the callback method */
    (*env)->CallVoidMethod(env, g_device_callback, g_device_callback_method, j_device);
    
    /* Clean up local references */
    if (j_id) (*env)->DeleteLocalRef(env, j_id);
    if (j_name) (*env)->DeleteLocalRef(env, j_name);
    if (j_os) (*env)->DeleteLocalRef(env, j_os);
    if (j_ip) (*env)->DeleteLocalRef(env, j_ip);
    if (j_device) (*env)->DeleteLocalRef(env, j_device);
    if (device_class) (*env)->DeleteLocalRef(env, device_class);
    
    /* Detach thread only if we attached it */
    if (need_detach) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}



/* Library initialization */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved)
{
    /* Store JavaVM globally */
    g_vm = vm;
    
    /* Initialize Android logging to redirect native logs to logcat */
    init_android_logging();
    LOGI("LANShareNative", "JNI_OnLoad: Native library loaded successfully");
    
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved)
{
    /* Cleanup library */

    JNIEnv* env;
    if ((*vm)->GetEnv(vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        /* Delete global references */
        if (g_transfer_callback != NULL) {
            (*env)->DeleteGlobalRef(env, g_transfer_callback);
            g_transfer_callback = NULL;
        }
        if (g_device_callback != NULL) {
            (*env)->DeleteGlobalRef(env, g_device_callback);
            g_device_callback = NULL;
            g_device_callback_method = NULL;
        }
    }
    
    g_vm = NULL;
}

/* =========================================
 * New JNI functions for com_lnan_lanshare_ndk_NativeLibrary
 * ========================================= */

/* Global mDNS handles for cleanup */
static lanshare_mdns_handle_t g_mdns_discoverer_handle = NULL;
static lanshare_mdns_handle_t g_mdns_broadcaster_handle = NULL;

/* JNI function: startDiscoverer */
JNIEXPORT jboolean JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_startDiscoverer
  (JNIEnv* env, jclass clazz, jstring serviceType, jint port)
{
    const char* c_service_type = jstring_to_cstring(env, serviceType);
    
    lanshare_mdns_handle_t handle = lanshare_mdns_init(c_service_type, port);
    
    (*env)->ReleaseStringUTFChars(env, serviceType, c_service_type);
    
//    if (!handle) {
//        return JNI_FALSE;
//    }
    
    int result = lanshare_mdns_start_discoverer(NULL, mdns_device_callback, NULL);
    
    /* Store handle globally for later cleanup */
    g_mdns_discoverer_handle = NULL;
    
    return (result == 0) ? JNI_TRUE : JNI_FALSE;
}

/* JNI function: stopDiscoverer */
JNIEXPORT jboolean JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_stopDiscoverer
  (JNIEnv* env, jclass clazz)
{
    if (g_mdns_discoverer_handle != NULL) {
        lanshare_mdns_cleanup(g_mdns_discoverer_handle);
        g_mdns_discoverer_handle = NULL;
        return JNI_TRUE;
    }
    
    return JNI_FALSE;
}


/* JNI function: startBroadcaster */
JNIEXPORT jboolean JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_startBroadcaster
  (JNIEnv* env, jclass clazz, jstring id, jstring name, jstring osName, jstring ipAddress, jint port)
{
    lanshare_device_t device;
    memset(&device, 0, sizeof(device));
    
    const char* c_id = jstring_to_cstring(env, id);
    const char* c_name = jstring_to_cstring(env, name);
    const char* c_os = jstring_to_cstring(env, osName);
    const char* c_ip = jstring_to_cstring(env, ipAddress);
    
    strncpy(device.id, c_id, sizeof(device.id) - 1);
    strncpy(device.name, c_name, sizeof(device.name) - 1);
    strncpy(device.os_name, c_os, sizeof(device.os_name) - 1);
    strncpy(device.ip_address, c_ip, sizeof(device.ip_address) - 1);
    
    (*env)->ReleaseStringUTFChars(env, id, c_id);
    (*env)->ReleaseStringUTFChars(env, name, c_name);
    (*env)->ReleaseStringUTFChars(env, osName, c_os);
    (*env)->ReleaseStringUTFChars(env, ipAddress, c_ip);
    
    lanshare_mdns_handle_t handle = lanshare_mdns_init();
    
    if (!handle) {
        return JNI_FALSE;
    }
    
    int result = lanshare_mdns_start_broadcaster(handle, &device,"_lanshare._tcp.local.", port);
    
    /* Store handle globally for later cleanup */
    g_mdns_broadcaster_handle = handle;
    
    return (result == 0) ? JNI_TRUE : JNI_FALSE;
}

/* JNI function: stopBroadcaster */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_stopBroadcaster
  (JNIEnv* env, jclass clazz)
{
    if (g_mdns_broadcaster_handle != NULL) {
        lanshare_mdns_cleanup(g_mdns_broadcaster_handle);
        g_mdns_broadcaster_handle = NULL;
    }
}

/* JNI function: uploadCreate */
JNIEXPORT jlong JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadCreate
  (JNIEnv* env, jclass clazz, jstring peerIp, jint port, jstring filePath, jstring folder)
{
    const char* c_peer_ip = jstring_to_cstring(env, peerIp);
    const char* c_file_path = jstring_to_cstring(env, filePath);
    const char* c_folder = folder ? jstring_to_cstring(env, folder) : NULL;
    
    lanshare_upload_handle_t handle = lanshare_upload_create(
        c_peer_ip, port, c_file_path, c_folder);
    
    (*env)->ReleaseStringUTFChars(env, peerIp, c_peer_ip);
    (*env)->ReleaseStringUTFChars(env, filePath, c_file_path);
    if (folder) {
        (*env)->ReleaseStringUTFChars(env, folder, c_folder);
    }
    
    return (jlong)(intptr_t)handle;
}

/* JNI function: uploadCreateWithFd - Create upload task using file descriptor */
JNIEXPORT jlong JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadCreateWithFd
  (JNIEnv* env, jclass clazz, jstring peerIp, jint port, jint fileDescriptor, jstring fileName, jstring folder)
{
    if (fileDescriptor < 0) {
        LOGE("JNI", "Invalid file descriptor: %d", fileDescriptor);
        return 0;
    }
    
    const char* c_peer_ip = jstring_to_cstring(env, peerIp);
    const char* c_file_name = jstring_to_cstring(env, fileName);
    const char* c_folder = folder ? jstring_to_cstring(env, folder) : NULL;
    
    // Duplicate the file descriptor to avoid conflicts with Java layer
    int dupFd = dup(fileDescriptor);
    if (dupFd == -1) {
        LOGE("JNI", "Failed to duplicate file descriptor");
        (*env)->ReleaseStringUTFChars(env, peerIp, c_peer_ip);
        (*env)->ReleaseStringUTFChars(env, fileName, c_file_name);
        if (folder) {
            (*env)->ReleaseStringUTFChars(env, folder, c_folder);
        }
        return 0;
    }
    
    // Create upload handle using file descriptor
    lanshare_upload_handle_t handle = lanshare_upload_create_with_fd(
        c_peer_ip, port, dupFd, c_file_name, c_folder);
    
    (*env)->ReleaseStringUTFChars(env, peerIp, c_peer_ip);
    (*env)->ReleaseStringUTFChars(env, fileName, c_file_name);
    if (folder) {
        (*env)->ReleaseStringUTFChars(env, folder, c_folder);
    }
    
    if (!handle) {
        close(dupFd); // Clean up if handle creation failed
        LOGE("JNI", "Failed to create upload handle with file descriptor");
    } else {
        LOGI("JNI", "Successfully created upload handle with fd: %d", dupFd);
    }
    
    return (jlong)(intptr_t)handle;
}


/* JNI function: uploadPause */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadPause
  (JNIEnv* env, jclass clazz, jlong handle)
{
    return lanshare_upload_pause((lanshare_upload_handle_t)(intptr_t)handle);
}

/* JNI function: uploadResume */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadResume
  (JNIEnv* env, jclass clazz, jlong handle)
{
    return lanshare_upload_resume((lanshare_upload_handle_t)(intptr_t)handle);
}

/* JNI function: uploadCancel */
JNIEXPORT jboolean JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadCancel
  (JNIEnv* env, jclass clazz, jlong handle)
{
    int result = lanshare_upload_cancel((lanshare_upload_handle_t)(intptr_t)handle);
    return (result == 0) ? JNI_TRUE : JNI_FALSE;
}

/* JNI function: uploadDestroy */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadDestroy
  (JNIEnv* env, jclass clazz, jlong handle)
{
    lanshare_upload_destroy((lanshare_upload_handle_t)(intptr_t)handle);
}

/* JNI function: uploadGetStatus */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadGetStatus
  (JNIEnv* env, jclass clazz, jlong handle)
{
    lanshare_transfer_info_t info;
    if (lanshare_get_transfer_info((void*)(intptr_t)handle, 1, &info) == 0) {
        return (jint)info.state;
    }
    return -1;
}

/* Transfer progress callback from native thread */
static void transfer_progress_callback(const lanshare_transfer_info_t* info, void* user_data)
{
    (void)user_data;

    if (!g_vm || !g_transfer_progress_callback || !g_transfer_progress_method) {
        return;
    }

    /* Get JNIEnv from native thread */
    JNIEnv* env;
    jint get_env_result = (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6);
    int need_detach = 0;

    /* If not attached, attach this thread to JVM */
    if (get_env_result == JNI_EDETACHED) {
        jint attach_result = (*g_vm)->AttachCurrentThread(g_vm, &env, NULL);
        if (attach_result != JNI_OK) {
            LOGE("JNI", "Failed to attach thread to JVM for transfer callback");
            return;
        }
        need_detach = 1;
    } else if (get_env_result != JNI_OK) {
        LOGE("JNI", "Failed to get JNIEnv for transfer callback");
        return;
    }

    /* Create TransferInfo object */
    jclass info_class = (*env)->FindClass(env, "com/lnan/lanshare/model/TransferInfo");
    if (!info_class) {
        LOGE("JNI", "Failed to find TransferInfo class");
        if (need_detach) {
            (*g_vm)->DetachCurrentThread(g_vm);
        }
        return;
    }

    /* Get constructor: TransferInfo(String, String, String, String, TransferState, TransferType, int, long, long, double, boolean) */
    jmethodID ctor = (*env)->GetMethodID(env, info_class, "<init>",
        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Lcom/lnan/lanshare/model/TransferState;Lcom/lnan/lanshare/model/TransferType;IJJDZ)V");
    if (!ctor) {
        LOGE("JNI", "Failed to get TransferInfo constructor");
        (*env)->DeleteLocalRef(env, info_class);
        if (need_detach) {
            (*g_vm)->DetachCurrentThread(g_vm);
        }
        return;
    }

    /* Get TransferState enum value */
    jclass state_class = (*env)->FindClass(env, "com/lnan/lanshare/model/TransferState");
    jfieldID state_field = (*env)->GetStaticFieldID(env, state_class,
        info->state == TRANSFER_STATE_PENDING_CONFIRM ? "PENDING_CONFIRM" :
        info->state == TRANSFER_STATE_TRANSFERRING ? "TRANSFERRING" :
        info->state == TRANSFER_STATE_FINISHED ? "FINISHED" :
        info->state == TRANSFER_STATE_CANCELLED ? "CANCELLED" :
        info->state == TRANSFER_STATE_ERROR ? "ERROR" : "IDLE",
        "Lcom/lnan/lanshare/model/TransferState;");
    jobject state_obj = (*env)->GetStaticObjectField(env, state_class, state_field);

    /* Get TransferType enum value */
    jclass type_class = (*env)->FindClass(env, "com/lnan/lanshare/model/TransferType");
    jfieldID type_field = (*env)->GetStaticFieldID(env, type_class,
        info->type == TRANSFER_TYPE_DOWNLOAD ? "DOWNLOAD" : "UPLOAD",
        "Lcom/lnan/lanshare/model/TransferType;");
    jobject type_obj = (*env)->GetStaticObjectField(env, type_class, type_field);

    /* Create Java strings */
    jstring j_file_path = (*env)->NewStringUTF(env, info->file_path);
    jstring j_file_name = (*env)->NewStringUTF(env, info->file_name);
    jstring j_peer_id = (*env)->NewStringUTF(env, info->peer_id);
    jstring j_peer_name = (*env)->NewStringUTF(env, info->peer_name);

    /* Create TransferInfo object */
    LOGI("JNI", "Creating TransferInfo: progress=%d, data_size=%lld, bytes_transferred=%lld, speed=%.2f, is_sender=%d",
         info->progress, (long long)info->data_size, (long long)info->bytes_transferred, 
         info->transfer_speed, info->is_sender);
    
    jobject j_info = (*env)->NewObject(env, info_class, ctor,
        j_file_path, j_file_name, j_peer_id, j_peer_name,
        state_obj, type_obj,
        (jint)info->progress,
        (jlong)info->data_size,
        (jlong)info->bytes_transferred,
        (jdouble)info->transfer_speed,
        (jboolean)info->is_sender);
    
    if (!j_info) {
        LOGE("JNI", "Failed to create TransferInfo object");
        goto cleanup;
    }

    /* Call the callback method */
    LOGI("JNI", "Calling transfer progress callback");
    (*env)->CallVoidMethod(env, g_transfer_progress_callback, g_transfer_progress_method, j_info);
    
cleanup:

    /* Clean up */
    (*env)->DeleteLocalRef(env, j_file_path);
    (*env)->DeleteLocalRef(env, j_file_name);
    (*env)->DeleteLocalRef(env, j_peer_id);
    (*env)->DeleteLocalRef(env, j_peer_name);
    (*env)->DeleteLocalRef(env, state_obj);
    (*env)->DeleteLocalRef(env, type_obj);
    (*env)->DeleteLocalRef(env, j_info);
    (*env)->DeleteLocalRef(env, info_class);
    (*env)->DeleteLocalRef(env, state_class);
    (*env)->DeleteLocalRef(env, type_class);

    /* Detach thread only if we attached it */
    if (need_detach) {
        (*g_vm)->DetachCurrentThread(g_vm);
    }
}
/* JNI function: uploadStart */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_uploadStart
    (JNIEnv* env, jclass clazz, jlong handle)
{
  return lanshare_upload_start((lanshare_upload_handle_t)(intptr_t)handle, transfer_progress_callback, NULL);
}

/* JNI function: downloadServerStart */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadServerStart
  (JNIEnv* env, jclass clazz, jint port, jstring saveDir)
{
    const char* c_save_dir = saveDir ? jstring_to_cstring(env, saveDir) : NULL;
    
    int result = lanshare_download_server_start(port, c_save_dir, transfer_progress_callback, NULL);
    
    if (saveDir && c_save_dir) {
        (*env)->ReleaseStringUTFChars(env, saveDir, c_save_dir);
    }
    
    if (result == 0) {
        LOGI("JNI", "Download server started on port %d, save dir: %s", port, c_save_dir ? c_save_dir : "default");
    } else {
        LOGE("JNI", "Failed to start download server on port %d", port);
    }
    
    return result;
}

/* JNI function: downloadServerStop */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadServerStop
  (JNIEnv* env, jclass clazz)
{
    return lanshare_download_server_stop();
}

/* JNI function: setTransferDecision */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_setTransferDecision
  (JNIEnv* env, jclass clazz, jboolean accept)
{
    lanshare_set_transfer_decision(accept ? 1 : 0);
}

/* JNI function: getPendingTransfer */
JNIEXPORT jstring JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_getPendingTransfer
  (JNIEnv* env, jclass clazz)
{
    char filename[256] = {0};
    uint64_t file_size = 0;
    
    if (lanshare_get_pending_transfer(filename, sizeof(filename), &file_size) == 0) {
        /* Return JSON with filename and size */
        char result[512];
        snprintf(result, sizeof(result), "{\"filename\":\"%s\",\"size\":%llu}", 
                 filename, (unsigned long long)file_size);
        return (*env)->NewStringUTF(env, result);
    }
    
    return (*env)->NewStringUTF(env, "{}");
}

/* JNI function: downloadCreate */
JNIEXPORT jlong JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadCreate
  (JNIEnv* env, jclass clazz, jstring peerIp, jint port, jstring filePath, jstring saveDir)
{
    const char* c_peer_ip = jstring_to_cstring(env, peerIp);
    const char* c_file_path = jstring_to_cstring(env, filePath);
    const char* c_save_dir = jstring_to_cstring(env, saveDir);
    
    lanshare_download_handle_t handle = lanshare_download_create(
        c_peer_ip, port, c_file_path);
    
    (*env)->ReleaseStringUTFChars(env, peerIp, c_peer_ip);
    (*env)->ReleaseStringUTFChars(env, filePath, c_file_path);
    (*env)->ReleaseStringUTFChars(env, saveDir, c_save_dir);
    
    return (jlong)(intptr_t)handle;
}

/* JNI function: downloadStart */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadStart
  (JNIEnv* env, jclass clazz, jlong handle)
{
    return lanshare_download_start((lanshare_download_handle_t)(intptr_t)handle);
}

/* JNI function: downloadPause */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadPause
  (JNIEnv* env, jclass clazz, jlong handle)
{
    return lanshare_download_pause((lanshare_download_handle_t)(intptr_t)handle);
}

/* JNI function: downloadResume */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadResume
  (JNIEnv* env, jclass clazz, jlong handle)
{
    return lanshare_download_resume((lanshare_download_handle_t)(intptr_t)handle);
}

/* JNI function: downloadCancel */
JNIEXPORT jboolean JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadCancel
  (JNIEnv* env, jclass clazz, jlong handle)
{
    int result = lanshare_download_cancel((lanshare_download_handle_t)(intptr_t)handle);
    return (result == 0) ? JNI_TRUE : JNI_FALSE;
}

/* JNI function: downloadDestroy */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadDestroy
  (JNIEnv* env, jclass clazz, jlong handle)
{
    lanshare_download_destroy((lanshare_download_handle_t)(intptr_t)handle);
}

/* JNI function: downloadGetStatus */
JNIEXPORT jint JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_downloadGetStatus
  (JNIEnv* env, jclass clazz, jlong handle)
{
    lanshare_transfer_info_t info;
    if (lanshare_get_transfer_info((void*)(intptr_t)handle, 0, &info) == 0) {
        return (jint)info.state;
    }
    return -1;
}



/* JNI function: getDiscoveredDevices */
JNIEXPORT jstring JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_getDiscoveredDevices
  (JNIEnv* env, jclass clazz)
{
    /* Process mDNS responses to update device list */
    lanshare_mdns_process_responses();
    
    char buffer[4096]={0};
    
    /* Get discovered devices as JSON */
    const char* json = lanshare_mdns_get_discovered_devices(buffer, sizeof(buffer));
    
    if (!json) {
        return (*env)->NewStringUTF(env, "[]");
    }
    
    return (*env)->NewStringUTF(env, json);
}

/* JNI function: registerDeviceCallback */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_registerDeviceCallback
  (JNIEnv* env, jclass clazz, jobject callback)
{
    /* Delete old callback if exists */
    if (g_device_callback != NULL) {
        (*env)->DeleteGlobalRef(env, g_device_callback);
        g_device_callback = NULL;
    }
    
    if (callback != NULL) {
        /* Create global reference */
        g_device_callback = (*env)->NewGlobalRef(env, callback);
        
        /* Get callback method */
        jclass callback_class = (*env)->GetObjectClass(env, callback);
        g_device_callback_method = (*env)->GetMethodID(env, callback_class, 
                                                       "onDeviceFound", 
                                                       "(Lcom/lnan/lanshare/model/Device;)V");
        (*env)->DeleteLocalRef(env, callback_class);
        
        LOGI("JNI", "Device callback registered successfully");
    } else {
        g_device_callback_method = NULL;
        LOGI("JNI", "Device callback unregistered");
    }
}

/* JNI function: unregisterDeviceCallback */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_unregisterDeviceCallback
  (JNIEnv* env, jclass clazz)
{
    if (g_device_callback != NULL) {
        (*env)->DeleteGlobalRef(env, g_device_callback);
        g_device_callback = NULL;
        g_device_callback_method = NULL;
    }
    LOGI("JNI", "Device callback unregistered");
}

/* JNI function: registerTransferProgressCallback */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_registerTransferProgressCallback
  (JNIEnv* env, jclass clazz, jobject callback)
{
    /* Delete old callback if exists */
    if (g_transfer_progress_callback != NULL) {
        (*env)->DeleteGlobalRef(env, g_transfer_progress_callback);
        g_transfer_progress_callback = NULL;
    }
    
    if (callback != NULL) {
        /* Create global reference */
        g_transfer_progress_callback = (*env)->NewGlobalRef(env, callback);
        
        /* Get callback method */
        jclass callback_class = (*env)->GetObjectClass(env, callback);
        g_transfer_progress_method = (*env)->GetMethodID(env, callback_class, 
                                                       "onTransferProgress", 
                                                       "(Lcom/lnan/lanshare/model/TransferInfo;)V");
        (*env)->DeleteLocalRef(env, callback_class);
        
        LOGI("JNI", "Transfer progress callback registered successfully");
    } else {
        g_transfer_progress_method = NULL;
        LOGI("JNI", "Transfer progress callback unregistered");
    }
}

/* JNI function: unregisterTransferProgressCallback */
JNIEXPORT void JNICALL Java_com_lnan_lanshare_ndk_NativeLibrary_unregisterTransferProgressCallback
  (JNIEnv* env, jclass clazz)
{
    if (g_transfer_progress_callback != NULL) {
        (*env)->DeleteGlobalRef(env, g_transfer_progress_callback);
        g_transfer_progress_callback = NULL;
        g_transfer_progress_method = NULL;
    }
    LOGI("JNI", "Transfer progress callback unregistered");
}
