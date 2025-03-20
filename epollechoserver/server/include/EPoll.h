#pragma once

#include <memory>
#include <vector>
#include <sys/epoll.h>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "include/Context.h"
#include "include/EPollBuffer.h"

// 클라이언트 컨텍스트 구조체
struct ClientContext {
    int fd;                     // 파일 디스크립터
    OperationType op_type;      // 작업 타입
    
    ClientContext(int client_fd = -1) : fd(client_fd), op_type(OperationType::READ) {}
};

// EPoll 래핑 클래스
class EPoll {
public:
    // 상수 정의
    static constexpr int MAX_EVENTS = 512;
    
    EPoll();
    ~EPoll();
    
    // epoll 초기화 및 종료
    void initEPoll();
    
    // 이벤트 관리
    bool addEvent(int fd, uint32_t events);
    bool modifyEvent(int fd, uint32_t events);
    bool removeEvent(int fd);
    
    // 작업 준비 메서드
    void prepareAccept(int socket_fd);
    void prepareRead(int client_fd);
    bool prepareWrite(int client_fd, const void* buf, unsigned len);
    void prepareClose(int client_fd);
    
    // 이벤트 처리
    int waitForEvents(int timeout_ms = 50);
    int getEvents(epoll_event* events, int max_events = MAX_EVENTS);
    
    // 클라이언트 컨텍스트 관리
    ClientContext* getClientContext(int fd);
    void setClientContext(int fd, OperationType type);
    
    // 버퍼 관련 메서드
    EPollBuffer& getBufferManager() { return *buffer_manager_; }
    
    // 이벤트 제출 및 대기 메서드
    int submitAndWait(int timeout_ms = 50);

    // epoll 파일 디스크립터 getter
    int getEpollFd() const { return epoll_fd_; }
    
    // 기본 이벤트 플래그
    static constexpr uint32_t BASE_EVENTS = EPOLLIN | EPOLLET | EPOLLRDHUP;
    
private:
    int epoll_fd_;
    bool epoll_initialized_;
    
    std::unique_ptr<EPollBuffer> buffer_manager_;
    std::unordered_map<int, std::unique_ptr<ClientContext>> fd_contexts_;
    std::mutex mutex_;
    
    std::vector<epoll_event> events_;
    int num_events_;
    int current_event_;
}; 