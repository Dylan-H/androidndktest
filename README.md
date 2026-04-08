# LAN Share KMP (Kotlin Multiplatform)

Kotlin Multiplatform version of LAN Share for Android

## Project Structure

```
lanshare-kmp/
├── build.gradle.kts          # Root build configuration
├── settings.gradle.kts       # Gradle settings
├── gradle.properties         # Gradle properties
├── gradle/
│   ├── libs.versions.toml    # Dependency versions
│   └── wrapper/              # Gradle wrapper
├── composeApp/
│   ├── build.gradle.kts      # KMP configuration
│   └── src/
│       ├── commonMain/       # Common code
│       │   └── kotlin/
│       │       └── com/lnan/lanshare/
│       │           ├── App.kt
│       │           ├── Platform.kt
│       │           ├── model/    # Data models
│       │           ├── ui/       # UI screens
│       │           └── network/  # Network layer
│       ├── androidMain/      # Android-specific code
│       │   ├── kotlin/
│       │   │   └── com/lnan/lanshare/
│       │   │       ├── MainActivity.kt
│       │   │       ├── Platform.android.kt
│       │   │       ├── network/
│       │   │       └── ndk/      # NDK bindings
│       │   ├── jni/            # Native C code
│       │   │   ├── CMakeLists.txt
│       │   │   └── *.h
│       │   └── res/            # Android resources
│       │       ├── values/
│       │       ├── mipmap/
│       │       └── ...
│       └── c-native/           # C/C++ native library (separate)
│           ├── mdns/
│           │   ├── MDNSManager.h    # C++17 mDNS manager header
│           │   └── MDNSManager.cpp  # C++17 mDNS manager implementation
│           ├── transfer/
│           ├── jni/
│           └── include/
```

## Building

```bash
cd lanshare-kmp
./gradlew assembleDebug
```

## Running

```bash
./gradlew installDebug
```

## Prerequisites

- Android SDK (API 24+)
- Android NDK r26+
- CMake 3.22+
- Kotlin Multiplatform plugin
- Java 17+ (for Kotlin 2.3.x, or Java 11 for Kotlin 2.1.x)

## Network Requirements

**Important**: This project requires access to Maven Central and Gradle Plugin Portal to download dependencies. If you encounter TLS handshake errors when building:

### Solution 1: Configure Gradle with custom TLS protocols

Add to `gradle.properties`:
```properties
org.gradle.jvmargs=-Xmx4096M -Dfile.encoding=UTF-8 -Dhttps.protocols=TLSv1.2,TLSv1.3
```

### Solution 2: Use a VPN or proxy

If your network blocks or downgrades TLS connections to Maven Central, use a VPN or configure Gradle to use a proxy.

### Solution 3: Use a Maven Mirror

Configure a Maven mirror in `settings.gradle.kts`:
```kotlin
pluginManagement {
    repositories {
        // Add Aliyun mirror for China
        maven("https://maven.aliyun.com/repository/public")
        maven("https://maven.aliyun.com/repository/gradle-plugin")
    }
}
```

### Solution 4: Use cached dependencies

If you've built successfully before, Gradle may cache the plugins. Try:
```bash
./gradlew --offline projects
```

## Features

- mDNS device discovery
- NNG-based file transfer
- Cross-platform support (Android, iOS, JVM)

## Native Library

The project uses the C native library from `c-native/` directory which provides:
- mDNS/DNS-SD device discovery  
- NNG-based reliable messaging

## Issues

For TLS handshake failures, see [Gradle SSL Handbook](https://docs.gradle.org/8.14.3/userguide/build_environment.html#sec:gradle_system_properties)
