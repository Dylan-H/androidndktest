cmake_minimum_required(VERSION 3.22.1)

project(LANShareNative C CXX)
set(CMAKE_PREFIX_PATH "${PROJECT_SOURCE_DIR}/cmakelib_android/arm64-v8a")
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(NATIVE_SOURCE_DIR ${PROJECT_SOURCE_DIR})
message(STATUS "CMAKE_PREFIX_PATH:${CMAKE_PREFIX_PATH}")
# 启用传输和mDNS
set(LANSHARE_ENABLE_TRANSFER ON)
set(LANSHARE_ENABLE_MDNS ON)

# 查找 NNG 库 (Android 预编译库)
find_package(nng REQUIRED CONFIG)
if(nng_FOUND)
    message(STATUS "nng found")
    message(STATUS "  Include dirs: ${NNG_INCLUDE_DIRS}")
    message(STATUS "  Library target: ${NNG_LIBRARY}")

endif()

# mDNS库 (使用 mjansson/mdns)
set(MDNS_DIR ${NATIVE_SOURCE_DIR}/mdns)
set(MDNS_SOURCES
        ${MDNS_DIR}/MDNSManager.cpp  # LAN-Share mDNS 适配层 (C++17)
        ${MDNS_DIR}/lanshare_log.cpp  # Logging implementation
)

# Transfer 实现 (NNG only)
set(TRANSFER_DIR ${NATIVE_SOURCE_DIR}/transfer)
if(LANSHARE_ENABLE_TRANSFER)
    message(STATUS "Using NNG for file transfer")
    set(TRANSFER_SOURCES
            ${TRANSFER_DIR}/transfer_file.c
            ${TRANSFER_DIR}/transfer_nng.c
    )
endif()

# JNI实现
set(JNI_SOURCES
        ${NATIVE_SOURCE_DIR}/jni/jni_bridge.c
)

# 添加include目录
include_directories(
        ${NATIVE_SOURCE_DIR}/include
        ${MDNS_DIR}
        ${NATIVE_SOURCE_DIR}/external/mdns
        ${TRANSFER_DIR}
)


# 编译共享库
add_library(
        lanshare_native
        SHARED
        ${MDNS_SOURCES}
        ${TRANSFER_SOURCES}
        ${JNI_SOURCES}
)

# 设置编译选项
target_compile_options(lanshare_native PRIVATE
        -Wall
        -Wextra
)

# 链接系统库 (添加 socket 库)
target_link_libraries(
        lanshare_native
        log
)

if(LANSHARE_ENABLE_TRANSFER)
    target_link_libraries(lanshare_native nng::nng)
endif()

# C++17 支持
set_target_properties(lanshare_native PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
)
