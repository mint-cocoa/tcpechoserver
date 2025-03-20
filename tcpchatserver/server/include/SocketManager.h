#pragma once
#include "Socket.h"
#include <string>
#include <memory>

// 소켓 관리 유틸리티 함수 네임스페이스
namespace SocketUtils {
    
    // 소켓 생성 함수
    inline SocketPtr createTCPSocket() {
        // Socket 생성자를 직접 사용 (Socket::TCP를 사용)
        return std::make_shared<Socket>(Socket::TCP);
    }
    
    inline SocketPtr createUDPSocket() {
        // Socket 생성자를 직접 사용 (Socket::UDP를 사용)
        return std::make_shared<Socket>(Socket::UDP);
    }
    
    // 소켓 설정 헬퍼 함수
    bool setSocketNonBlocking(SocketPtr socket, bool nonBlocking);
    bool setSocketReuseAddr(SocketPtr socket, bool reuseAddr);
    
    // 리스닝 소켓 생성 (주소 바인딩 + 리스닝 시작)
    SocketPtr createListeningSocket(const std::string& host, uint16_t port);
    
    // 클라이언트 소켓 생성 (연결 포함)
    SocketPtr createClientSocket(const std::string& host, uint16_t port);
    
    // 소켓 종료 (close 메소드는 private이므로 shared_ptr를 재설정하여 소켓을 닫음)
    inline void closeSocket(SocketPtr& socket) {
        if (socket && socket->isValid()) {
            // Socket 소멸자에서 자동으로 close가 호출됨
            socket.reset();
        }
    }
} 