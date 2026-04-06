/*
    LAN-Share mDNS Implementation using mjansson/mdns library
    C++17 implementation with proper encapsulation
    
    Copyright (C) 2026 LAN-Share Project
*/

#include "MDNSManager.h"
#include "lanshare_log.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#define usleep(milliseconds) Sleep((milliseconds) / 1000)
#define _CRT_SECURE_NO_WARNINGS 1
#define sleep(x) Sleep(x * 1000)
#else
#include <sys/time.h>
#endif

#include <cerrno>
#include <csignal>

namespace lanshare {
namespace mdns {

// Static member definitions
//thread_local char MDNSManager::addrbuffer_[64];
//thread_local char MDNSManager::entrybuffer_[256];
//thread_local char MDNSManager::namebuffer_[256];
thread_local char MDNSManager::sendbuffer_[1024];
//thread_local mdns_record_txt_t MDNSManager::txtbuffer_[128];
thread_local struct sockaddr_in MDNSManager::service_address_ipv4_{};
thread_local struct sockaddr_in6 MDNSManager::service_address_ipv6_{};
thread_local bool MDNSManager::has_ipv4_ = false;
thread_local bool MDNSManager::has_ipv6_ = false;
thread_local volatile sig_atomic_t MDNSManager::running_ = 1;

MDNSManager::MDNSManager() = default;

MDNSManager::~MDNSManager() {
    if (mdns_sock_ >= 0) {
        mdns_socket_close(mdns_sock_);
        mdns_sock_ = -1;
    }
}

std::string MDNSManager::ipv4AddressToString(const struct sockaddr_in* addr) {
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    int ret = getnameinfo((const struct sockaddr*)addr, sizeof(struct sockaddr_in),
                         host, NI_MAXHOST, service, NI_MAXSERV,
                         NI_NUMERICSERV | NI_NUMERICHOST);
    if (ret == 0) {
        if (addr->sin_port != 0) {
            return std::string(host) + ":" + service;
        }
        return std::string(host);
    }
    return "";
}

std::string MDNSManager::ipv6AddressToString(const struct sockaddr_in6* addr) {
    char host[NI_MAXHOST] = {0};
    char service[NI_MAXSERV] = {0};
    int ret = getnameinfo((const struct sockaddr*)addr, sizeof(struct sockaddr_in6),
                         host, NI_MAXHOST, service, NI_MAXSERV,
                         NI_NUMERICSERV | NI_NUMERICHOST);
    if (ret == 0) {
        if (addr->sin6_port != 0) {
            return "[" + std::string(host) + "]:" + service;
        }
        return std::string(host);
    }
    return "";
}

std::string MDNSManager::ipAddressToString(const struct sockaddr* addr) {
    if (addr->sa_family == AF_INET6) {
        return ipv6AddressToString((const struct sockaddr_in6*)addr);
    }
    return ipv4AddressToString((const struct sockaddr_in*)addr);
}

int MDNSManager::recordCallback(int sock, const struct sockaddr* from, size_t addrlen,
                               mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                               uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                               size_t name_offset, size_t name_length, size_t record_offset,
                               size_t record_length, void* user_data)
{
    (void)from;
    (void)addrlen;
    (void)query_id;
    (void)rclass;
    (void)ttl;
    (void)name_length;
    
    if (entry != MDNS_ENTRYTYPE_ANSWER) {
        return 0;
    }

    auto* manager = static_cast<MDNSManager*>(user_data);
    if (!manager) return 0;

    if (rtype == MDNS_RECORDTYPE_PTR) {
        char service_name[256];
        mdns_string_t namestr = mdns_record_parse_ptr(data, size, record_offset, record_length, 
                             service_name, sizeof(service_name));
        
        if (strstr(service_name, "_lanshare._tcp")) {
            auto* svc = manager->findOrCreateService(service_name);
            
            size_t capacity = 2048;
            auto buffer = std::make_unique<char[]>(capacity);
            if (buffer) {
                int srv_query_id = mdns_query_send(sock, MDNS_RECORDTYPE_SRV,  
                                                namestr.str, namestr.length,  
                                                buffer.get(), capacity, 0);  
                if (srv_query_id >= 0) {  
                    LOGD("MDNSManager", "Sent SRV query for %.*s", MDNS_STRING_FORMAT(namestr));
                }
            }
        }
        return 0;
    }
    
    if (rtype == MDNS_RECORDTYPE_SRV) {
        mdns_record_srv_t srv;
        char hostname[256];
        
        srv = mdns_record_parse_srv(data, size, record_offset, record_length,
                                   hostname, sizeof(hostname));
        
        char entry_name[256];
        mdns_string_extract(data, size, &name_offset, entry_name, sizeof(entry_name));
        
        auto* svc = manager->findOrCreateService(entry_name);
        if (svc) {
            svc->hostname = hostname;
            svc->port = srv.port;
            svc->has_srv = true;
            
            size_t capacity = 2048;
            auto buffer = std::make_unique<char[]>(capacity);
            if (buffer) {
                int a_query_id = mdns_query_send(sock, MDNS_RECORDTYPE_A,
                                               srv.name.str, srv.name.length,
                                               buffer.get(), capacity, 0);
                if (a_query_id >= 0) {
                    LOGD("MDNSManager", "Sent A query for %.*s", MDNS_STRING_FORMAT(srv.name));
                }
            }
        }
        return 0;
    }
    
    if (rtype == MDNS_RECORDTYPE_A) {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        
        struct sockaddr_in* sin = mdns_record_parse_a(data, size, record_offset, record_length, &addr);
        if (sin) {
            char entry_name[256];
            mdns_string_extract(data, size, &name_offset, entry_name, sizeof(entry_name));
            
            for (auto& svc : manager->discovered_services_) {
                LOGD("MDNSManager", "Comparing %s with %s", entry_name, svc.hostname.c_str());
                if (svc.hostname == entry_name) {
                    svc.addr = *sin;
                    svc.has_a = true;
                    manager->checkAndAddDevice(&svc);
                    break;
                }
            }
        }
    }
    
    return 0;
}

ServiceDiscovery* MDNSManager::findOrCreateService(const std::string& instance_name) {
    for (auto& svc : discovered_services_) {
        if (svc.instance_name == instance_name) {
            return &svc;
        }
    }
    
    if (discovered_services_.size() < MAX_SERVICE_INSTANCES) {
        ServiceDiscovery svc;
        svc.instance_name = instance_name;
        discovered_services_.push_back(svc);
        return &discovered_services_.back();
    }
    return nullptr;
}

void MDNSManager::checkAndAddDevice(ServiceDiscovery* svc) {
    LOGI("MDNSManager", "Checking and adding device %d %d", svc->has_srv, svc->has_a);
    if (!svc || !svc->has_srv || !svc->has_a) {
        return;
    }
    
    char ip_str[64];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
            (unsigned char)(svc->addr.sin_addr.s_addr & 0xFF),
            (unsigned char)((svc->addr.sin_addr.s_addr >> 8) & 0xFF),
            (unsigned char)((svc->addr.sin_addr.s_addr >> 16) & 0xFF),
            (unsigned char)((svc->addr.sin_addr.s_addr >> 24) & 0xFF));
    LOGI("MDNSManager", "Checking and adding device2");
    //std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& device : devices_) {
        if (std::strcmp(device.ip_address, ip_str) == 0) {
            LOGI("MDNSManager", "Device already exists at %s", ip_str);
            return;
        }
    }
    
    if (devices_.size() < MAX_DISCOVERED_DEVICES) {
        lanshare_device_t device{};
        LOGI("MDNSManager", "Checking and adding device3 %s",svc->instance_name.c_str());
        snprintf(device.id, sizeof(device.id), "dev_%s", ip_str);
        
        const char* dot = strstr(svc->instance_name.c_str(), "._lanshare._tcp");
        if (dot) {
            size_t len = dot - svc->instance_name.c_str();
            if (len > sizeof(device.name) - 1) len = sizeof(device.name) - 1;
            memcpy(device.name, svc->instance_name.c_str(), len);
            device.name[len] = '\0';
        } else {
            strncpy(device.name, svc->instance_name.c_str(), sizeof(device.name) - 1);
        }
        
        strncpy(device.os_name, "Unknown", sizeof(device.os_name) - 1);
        strncpy(device.ip_address, ip_str, sizeof(device.ip_address) - 1);
        
        LOGI("MDNSManager", "Discovered device: %s at %s:%d", device.name, ip_str, svc->port);
        
        devices_.push_back(device);
        
        // Notify via callback if registered
        if (device_callback_) {
            LOGI("MDNSManager", "Calling device callback for %s", device.name);
            device_callback_(&device, device_callback_user_data_);
        }
    }
}

int MDNSManager::openClientSockets(std::vector<int>& sockets, int max_sockets, int port) {
    sockets.clear();
    has_ipv4_ = false;
    has_ipv6_ = false;

#ifdef _WIN32
    IP_ADAPTER_ADDRESSES* adapter_address = nullptr;
    ULONG address_size = 8000;
    unsigned int ret;
    unsigned int num_retries = 4;
    do {
        adapter_address = (IP_ADAPTER_ADDRESSES*)malloc(address_size);
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST, 
                                  nullptr, adapter_address, &address_size);
        if (ret == ERROR_BUFFER_OVERFLOW) {
            free(adapter_address);
            adapter_address = nullptr;
            address_size *= 2;
        } else {
            break;
        }
    } while (num_retries-- > 0);

    if (!adapter_address || (ret != NO_ERROR)) {
        free(adapter_address);
        LOGE("MDNSManager", "Failed to get network adapter addresses");
        return 0;
    }

    bool first_ipv4 = true;
    bool first_ipv6 = true;
    for (PIP_ADAPTER_ADDRESSES adapter = adapter_address; adapter; adapter = adapter->Next) {
        if (adapter->TunnelType == TUNNEL_TYPE_TEREDO)
            continue;
        if (adapter->OperStatus != IfOperStatusUp)
            continue;

        for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast;
             unicast = unicast->Next) {
            if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
                struct sockaddr_in* saddr = (struct sockaddr_in*)unicast->Address.lpSockaddr;
                if ((saddr->sin_addr.S_un.S_un_b.s_b1 != 127) ||
                    (saddr->sin_addr.S_un.S_un_b.s_b2 != 0) ||
                    (saddr->sin_addr.S_un.S_un_b.s_b3 != 0) ||
                    (saddr->sin_addr.S_un.S_un_b.s_b4 != 1)) {
                    bool log_addr = false;
                    if (first_ipv4) {
                        service_address_ipv4_ = *saddr;
                        first_ipv4 = false;
                        log_addr = true;
                    }
                    has_ipv4_ = true;
                    if ((int)sockets.size() < max_sockets) {
                        saddr->sin_port = htons((unsigned short)port);
                        int sock = mdns_socket_open_ipv4(saddr);
                        if (sock >= 0) {
                            sockets.push_back(sock);
                            log_addr = true;
                        }
                    }
                    if (log_addr) {
                        std::string addr = ipv4AddressToString(saddr);
                        LOGI("MDNSManager", "Local IPv4 address: %s", addr.c_str());
                    }
                }
            } else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
                struct sockaddr_in6* saddr = (struct sockaddr_in6*)unicast->Address.lpSockaddr;
                if (saddr->sin6_scope_id)
                    continue;
                static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                          0, 0, 0, 0, 0, 0, 0, 1};
                static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
                                                                 0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
                if ((unicast->DadState == NldsPreferred) &&
                    memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
                    memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
                    bool log_addr = false;
                    if (first_ipv6) {
                        service_address_ipv6_ = *saddr;
                        first_ipv6 = false;
                        log_addr = true;
                    }
                    has_ipv6_ = true;
                    if ((int)sockets.size() < max_sockets) {
                        saddr->sin6_port = htons((unsigned short)port);
                        int sock = mdns_socket_open_ipv6(saddr);
                        if (sock >= 0) {
                            sockets.push_back(sock);
                            log_addr = true;
                        }
                    }
                    if (log_addr) {
                        std::string addr = ipv6AddressToString(saddr);
                        LOGI("MDNSManager", "Local IPv6 address: %s", addr.c_str());
                    }
                }
            }
        }
    }

    free(adapter_address);
#else
    struct ifaddrs* ifaddr = nullptr;
    struct ifaddrs* ifa = nullptr;

    if (getifaddrs(&ifaddr) < 0)
        LOGW("MDNSManager", "Unable to get interface addresses");

    bool first_ipv4 = true;
    bool first_ipv6 = true;
    for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr)
            continue;
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_MULTICAST))
            continue;
        if ((ifa->ifa_flags & IFF_LOOPBACK) || (ifa->ifa_flags & IFF_POINTOPOINT))
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in* saddr = (struct sockaddr_in*)ifa->ifa_addr;
            if (saddr->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) {
                bool log_addr = false;
                if (first_ipv4) {
                    service_address_ipv4_ = *saddr;
                    first_ipv4 = false;
                    log_addr = true;
                }
                has_ipv4_ = true;
                if ((int)sockets.size() < max_sockets) {
                    saddr->sin_port = htons(port);
                    int sock = mdns_socket_open_ipv4(saddr);
                    if (sock >= 0) {
                        sockets.push_back(sock);
                        log_addr = true;
                    }
                }
                if (log_addr) {
                    std::string addr = ipv4AddressToString(saddr);
                    LOGI("MDNSManager", "Local IPv4 address: %s", addr.c_str());
                }
            }
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            struct sockaddr_in6* saddr = (struct sockaddr_in6*)ifa->ifa_addr;
            if (saddr->sin6_scope_id)
                continue;
            static const unsigned char localhost[] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                      0, 0, 0, 0, 0, 0, 0, 1};
            static const unsigned char localhost_mapped[] = {0, 0, 0,    0,    0,    0, 0, 0,
                                                             0, 0, 0xff, 0xff, 0x7f, 0, 0, 1};
            if (memcmp(saddr->sin6_addr.s6_addr, localhost, 16) &&
                memcmp(saddr->sin6_addr.s6_addr, localhost_mapped, 16)) {
                bool log_addr = false;
                if (first_ipv6) {
                    service_address_ipv6_ = *saddr;
                    first_ipv6 = false;
                    log_addr = true;
                }
                has_ipv6_ = true;
                if ((int)sockets.size() < max_sockets) {
                    saddr->sin6_port = htons(port);
                    int sock = mdns_socket_open_ipv6(saddr);
                    if (sock >= 0) {
                        sockets.push_back(sock);
                        log_addr = true;
                    }
                }
                if (log_addr) {
                    std::string addr = ipv6AddressToString(saddr);
                    LOGI("MDNSManager", "Local IPv6 address: %s", addr.c_str());
                }
            }
        }
    }

    freeifaddrs(ifaddr);
#endif

    return (int)sockets.size();
}

int MDNSManager::openServiceSockets(std::vector<int>& sockets, int max_sockets) {
    sockets.clear();
    
    openClientSockets(sockets, 0, 0);

    if ((int)sockets.size() < max_sockets) {
        struct sockaddr_in sock_addr{};
        sock_addr.sin_family = AF_INET;
#ifdef _WIN32
        sock_addr.sin_addr.s_addr = inet_addr("192.168.3.64");
#else
        sock_addr.sin_addr.s_addr = INADDR_ANY;
#endif
        sock_addr.sin_port = htons(MDNS_PORT);
#ifdef __APPLE__
        sock_addr.sin_len = sizeof(struct sockaddr_in);
#endif
        int sock = mdns_socket_open_ipv4(&sock_addr);
        if (sock >= 0)
            sockets.push_back(sock);
    }

    if ((int)sockets.size() < max_sockets) {
        struct sockaddr_in6 sock_addr{};
        sock_addr.sin6_family = AF_INET6;
        sock_addr.sin6_addr = in6addr_any;
        sock_addr.sin6_port = htons(MDNS_PORT);
#ifdef __APPLE__
        sock_addr.sin6_len = sizeof(struct sockaddr_in6);
#endif
        int sock = mdns_socket_open_ipv6(&sock_addr);
        if (sock >= 0)
            sockets.push_back(sock);
    }

    return (int)sockets.size();
}

int MDNSManager::serviceCallback(int sock, const struct sockaddr* from, size_t addrlen,
                                mdns_entry_type_t entry, uint16_t query_id, uint16_t rtype,
                                uint16_t rclass, uint32_t ttl, const void* data, size_t size,
                                size_t name_offset, size_t name_length, size_t record_offset,
                                size_t record_length, void* user_data) {
    (void)sizeof(ttl);
    if (entry != MDNS_ENTRYTYPE_QUESTION)
        return 0;

    const char dns_sd[] = "_services._dns-sd._udp.local.";
    auto* service = static_cast<struct service_t*>(user_data);
    if (!service) return 0;

    std::string fromaddrstr = ipAddressToString(from);

    size_t offset = name_offset;
    char namebuf[256];
    mdns_string_t name = mdns_string_extract(data, size, &offset, namebuf, sizeof(namebuf));

    const char* record_name = nullptr;
    if (rtype == MDNS_RECORDTYPE_PTR)
        record_name = "PTR";
    else if (rtype == MDNS_RECORDTYPE_SRV)
        record_name = "SRV";
    else if (rtype == MDNS_RECORDTYPE_A)
        record_name = "A";
    else if (rtype == MDNS_RECORDTYPE_AAAA)
        record_name = "AAAA";
    else if (rtype == MDNS_RECORDTYPE_TXT)
        record_name = "TXT";
    else if (rtype == MDNS_RECORDTYPE_ANY)
        record_name = "ANY";
    else
        return 0;
    
    LOGD("MDNSManager", "Query %s %.*s", record_name, MDNS_STRING_FORMAT(name));

    // DNS-SD domain query
    if ((name.length == (sizeof(dns_sd) - 1)) &&
        (strncmp(name.str, dns_sd, sizeof(dns_sd) - 1) == 0)) {
        if ((rtype == MDNS_RECORDTYPE_PTR) || (rtype == MDNS_RECORDTYPE_ANY)) {
            mdns_record_t answer = {};
            answer.name = name;
            answer.type = MDNS_RECORDTYPE_PTR;
            answer.data.ptr.name = service->service;

            uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
            LOGD("MDNSManager", "  --> answer %.*s (%s)", MDNS_STRING_FORMAT(answer.data.ptr.name),
                   (unicast ? "unicast" : "multicast"));

            if (unicast) {
                mdns_query_answer_unicast(sock, from, addrlen, sendbuffer_, sizeof(sendbuffer_),
                                         query_id, static_cast<mdns_record_type_t>(rtype), name.str, name.length, answer, 0, 0, 0, 0);
            } else {
                mdns_query_answer_multicast(sock, sendbuffer_, sizeof(sendbuffer_), answer, 0, 0, 0, 0);
            }
        }
    }
    // Service type query
    else if ((name.length == service->service.length) &&
             (strncmp(name.str, service->service.str, name.length) == 0)) {
        if ((rtype == MDNS_RECORDTYPE_PTR) || (rtype == MDNS_RECORDTYPE_ANY)) {
            mdns_record_t answer = service->record_ptr;

            mdns_record_t additional[5] = {0};
            size_t additional_count = 0;

            additional[additional_count++] = service->record_srv;

            if (service->address_ipv4.sin_family == AF_INET)
                additional[additional_count++] = service->record_a;
            if (service->address_ipv6.sin6_family == AF_INET6)
                additional[additional_count++] = service->record_aaaa;

            additional[additional_count++] = service->txt_record[0];
            additional[additional_count++] = service->txt_record[1];

            uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
            LOGD("MDNSManager", "  --> answer %.*s (%s)",
                   MDNS_STRING_FORMAT(service->record_ptr.data.ptr.name),
                   (unicast ? "unicast" : "multicast"));

            if (unicast) {
                mdns_query_answer_unicast(sock, from, addrlen, sendbuffer_, sizeof(sendbuffer_),
                                         query_id, static_cast<mdns_record_type_t>(rtype), name.str, name.length, answer, 0, 0,
                                         additional, additional_count);
            } else {
                mdns_query_answer_multicast(sock, sendbuffer_, sizeof(sendbuffer_), answer, 0, 0,
                                            additional, additional_count);
            }
        }
    }
    // Service instance query
    else if ((name.length == service->service_instance.length) &&
             (strncmp(name.str, service->service_instance.str, name.length) == 0)) {
        if ((rtype == MDNS_RECORDTYPE_SRV) || (rtype == MDNS_RECORDTYPE_ANY)) {
            mdns_record_t answer = service->record_srv;

            mdns_record_t additional[5] = {0};
            size_t additional_count = 0;

            if (service->address_ipv4.sin_family == AF_INET)
                additional[additional_count++] = service->record_a;
            if (service->address_ipv6.sin6_family == AF_INET6)
                additional[additional_count++] = service->record_aaaa;

            additional[additional_count++] = service->txt_record[0];
            additional[additional_count++] = service->txt_record[1];

            uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
            LOGD("MDNSManager", "  --> answer %.*s port %d (%s)",
                   MDNS_STRING_FORMAT(service->record_srv.data.srv.name), service->port,
                   (unicast ? "unicast" : "multicast"));

            if (unicast) {
                mdns_query_answer_unicast(sock, from, addrlen, sendbuffer_, sizeof(sendbuffer_),
                                         query_id, static_cast<mdns_record_type_t>(rtype), name.str, name.length, answer, 0, 0,
                                         additional, additional_count);
            } else {
                mdns_query_answer_multicast(sock, sendbuffer_, sizeof(sendbuffer_), answer, 0, 0,
                                            additional, additional_count);
            }
        }
    }
    // Hostname query (A/AAAA records)
    else if ((name.length == service->hostname_qualified.length) &&
             (strncmp(name.str, service->hostname_qualified.str, name.length) == 0)) {
        if (((rtype == MDNS_RECORDTYPE_A) || (rtype == MDNS_RECORDTYPE_ANY)) &&
            (service->address_ipv4.sin_family == AF_INET)) {
            mdns_record_t answer = service->record_a;

            mdns_record_t additional[5] = {0};
            size_t additional_count = 0;

            if (service->address_ipv6.sin6_family == AF_INET6)
                additional[additional_count++] = service->record_aaaa;

            additional[additional_count++] = service->txt_record[0];
            additional[additional_count++] = service->txt_record[1];

            uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
            std::string addrstr = ipAddressToString(
                (struct sockaddr*)&service->record_a.data.a.addr);
            LOGD("MDNSManager", "  --> answer %.*s IPv4 %s (%s)", MDNS_STRING_FORMAT(service->record_a.name),
                   addrstr.c_str(), (unicast ? "unicast" : "multicast"));

            if (unicast) {
                mdns_query_answer_unicast(sock, from, addrlen, sendbuffer_, sizeof(sendbuffer_),
                                         query_id, static_cast<mdns_record_type_t>(rtype), name.str, name.length, answer, 0, 0,
                                         additional, additional_count);
            } else {
                mdns_query_answer_multicast(sock, sendbuffer_, sizeof(sendbuffer_), answer, 0, 0,
                                            additional, additional_count);
            }
        } else if (((rtype == MDNS_RECORDTYPE_AAAA) || (rtype == MDNS_RECORDTYPE_ANY)) &&
                   (service->address_ipv6.sin6_family == AF_INET6)) {
            mdns_record_t answer = service->record_aaaa;

            mdns_record_t additional[5] = {0};
            size_t additional_count = 0;

            if (service->address_ipv4.sin_family == AF_INET)
                additional[additional_count++] = service->record_a;

            additional[additional_count++] = service->txt_record[0];
            additional[additional_count++] = service->txt_record[1];

            uint16_t unicast = (rclass & MDNS_UNICAST_RESPONSE);
            std::string addrstr = ipAddressToString(
                (struct sockaddr*)&service->record_aaaa.data.aaaa.addr);
            LOGD("MDNSManager", "  --> answer %.*s IPv6 %s (%s)",
                   MDNS_STRING_FORMAT(service->record_aaaa.name), addrstr.c_str(),
                   (unicast ? "unicast" : "multicast"));

            if (unicast) {
                mdns_query_answer_unicast(sock, from, addrlen, sendbuffer_, sizeof(sendbuffer_),
                                         query_id, static_cast<mdns_record_type_t>(rtype), name.str, name.length, answer, 0, 0,
                                         additional, additional_count);
            } else {
                mdns_query_answer_multicast(sock, sendbuffer_, sizeof(sendbuffer_), answer, 0, 0,
                                            additional, additional_count);
            }
        }
    }
    return 0;
}

int MDNSManager::serviceMDNS(const std::string& hostname, 
                            const std::string& service_name, 
                            int service_port) {
    std::vector<int> sockets;
    int num_sockets = openServiceSockets(sockets, 32);
    if (num_sockets <= 0) {
        LOGE("MDNSManager", "Failed to open any client sockets");
        return -1;
    }
    LOGI("MDNSManager", "Opened %d socket%s for mDNS service", num_sockets, num_sockets > 1 ? "s" : "");

    std::string svc_name = service_name;
    if (!svc_name.empty() && svc_name.back() != '.') {
        svc_name += '.';
    }

    LOGI("MDNSManager", "Service mDNS: %s:%d", svc_name.c_str(), service_port);
    LOGI("MDNSManager", "Hostname: %s", hostname.c_str());

    size_t capacity = 2048;
    auto buffer = std::make_unique<char[]>(capacity);

    mdns_string_t service_string = {svc_name.c_str(), svc_name.length()};
    mdns_string_t hostname_string = {hostname.c_str(), hostname.length()};

    char service_instance_buffer[256] = {0};
    snprintf(service_instance_buffer, sizeof(service_instance_buffer) - 1, "%.*s.%.*s",
            MDNS_STRING_FORMAT(hostname_string), MDNS_STRING_FORMAT(service_string));
    mdns_string_t service_instance_string = {service_instance_buffer, strlen(service_instance_buffer)};

    char qualified_hostname_buffer[256] = {0};
    snprintf(qualified_hostname_buffer, sizeof(qualified_hostname_buffer) - 1, "%.*s.local.",
            MDNS_STRING_FORMAT(hostname_string));
    mdns_string_t hostname_qualified_string = {qualified_hostname_buffer, strlen(qualified_hostname_buffer)};

    service_t service{};
    
    service.service = service_string;
    service.hostname = hostname_string;
    service.service_instance = service_instance_string;
    service.hostname_qualified = hostname_qualified_string;
    service.address_ipv4 = service_address_ipv4_;
    service.address_ipv6 = service_address_ipv6_;
    service.port = service_port;

    service.record_ptr = {.name = service.service,
                         .type = MDNS_RECORDTYPE_PTR,
                         .data = {.ptr = {.name = service.service_instance}},
                         .rclass = 0,
                         .ttl = 0};

    service.record_srv = {.name = service.service_instance,
                         .type = MDNS_RECORDTYPE_SRV,
                         .data = {.srv = {.name = service.hostname_qualified,
                                        .port = static_cast<uint16_t>(service.port),
                                        .priority = 0,
                                        .weight = 0}},
                         .rclass = 0,
                         .ttl = 0};

    service.record_a = {.name = service.hostname_qualified,
                       .type = MDNS_RECORDTYPE_A,
                       .data = {.a = {.addr = service.address_ipv4}},
                       .rclass = 0,
                       .ttl = 0};

    service.record_aaaa = {.name = service.hostname_qualified,
                          .type = MDNS_RECORDTYPE_AAAA,
                          .data = {.aaaa = {.addr = service.address_ipv6}},
                          .rclass = 0,
                          .ttl = 0};

    service.txt_record[0] = {.name = service.service_instance,
                            .type = MDNS_RECORDTYPE_TXT,
                            .data = {.txt = {.key = {MDNS_STRING_CONST("test")},
                                           .value = {MDNS_STRING_CONST("1")}}},
                            .rclass = 0,
                            .ttl = 0};
    service.txt_record[1] = {.name = service.service_instance,
                            .type = MDNS_RECORDTYPE_TXT,
                            .data = {.txt = {.key = {MDNS_STRING_CONST("other")},
                                           .value = {MDNS_STRING_CONST("value")}}},
                            .rclass = 0,
                            .ttl = 0};

    {
        LOGD("MDNSManager", "Sending announce");
        mdns_record_t additional[5] = {0};
        size_t additional_count = 0;
        additional[additional_count++] = service.record_srv;
        if (service.address_ipv4.sin_family == AF_INET)
            additional[additional_count++] = service.record_a;
        if (service.address_ipv6.sin6_family == AF_INET6)
            additional[additional_count++] = service.record_aaaa;
        additional[additional_count++] = service.txt_record[0];
        additional[additional_count++] = service.txt_record[1];

        for (int isock = 0; isock < num_sockets; ++isock)
            mdns_announce_multicast(sockets[isock], buffer.get(), capacity, service.record_ptr, 0, 0,
                                   additional, additional_count);
    }

    while (running_) {
        int nfds = 0;
        fd_set readfs;
        FD_ZERO(&readfs);
        for (int isock = 0; isock < num_sockets; ++isock) {
            if (sockets[isock] >= nfds)
                nfds = sockets[isock] + 1;
            FD_SET(sockets[isock], &readfs);
        }

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        if (select(nfds, &readfs, nullptr, nullptr, &timeout) >= 0) {
            for (int isock = 0; isock < num_sockets; ++isock) {
                if (FD_ISSET(sockets[isock], &readfs)) {
                    mdns_socket_listen(sockets[isock], buffer.get(), capacity, 
                                     serviceCallback, &service);
                }
                FD_SET(sockets[isock], &readfs);
            }
        } else {
            break;
        }
    }

    {
        LOGD("MDNSManager", "Sending goodbye");
        mdns_record_t additional[5] = {0};
        size_t additional_count = 0;
        additional[additional_count++] = service.record_srv;
        if (service.address_ipv4.sin_family == AF_INET)
            additional[additional_count++] = service.record_a;
        if (service.address_ipv6.sin6_family == AF_INET6)
            additional[additional_count++] = service.record_aaaa;
        additional[additional_count++] = service.txt_record[0];
        additional[additional_count++] = service.txt_record[1];

        for (int isock = 0; isock < num_sockets; ++isock)
            mdns_goodbye_multicast(sockets[isock], buffer.get(), capacity, service.record_ptr, 0, 0,
                                  additional, additional_count);
    }

    for (int isock = 0; isock < num_sockets; ++isock)
        mdns_socket_close(sockets[isock]);
    LOGI("MDNSManager", "Closed socket%s", num_sockets > 1 ? "s" : "");

    return 0;
}

int MDNSManager::queryService(const std::string& service_name,
                             const std::vector<int>& sockets,
                             void* buffer, size_t capacity) {
    LOGD("MDNSManager", "Sending mDNS query for %s", service_name.c_str());
    
    std::vector<int> query_ids(sockets.size());
    for (size_t isock = 0; isock < sockets.size(); ++isock) {
        query_ids[isock] = mdns_query_send(sockets[isock], MDNS_RECORDTYPE_PTR,
                                          service_name.c_str(), service_name.length(),
                                          buffer, capacity, 0);
        if (query_ids[isock] < 0) {
            LOGW("MDNSManager", "Failed to send mDNS query on socket %zu: %s", isock, strerror(errno));
        }
    }
    
    int res;
    size_t total_records = 0;
    LOGD("MDNSManager", "Reading mDNS query replies (timeout: 3 seconds)");
    do {
        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;
        
        int nfds = 0;
        fd_set readfs;
        FD_ZERO(&readfs);
        for (int sock : sockets) {
            if (sock >= nfds)
                nfds = sock + 1;
            FD_SET(sock, &readfs);
        }
        
        res = select(nfds, &readfs, nullptr, nullptr, &timeout);
        if (res > 0) {
            for (size_t isock = 0; isock < sockets.size(); ++isock) {
                if (FD_ISSET(sockets[isock], &readfs)) {
                    size_t rec = mdns_query_recv(sockets[isock], buffer, capacity,
                                                recordCallback, this, query_ids[isock]);
                    if (rec > 0)
                        total_records += rec;
                }
            }
        }
    } while (res > 0);
    
    LOGD("MDNSManager", "Received %zu records for service query", total_records);
    return 0;
}

void MDNSManager::setDeviceCallback(lanshare_device_callback_t callback, void* user_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    device_callback_ = callback;
    device_callback_user_data_ = user_data;
}

int MDNSManager::startDiscoverer(lanshare_device_callback_t callback, void* user_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Save the callback for device discovery notifications
    device_callback_ = callback;
    device_callback_user_data_ = user_data;
    
    devices_.clear();
    discovered_services_.clear();
    service_instances_.clear();

    std::vector<int> sockets;
    int num_sockets = openClientSockets(sockets, 32, 0);
    if (num_sockets <= 0) {
        LOGE("MDNSManager", "Failed to open any client sockets for mDNS discovery");
        return -1;
    }
    LOGI("MDNSManager", "Opened %d socket%s for mDNS discovery", num_sockets, num_sockets > 1 ? "s" : "");

    size_t capacity = 2048;
    auto buffer = std::make_unique<char[]>(capacity);
    if (!buffer) {
        LOGE("MDNSManager", "Failed to allocate buffer for mDNS discovery");
        for (int sock : sockets)
            mdns_socket_close(sock);
        return -1;
    }

    char service_query[256];
    snprintf(service_query, sizeof(service_query), "%s.local.", LANSHARE_MDNS_SERVICE_TYPE);
    queryService(service_query, sockets, buffer.get(), capacity);

    LOGI("MDNSManager", "Discovery complete, found %zu device(s)", devices_.size());

    for (int sock : sockets)
        mdns_socket_close(sock);
    LOGI("MDNSManager", "Closed discovery socket%s", num_sockets > 1 ? "s" : "");

    return 0;
}

void MDNSManager::stopDiscoverer() {
}

int MDNSManager::startBroadcaster(const lanshare_device_t* device, const char* service_type, int port) {
    (void)device;
    std::thread([this,service_type, port]() {
         serviceMDNS("mc", service_type ? service_type : LANSHARE_MDNS_SERVICE_TYPE, port);
    }).detach();
    return 0;
}

void MDNSManager::stopBroadcaster() {
}

const char* MDNSManager::getDiscoveredDevices(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) {
        return nullptr;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    memset(buffer, 0, buffer_size);
    size_t written = 0;
    
    written += snprintf(buffer + written, buffer_size - written, "[");
    
    for (size_t i = 0; i < devices_.size(); i++) {
        if (i > 0) {
            written += snprintf(buffer + written, buffer_size - written, ",");
        }
        
        written += snprintf(buffer + written, buffer_size - written,
            "{\"id\":\"%s\",\"name\":\"%s\",\"os\":\"%s\",\"ip\":\"%s\"}",
            devices_[i].id,
            devices_[i].name,
            devices_[i].os_name,
            devices_[i].ip_address);
    }
    
    written += snprintf(buffer + written, buffer_size - written, "]");
    
    return buffer;
}

int MDNSManager::addDevice(const lanshare_device_t* device) {
    if (!device) return -1;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (devices_.size() < MAX_DISCOVERED_DEVICES) {
        devices_.push_back(*device);
        return 0;
    }
    return -1;
}

void MDNSManager::clearDevices() {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_.clear();
}

int MDNSManager::getDeviceCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)devices_.size();
}

const lanshare_device_t* MDNSManager::getDevice(int index) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (index < 0 || index >= (int)devices_.size()) {
        return nullptr;
    }
    return &devices_[index];
}

int MDNSManager::processResponses() {
    if (mdns_sock_ < 0) {
        return -1;
    }
    
    uint8_t buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    
    size_t count = mdns_query_recv(mdns_sock_, buffer, sizeof(buffer), 
                                  recordCallback, this, 0);
    
    return (int)count;
}

static std::unique_ptr<MDNSManager> g_mdns_manager;

} // namespace mdns
} // namespace lanshare

extern "C" {

lanshare_mdns_handle_t lanshare_mdns_init() {
    auto manager = std::make_unique<lanshare::mdns::MDNSManager>();

    lanshare::mdns::g_mdns_manager = std::move(manager);
    return (lanshare_mdns_handle_t)lanshare::mdns::g_mdns_manager.get();
}

void lanshare_mdns_cleanup(lanshare_mdns_handle_t handle) {
    (void)handle;
    lanshare::mdns::g_mdns_manager.reset();
}

int lanshare_mdns_start_discoverer(lanshare_mdns_handle_t handle,
                                   lanshare_device_callback_t callback,
                                   void* user_data) {
    (void)handle;
    if (!lanshare::mdns::g_mdns_manager) {
        return -1;
    }
    return lanshare::mdns::g_mdns_manager->startDiscoverer(callback, user_data);
}

void lanshare_mdns_stop_discoverer(lanshare_mdns_handle_t handle) {
    (void)handle;
    if (lanshare::mdns::g_mdns_manager) {
        lanshare::mdns::g_mdns_manager->stopDiscoverer();
    }
}

int lanshare_mdns_start_broadcaster(lanshare_mdns_handle_t handle, 
                                    const lanshare_device_t* device, const char* service_type, int port) {
    (void)handle;
    if (!lanshare::mdns::g_mdns_manager) {
        return -1;
    }
    return lanshare::mdns::g_mdns_manager->startBroadcaster(device, service_type, port);
}

void lanshare_mdns_stop_broadcaster(lanshare_mdns_handle_t handle) {
    (void)handle;
    if (lanshare::mdns::g_mdns_manager) {
        lanshare::mdns::g_mdns_manager->stopBroadcaster();
    }
}

const char* lanshare_mdns_version(void) {
    return lanshare::mdns::MDNSManager::getVersion();
}

const char* lanshare_mdns_get_discovered_devices(char* buffer, size_t buffer_size) {
    if (!lanshare::mdns::g_mdns_manager) {
        return nullptr;
    }
    return lanshare::mdns::g_mdns_manager->getDiscoveredDevices(buffer, buffer_size);
}

int lanshare_mdns_add_device(const lanshare_device_t* device) {
    if (!lanshare::mdns::g_mdns_manager || !device) {
        return -1;
    }
    return lanshare::mdns::g_mdns_manager->addDevice(device);
}

void lanshare_mdns_clear_devices(void) {
    if (lanshare::mdns::g_mdns_manager) {
        lanshare::mdns::g_mdns_manager->clearDevices();
    }
}

int lanshare_mdns_get_device_count(void) {
    if (!lanshare::mdns::g_mdns_manager) {
        return 0;
    }
    return lanshare::mdns::g_mdns_manager->getDeviceCount();
}

const lanshare_device_t* lanshare_mdns_get_device(int index) {
    if (!lanshare::mdns::g_mdns_manager) {
        return nullptr;
    }
    return lanshare::mdns::g_mdns_manager->getDevice(index);
}

int lanshare_mdns_process_responses(void) {
    if (!lanshare::mdns::g_mdns_manager) {
        return -1;
    }
    return lanshare::mdns::g_mdns_manager->processResponses();
}

const char* lanshare_mdns_get_discovered_devices_impl(char* buffer, size_t buffer_size) {
    return lanshare_mdns_get_discovered_devices(buffer, buffer_size);
}

} // extern "C"
