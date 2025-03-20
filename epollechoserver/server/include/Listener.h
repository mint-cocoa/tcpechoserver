#pragma once

#include <memory>
#include <unistd.h>
#include <sys/epoll.h>
#include "SocketManager.h"
#include "SessionManager.h"
#include "EPollBuffer.h"
#include "Socket.h"
#include "EPoll.h"
// 상수 정의


class Listener {
public:
    static constexpr int MAX_EVENTS = 512;
    
    // 소멸자는 public으로 유지
    ~Listener();
    
    // 복사 및 이동 생성자/대입 연산자 삭제
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;
    
    // 싱글톤 인스턴스 획득 메서드
    static Listener& getInstance(int port);
    
    void start();
    void processEvents();
    void stop();

private:
    // 생성자를 private으로 변경
    explicit Listener(int port);
    
    static Listener* instance_;
    
    int port_;
    bool running_;
    SocketPtr listening_socket_;  // Socket 클래스 사용
    std::unique_ptr<EPoll> epoll_instance_;
    
    // 메인 루프에서 반복적으로 사용되는 변수들을 멤버 변수로 이동
    struct epoll_event events_[MAX_EVENTS];
    SessionManager& session_manager_; // 싱글톤 참조를 저장
}; 