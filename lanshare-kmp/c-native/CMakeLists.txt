# LAN-Share Native Library CMake Configuration
# Configurable variables:
#   LANSHARE_ENABLE_MDNS - Enable mDNS support (default: ON)
#   LANSHARE_ENABLE_TRANSFER - Enable socket transfer (default: ON)
#   LANSHARE_ENABLE_TESTS - Enable tests (default: OFF)

cmake_minimum_required(VERSION 3.10)
project(LANShareNative C CXX)

# Options
option(LANSHARE_ENABLE_MDNS "Enable mDNS device discovery" ON)
option(LANSHARE_ENABLE_TRANSFER "Enable socket data transfer" ON)
option(LANSHARE_ENABLE_TESTS "Enable tests" OFF)

# Source files
set(LANSHARE_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lanshare.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lanshare_protocol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lanshare_mdns.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/lanshare_transfer.h
)

# mDNS implementation using mjansson/mdns (cross-platform)
set(LANSHARE_MDNS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/mdns/MDNSManager.cpp  # C++17 mDNS manager
    ${CMAKE_CURRENT_SOURCE_DIR}/mdns/lanshare_log.cpp  # Logging implementation
)

# Transfer implementation
set(LANSHARE_TRANSFER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/transfer/transfer_file.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transfer/transfer_protocol.c
    ${CMAKE_CURRENT_SOURCE_DIR}/transfer/transfer_socket.c
)

# JNI sources
set(LANSHARE_JNI_SOURCES)
if(LANSHARE_ENABLE_JNI)
    message(STATUS "Building with JNI support")
    set(LANSHARE_JNI_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/jni/jni_bridge.c
    )
    
    find_package(Java REQUIRED)
    find_package(JNI REQUIRED)
    include_directories(${JNI_INCLUDE_DIRS})
endif()

# Combine all sources
set(LANSHARE_SOURCES
    ${LANSHARE_MDNS_SOURCES}
    ${LANSHARE_TRANSFER_SOURCES}
    ${LANSHARE_JNI_SOURCES}
)

# Create shared library
add_library(lanshare SHARED ${LANSHARE_SOURCES})

# Include directories
target_include_directories(lanshare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/mdns
)

# Link libraries
target_link_libraries(lanshare
    ${CMAKE_DL_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
)

# C++17 support
set_target_properties(lanshare PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# mDNS backend: mjansson/mdns is cross-platform, no additional configuration needed

# Add preprocessor definitions to avoid winsock conflicts on Windows
if(MSVC)
    target_compile_definitions(lanshare PRIVATE
        _WINSOCK_DEPRECATED_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
    )
endif()

# Install rules
install(TARGETS lanshare
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${LANSHARE_PUBLIC_HEADERS}
    DESTINATION include
)

# Test support
if(LANSHARE_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
