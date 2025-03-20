#pragma once
#include "SocketAddress.h"
#include <memory>
#include <unistd.h>
#include <fcntl.h>
#include <stdexcept>
#include <iostream>

class Socket {
public:
    enum Type {
        TCP,
        UDP
    };

    // 기본 생성자 - 새 소켓 생성
    Socket(Type type = TCP) : mType(type), mOwnsFd(true), mSocketFd(-1) {
        int socketType = (type == TCP) ? SOCK_STREAM : SOCK_DGRAM;
        mSocketFd = socket(AF_INET, socketType, 0);
    }

    // 기존 소켓 FD로부터 생성
    explicit Socket(int existingSocketFd) : mType(TCP), mOwnsFd(true), mSocketFd(existingSocketFd) {}

    // 소멸자 - 소켓 자원 정리
    ~Socket() {
        close();
    }

    // 소켓 작업 메서드
    bool bind(const SocketAddress& localAddress) {
        int result = ::bind(mSocketFd, localAddress.getSockAddrPtr(), localAddress.getSize());
        return result == 0;
    }

    bool listen(int backlog = SOMAXCONN) {
        int result = ::listen(mSocketFd, backlog);
        return result == 0;
    }

    std::shared_ptr<Socket> accept(SocketAddress& clientAddress) {
        socklen_t addrLen = static_cast<socklen_t>(clientAddress.getSize());
        int newSocketFd = ::accept(mSocketFd, clientAddress.getSockAddrPtr(), &addrLen);
        
        if (newSocketFd >= 0) {
            return std::make_shared<Socket>(newSocketFd);
        }
        return nullptr;
    }

    bool connect(const SocketAddress& serverAddress) {
        int result = ::connect(mSocketFd, serverAddress.getSockAddrPtr(), serverAddress.getSize());
        return result == 0;
    }

    // 데이터 전송 메서드
    int send(const void* data, int length) {
        return ::send(mSocketFd, data, length, 0);
    }

    int receive(void* buffer, int length) {
        return ::recv(mSocketFd, buffer, length, 0);
    }

    // 소켓 설정 메서드
    bool setNonBlocking(bool nonBlocking) {
        int flags = fcntl(mSocketFd, F_GETFL, 0);
        if (flags == -1) return false;

        if (nonBlocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }

        int result = fcntl(mSocketFd, F_SETFL, flags);
        return result != -1;
    }

    bool setReuseAddr(bool reuse) {
        int optVal = reuse ? 1 : 0;
        int result = setsockopt(mSocketFd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
        return result == 0;
    }

    // 상태 확인 메서드
    bool isValid() const {
        return mSocketFd >= 0;
    }

    int getSocketFd() const {
        return mSocketFd;
    }

    // 복사 금지
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // 이동 생성자 및 대입 연산자
    Socket(Socket&& other) noexcept : mSocketFd(other.mSocketFd), mType(other.mType), mOwnsFd(other.mOwnsFd) {
        other.mSocketFd = -1;
        other.mOwnsFd = false;
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            mSocketFd = other.mSocketFd;
            mType = other.mType;
            mOwnsFd = other.mOwnsFd;
            other.mSocketFd = -1;
            other.mOwnsFd = false;
        }
        return *this;
    }

private:
    int mSocketFd;
    Type mType;
    bool mOwnsFd;  // 소켓 FD의 소유권을 가지고 있는지 여부

    void close() {
        if (mOwnsFd && mSocketFd >= 0) {
            ::close(mSocketFd);
            mSocketFd = -1;
        }
    }
};

using SocketPtr = std::shared_ptr<Socket>; 