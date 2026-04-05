/*
    LAN-Share Transfer File Operations
    Cross-platform file handling for transfers
    
    Copyright (C) 2026 LAN-Share Project
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #define R_OK 4
    #define access _access
#else
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "lanshare_transfer.h"

/* Open file for reading (upload) */
FILE* lanshare_file_open_read(const char* path)
{
    if (!path) {
        return NULL;
    }
    
    return fopen(path, "rb");
}

/* Open file from file descriptor for reading (upload) */
FILE* lanshare_file_open_from_fd(int fd)
{
    if (fd < 0) {
        return NULL;
    }
    
    // Use fdopen to convert file descriptor to FILE*
    // "rb" mode for binary read
    return fdopen(fd, "rb");
}

/* Open file for writing (download) */
FILE* lanshare_file_open_write(const char* path)
{
    if (!path) {
        return NULL;
    }
    
    /* Open for writing, create if not exists */
    return fopen(path, "wb");
}

/* Close file */
void lanshare_file_close(FILE* file)
{
    if (file) {
        fclose(file);
    }
}

/* Read data from file */
size_t lanshare_file_read(FILE* file, void* buffer, size_t size)
{
    if (!file || !buffer) {
        return 0;
    }
    
    return fread(buffer, 1, size, file);
}

/* Write data to file */
size_t lanshare_file_write(FILE* file, const void* buffer, size_t size)
{
    if (!file || !buffer) {
        return 0;
    }
    
    return fwrite(buffer, 1, size, file);
}

/* Get file size */
long long lanshare_file_get_size(FILE* file)
{
    if (!file) {
        return -1;
    }
    
    long long current_pos = ftell(file);
    fseek(file, 0, SEEK_END);
    long long size = ftell(file);
    fseek(file, current_pos, SEEK_SET);
    
    return size;
}

/* Set file position */
int lanshare_file_set_pos(FILE* file, long long offset)
{
    if (!file) {
        return -1;
    }
    
    return fseek(file, (long)offset, SEEK_SET);
}

/* Get current file position */
long long lanshare_file_get_pos(FILE* file)
{
    if (!file) {
        return -1;
    }
    
    return ftell(file);
}

/* Create directory recursively */
int lanshare_file_mkdirs(const char* path)
{
    if (!path) {
        return -1;
    }
    
#if defined(_WIN32)
    /* Windows: use _mkdir or CreateDirectory */
    return mkdir(path);
#else
    /* POSIX: use mkdir with mode 0755 */
    return mkdir(path, 0755);
#endif
}

/* Check if file exists */
int lanshare_file_exists(const char* path)
{
    if (!path) {
        return 0;
    }
    
#if defined(_WIN32)
    return _access(path, 0) == 0;
#else
    return access(path, F_OK) == 0;
#endif
}

/* Get file name from path */
const char* lanshare_file_get_name(const char* path)
{
    if (!path) {
        return NULL;
    }
    
    const char* name = strrchr(path, '/');
#ifdef _WIN32
    if (!name) {
        name = strrchr(path, '\\');
    }
#endif
    
    return name ? name + 1 : path;
}

/* Get directory from path */
int lanshare_file_get_dir(const char* path, char* dir, size_t size)
{
    if (!path || !dir || size == 0) {
        return -1;
    }
    
    strncpy(dir, path, size - 1);
    dir[size - 1] = '\0';
    
    char* last_sep = strrchr(dir, '/');
#ifdef _WIN32
    if (!last_sep) {
        last_sep = strrchr(dir, '\\');
    }
#endif
    
    if (last_sep) {
        *last_sep = '\0';
    }
    
    return 0;
}

/* Get file extension */
const char* lanshare_file_get_ext(const char* path)
{
    if (!path) {
        return NULL;
    }
    
    const char* name = lanshare_file_get_name(path);
    if (!name) {
        return NULL;
    }
    
    const char* ext = strrchr(name, '.');
    return ext ? ext + 1 : NULL;
}

/* Generate unique filename if file exists */
int lanshare_file_get_unique_path(const char* dir_path, const char* filename,
                                   char* out_path, size_t out_size)
{
    if (!dir_path || !filename || !out_path || out_size == 0) {
        return -1;
    }
    
    /* Construct initial path */
    if (strlen(dir_path) + 1 + strlen(filename) + 1 > out_size) {
        return -1;
    }
    
    snprintf(out_path, out_size, "%s/%s", dir_path, filename);
    
    /* Check if exists */
    if (!lanshare_file_exists(out_path)) {
        return 0;  /* File doesn't exist, no modification needed */
    }
    
    /* Generate unique name with counter */
    const char* ext = lanshare_file_get_ext(filename);
    char base[256];
    
    if (ext) {
        /* Has extension */
        size_t base_len = strlen(filename) - strlen(ext) - 1;
        if (base_len >= sizeof(base)) {
            base_len = sizeof(base) - 1;
        }
        strncpy(base, filename, base_len);
        base[base_len] = '\0';
        
        /* Try counter */
        for (int i = 1; i < 1000; i++) {
            char temp_path[512];
            snprintf(temp_path, sizeof(temp_path), "%s/%s (%d).%s", 
                     dir_path, base, i, ext);
            
            if (!lanshare_file_exists(temp_path)) {
                strncpy(out_path, temp_path, out_size - 1);
                out_path[out_size - 1] = '\0';
                return 0;
            }
        }
    } else {
        /* No extension */
        strncpy(base, filename, sizeof(base) - 1);
        base[sizeof(base) - 1] = '\0';
        
        /* Try counter */
        for (int i = 1; i < 1000; i++) {
            char temp_path[512];
            snprintf(temp_path, sizeof(temp_path), "%s/%s (%d)", 
                     dir_path, base, i);
            
            if (!lanshare_file_exists(temp_path)) {
                strncpy(out_path, temp_path, out_size - 1);
                out_path[out_size - 1] = '\0';
                return 0;
            }
        }
    }
    
    return -1;  /* Failed to generate unique path */
}
