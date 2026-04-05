cmake_minimum_required(VERSION 3.22.1)

project(LANShareNative C CXX)

# 定义源文件 - 相对于 composeApp/src/androidMain/jni 的路径
# 从 jni/ 到 c-native/ 的路径: ../../../../../c-native
set(NATIVE_SOURCE_DIR ${CMAKE_SOURCE_DIR}/../../../../c-native)

# 启用传输和mDNS
set(LANSHARE_ENABLE_TRANSFER ON)
set(LANSHARE_ENABLE_MDNS ON)

# mDNS库 (使用 mjansson/mdns)
set(MDNS_DIR ${NATIVE_SOURCE_DIR}/mdns)
set(MDNS_SOURCES
    ${MDNS_DIR}/MDNSManager.cpp  # LAN-Share mDNS 适配层 (C++17)
    ${MDNS_DIR}/lanshare_log.cpp  # Logging implementation
    #${NATIVE_SOURCE_DIR}/external/mdns/mdns.c  # mjansson/mdns library
)

# Transfer 实现 (实际的文件名)
set(TRANSFER_DIR ${NATIVE_SOURCE_DIR}/transfer)
set(TRANSFER_SOURCES
    ${TRANSFER_DIR}/transfer_file.c
    ${TRANSFER_DIR}/transfer_protocol.c
    ${TRANSFER_DIR}/transfer_socket.c
)

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

# C++17 支持
set_target_properties(lanshare_native PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
