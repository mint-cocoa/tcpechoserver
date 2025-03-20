#include "SocketManager.h"
#include "Logger.h"
#include <stdexcept>
#include "Context.h"

// SocketUtils 네임스페이스 내 함수 구현
namespace SocketUtils {

bool setSocketNonBlocking(SocketPtr socket, bool nonBlocking) {
    if (!socket || !socket->isValid()) {
        LOG_ERROR("[SocketUtils] Invalid socket in setSocketNonBlocking");
        return false;
    }
    
    bool result = socket->setNonBlocking(nonBlocking);
    if (result) {
        LOG_DEBUG("[SocketUtils] Set socket non-blocking mode: ", nonBlocking);
    } else {
        LOG_ERROR("[SocketUtils] Failed to set socket non-blocking mode");
    }
    return result;
}

bool setSocketReuseAddr(SocketPtr socket, bool reuseAddr) {
    if (!socket || !socket->isValid()) {
        LOG_ERROR("[SocketUtils] Invalid socket in setSocketReuseAddr");
        return false;
    }
    
    bool result = socket->setReuseAddr(reuseAddr);
    if (result) {
        LOG_DEBUG("[SocketUtils] Set socket reuse address: ", reuseAddr);
    } else {
        LOG_ERROR("[SocketUtils] Failed to set socket reuse address");
    }
    return result;
}

SocketPtr createListeningSocket(const std::string& host, uint16_t port) {
    // TCP 소켓 생성
    auto socket = createTCPSocket();
    if (!socket) {
        return nullptr;
    }
    
    // 소켓 옵션 설정
    if (!setSocketReuseAddr(socket, true)) {
        return nullptr;
    }
    
    // 주소 바인딩
    SocketAddress address(host, port);
    if (!socket->bind(address)) {
        LOG_ERROR("[SocketUtils] Failed to bind socket to ", host, ":", port);
        return nullptr;
    }
    
    // 리스닝 시작
    if (!socket->listen()) {
        LOG_ERROR("[SocketUtils] Failed to start listening on socket");
        return nullptr;
    }
    
    LOG_INFO("[SocketUtils] Successfully created listening socket on ", host, ":", port);
    return socket;
}

SocketPtr createClientSocket(const std::string& host, uint16_t port) {
    // TCP 소켓 생성
    auto socket = createTCPSocket();
    if (!socket) {
        return nullptr;
    }
    
    // 서버 연결
    SocketAddress serverAddress(host, port);
    if (!socket->connect(serverAddress)) {
        LOG_ERROR("[SocketUtils] Failed to connect to ", host, ":", port);
        return nullptr;
    }
    
    LOG_INFO("[SocketUtils] Successfully connected to ", host, ":", port);
    return socket;
}

} // namespace SocketUtils 