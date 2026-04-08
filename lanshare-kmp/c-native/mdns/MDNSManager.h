/*
    LAN-Share mDNS Manager Header
    C++17 implementation with proper encapsulation
    
    Copyright (C) 2026 LAN-Share Project
*/

#ifndef MDNS_MANAGER_H
#define MDNS_MANAGER_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <netdb.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include "mdns.h"
#include "lanshare_protocol.h"
#include "lanshare_mdns.h"

namespace lanshare {
namespace mdns {

constexpr int MAX_DISCOVERED_DEVICES = 50;
constexpr int MAX_SERVICE_INSTANCES = 50;
constexpr const char* VERSION = "1.0.0-mdns";
constexpr int DEFAULT_SERVICE_PORT = 17116;

struct ServiceDiscovery {
    std::string instance_name;
    std::string hostname;
    int port = 0;
    struct sockaddr_in addr{};
    bool has_srv = false;
    bool has_a = false;
    bool has_txt = false;
    char os_name[64] = {0};
};

struct service_t {
    mdns_string_t service;
    mdns_string_t hostname;
    mdns_string_t service_instance;
    mdns_string_t hostname_qualified;
    struct sockaddr_in address_ipv4;
    struct sockaddr_in6 address_ipv6;
    int port;
    mdns_record_t record_ptr;
    mdns_record_t record_srv;
    mdns_record_t record_a;
    mdns_record_t record_aaaa;
    mdns_record_t txt_record[3];
    char os_name_buffer[64];
};

class MDNSManager {
public:
    MDNSManager();
    ~MDNSManager();
    
    // Non-copyable
    MDNSManager(const MDNSManager&) = delete;
    MDNSManager& operator=(const MDNSManager&) = delete;
    
    // Movable
    MDNSManager(MDNSManager&&) = default;
    MDNSManager& operator=(MDNSManager&&) = default;
    
    int startDiscoverer(lanshare_device_callback_t callback, void* user_data);
    void stopDiscoverer();
    
    int startBroadcaster(const lanshare_device_t* device, const char* service_type, int port);
    void stopBroadcaster();
    
    const char* getDiscoveredDevices(char* buffer, size_t buffer_size);

    int processResponses();
    
    int serviceMDNS(const std::string& hostname, 
                   const std::string& service_name, 
                   int service_port,
                   const std::string& os_name);

private:
    ServiceDiscovery* findOrCreateService(const std::string& instance_name);
    void checkAndAddDevice(ServiceDiscovery* svc);
    int openClientSockets(std::vector<int>& sockets, int max_sockets, int port);
    int openServiceSockets(std::vector<int>& sockets, int max_sockets);
    int queryService(const std::string& service_name, 
                    const std::vector<int>& sockets,
                    void* buffer, size_t capacity);
    
    static int recordCallback(int sock, const struct sockaddr* from, size_t addrlen,
                             mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                             uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                             size_t name_offset, size_t name_length, size_t record_offset,
                             size_t record_length, void* user_data);
    
    static int serviceCallback(int sock, const struct sockaddr* from, size_t addrlen,
                              mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                              uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                              size_t name_offset, size_t name_length, size_t record_offset,
                              size_t record_length, void* user_data);
    
    static std::string ipv4AddressToString(const struct sockaddr_in* addr);
    static std::string ipv6AddressToString(const struct sockaddr_in6* addr);
    static std::string ipAddressToString(const struct sockaddr* addr);

    std::vector<lanshare_device_t> devices_;
    std::vector<ServiceDiscovery> discovered_services_;
    std::vector<std::string> service_instances_;
    int mdns_sock_ = -1;

    mutable std::mutex mutex_;
    
    // Device discovery callback
    lanshare_device_callback_t device_callback_ = nullptr;
    void* device_callback_user_data_ = nullptr;
    

    static thread_local char sendbuffer_[1024];
    static thread_local struct sockaddr_in service_address_ipv4_;
    static thread_local struct sockaddr_in6 service_address_ipv6_;
    static thread_local bool has_ipv4_;
    static thread_local bool has_ipv6_;
    static thread_local volatile sig_atomic_t running_;
};

} // namespace mdns
} // namespace lanshare

#endif // MDNS_MANAGER_H
