#pragma once
#include <sys/socket.h>
#include <cstring>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <arpa/inet.h>

class SocketAddress {
public:
    // 생성자들
    SocketAddress(uint32_t ipAddress, uint16_t port)
    {
        auto* addrIn = getAsSockAddrIn();
        addrIn->sin_family = AF_INET;
        addrIn->sin_addr.s_addr = htonl(ipAddress);
        addrIn->sin_port = htons(port);
    }
    
    SocketAddress(const std::string& ipString, uint16_t port)
    {
        auto* addrIn = getAsSockAddrIn();
        addrIn->sin_family = AF_INET;
        addrIn->sin_port = htons(port);
        
        // IP 주소 변환
        if (inet_pton(AF_INET, ipString.c_str(), &(addrIn->sin_addr)) <= 0) {
            // 변환 실패 시 localhost로 설정
            addrIn->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
    }
    
    explicit SocketAddress(const sockaddr& inSockAddr)
    {
        std::memcpy(&mSockAddr, &inSockAddr, sizeof(sockaddr));
    }
    
    // 접근자 메서드
    uint32_t getIPAddress() const
    {
        return ntohl(getAsSockAddrIn()->sin_addr.s_addr);
    }
    
    uint16_t getPort() const
    {
        return ntohs(getAsSockAddrIn()->sin_port);
    }
    
    std::string toString() const
    {
        char ipBuffer[INET_ADDRSTRLEN];
        const sockaddr_in* addrIn = getAsSockAddrIn();
        inet_ntop(AF_INET, &(addrIn->sin_addr), ipBuffer, INET_ADDRSTRLEN);
        return std::string(ipBuffer) + ":" + std::to_string(getPort());
    }
    
    size_t getSize() const { return sizeof(sockaddr); }
    
    // 내부 접근자
    const sockaddr* getSockAddrPtr() const { return reinterpret_cast<const sockaddr*>(&mSockAddr); }
    sockaddr* getSockAddrPtr() { return reinterpret_cast<sockaddr*>(&mSockAddr); }

private:
    sockaddr_storage mSockAddr;
    
    // 헬퍼 메서드
    sockaddr_in* getAsSockAddrIn() { return reinterpret_cast<sockaddr_in*>(&mSockAddr); }
    const sockaddr_in* getAsSockAddrIn() const { return reinterpret_cast<const sockaddr_in*>(&mSockAddr); }
};

using SocketAddressPtr = std::shared_ptr<SocketAddress>;


