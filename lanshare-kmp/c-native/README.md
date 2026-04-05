# LAN-Share Native C Library

这是一个纯C语言实现的LAN-Share底层库，用于设备发现和数据传输。该库完全独立于Qt，可以在任何平台（包括Android）上使用。

## 特性

- **跨平台**: 支持 Linux、macOS、Windows、Android
- **零依赖**: 仅依赖标准C库和可选的 mDNS/NNG 库
- **JNI友好**: 可通过JNI与Android Java/Kotlin代码无缝对接
- **事件驱动**: 基于回调的异步接口
- **纯C API**: 简单、清晰的C接口设计

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Qt UI Layer (optional)                   │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│           C Native Library (core)                           │
│  ┌─────────────────┐  ┌─────────────────┐                  │
│  │  mDNS Module    │  │  NNG Module     │                  │
│  │  - Deviceadv.   │  │  - Upload       │                  │
│  │  - Discover     │  │  - Download     │                  │
│  └─────────────────┘  └─────────────────┘                  │
└─────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────┐
│              Java/Kotlin (Android)                          │
│              JNI Bridge Layer                               │
└─────────────────────────────────────────────────────────────┘
```

## 编译

### 前置要求

- CMake 3.10+
- C编译器（GCC、Clang、MSVC）
- NNG库（可选，用于数据传输）
- Avahi或DNS-SDK（可选，用于mDNS发现）

### Linux/Unix (使用Avahi)

```bash
# 安装依赖
sudo apt install libavahi-client-dev libnng-dev

# 编译
cd c-native
mkdir build && cd build
cmake -DLANSHARE_ENABLE_MDNS=ON -DLANSHARE_ENABLE_TRANSFER=ON ..
make

# 安装
sudo make install
```

### Windows (使用DNS-SDK)

```bash
# 下载并安装 Bonjour SDK
# 然后编译
cd c-native
mkdir build && cd build
cmake -DLANSHARE_ENABLE_MDNS=ON -DLANSHARE_ENABLE_TRANSFER=ON ..
cmake --build . --config Release
```

### Android

```bash
# 在Android项目中添加
add_subdirectory(c-native)

# 链接库
target_link_libraries(your-app lanshare)
```

## 使用方法

### 设备发现 (mDNS)

```c
#include "lanshare_mdns.h"

// 初始化
lanshare_mdns_handle_t handle = lanshare_mdns_init("_lanshare._tcp", 17116);

// 广播自己的设备
lanshare_device_t device = {
    .id = "device-123",
    .name = "My Device",
    .os_name = "Linux",
    .ip_address = "192.168.1.100"
};
lanshare_mdns_start_broadcaster(handle, &device);

// 发现其他设备
lanshare_mdns_start_discoverer(handle, callback, user_data);

// 清理
lanshare_mdns_cleanup(handle);
```

### 数据传输 (NNG)

```c
#include "lanshare_transfer.h"

// 上传文件
lanshare_upload_handle_t upload = lanshare_upload_create(
    "192.168.1.101", 17116, "/path/to/file", NULL
);

lanshare_upload_start(upload, callback, user_data);

// 下载文件（服务器模式）
lanshare_download_server_start(17116, callback, user_data);

// 清理
lanshare_upload_destroy(upload);
```

### JNI (Android)

```java
import com.lnan.share.LanShareNative;

// 加载库
System.loadLibrary("lanshare");

// 广播设备
LanShareNative.DeviceInfo device = new LanShareNative.DeviceInfo();
device.id = "device-123";
device.name = "My Android";
device.osName = "Android";
device.ipAddress = "192.168.1.100";
LanShareNative.mdnsStartBroadcaster(device);

// 上传文件
long handle = LanShareNative.uploadCreate("192.168.1.101", 17116, "/path/to/file", null);
LanShareNative.uploadStart(handle);
```

## API参考

### 头文件

- `lanshare.h` - 统一导出头文件
- `lanshare_protocol.h` - 协议定义
- `lanshare_mdns.h` - mDNS发现API
- `lanshare_transfer.h` - 传输API

### 主要函数

#### mDNS
- `lanshare_mdns_init()` - 初始化mDNS模块
- `lanshare_mdns_start_broadcaster()` - 开始广播
- `lanshare_mdns_start_discoverer()` - 开始发现
- `lanshare_mdns_cleanup()` - 清理资源

#### 传输
- `lanshare_upload_create()` - 创建上传任务
- `lanshare_upload_start()` - 开始上传
- `lanshare_download_server_start()` - 启动下载服务器
- `lanshare_get_transfer_info()` - 获取传输信息

## 协议

### 帧结构

```
┌─────────────────────────────────────┐
│  Magic (4 bytes)     : 0x4C414E53  │
│  Version (1 byte)    : 1           │
│  Type (1 byte)       : 0x01-0x06   │
│  Flags (2 bytes)     : 0           │
│  Data Length (8 bytes)             │
│  Timestamp (8 bytes)               │
├─────────────────────────────────────┤
│  Payload (variable length)          │
└─────────────────────────────────────┘
```

### 包类型

- `0x01` - Header (文件元信息，JSON格式)
- `0x02` - Data (文件数据)
- `0x03` - Finish (传输完成)
- `0x04` - Cancel (取消传输)
- `0x05` - Pause (暂停)
- `0x06` - Resume (恢复)

## 目录结构

```
c-native/
├── include/          # 头文件
│   ├── lanshare.h           # 统一导出
│   ├── lanshare_protocol.h  # 协议
│   ├── lanshare_mdns.h      # mDNS API
│   └── lanshare_transfer.h  # 传输API
├── mdns/            # mDNS实现
│   ├── mdns_avahi.c     # Avahi后端
│   ├── mdns_dns_sd.c    # DNS-SD后端
│   └── mdns_common.c    # 公共实现
├── transfer/        # 传输实现
│   ├── transfer_context.c    # 上下文管理
│   ├── transfer_channels.c   # NNG通道
│   ├── transfer_protocol.c   # 协议编解码
│   ├── transfer_file.c       # 文件操作
│   ├── upload.c             # 上传逻辑
│   └── download.c           # 下载逻辑
└── jni/            # JNI桥接
    └── jni_bridge.c
```

## License

MIT License

## 贡献

欢迎提交Issue和Pull Request！
