#include "include/Listener.h"
#include "include/Logger.h"
#include "include/SocketManager.h"
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <cstring>
#include <errno.h>



// 정적 멤버 변수 초기화
Listener* Listener::instance_ = nullptr;

// 싱글톤 인스턴스 획득 메서드 구현
Listener& Listener::getInstance(int port) {
    if (instance_ == nullptr) {
        instance_ = new Listener(port);
        LOG_INFO("[Listener] Singleton instance created");
    }
    return *instance_;
}

Listener::Listener(int port)
    : port_(port), running_(false), 
      session_manager_(SessionManager::getInstance()) {
    
    // EPoll 인스턴스 생성
    epoll_instance_ = std::make_unique<EPoll>();
    epoll_instance_->initEPoll();  // 명시적으로 초기화 호출
    
    LOG_INFO("[Listener] Created with EPoll instance");
}

Listener::~Listener() {
    stop();
    if (instance_ == this) {
        instance_ = nullptr;
        LOG_INFO("[Listener] Singleton instance destroyed");
    }
}

void Listener::start() {
    if (running_) {
        return;
    }

    // 리스닝 소켓 생성
    listening_socket_ = SocketUtils::createListeningSocket("0.0.0.0", port_);
    if (!listening_socket_ || !listening_socket_->isValid()) {
        throw std::runtime_error("Failed to create listening socket");
    }
    
    // 소켓을 비블로킹 모드로 설정
    if (!listening_socket_->setNonBlocking(true)) {
        throw std::runtime_error("Failed to set socket non-blocking");
    }
    
    LOG_DEBUG("[Listener] Adding listening socket to epoll: fd=", listening_socket_->getSocketFd());
    
    // EPoll 인스턴스 초기화 확인
    if (!epoll_instance_) {
        LOG_ERROR("[Listener] EPoll instance is null");
        throw std::runtime_error("EPoll instance not initialized");
    }
    
    // 리스닝 소켓을 epoll에 등록
    if (!epoll_instance_->addEvent(listening_socket_->getSocketFd(), EPOLLIN)) {
        int error_code = errno;
        LOG_ERROR("[Listener] Failed to add listening socket to epoll: ", strerror(error_code));
        throw std::runtime_error("Failed to add listening socket to epoll");
    }
    
    LOG_INFO("[Listener] Server listening on port ", port_, ", socket: ", listening_socket_->getSocketFd());
    running_ = true;
}

void Listener::processEvents() {
    if (!running_) {
        return;
    }
    
    // 이벤트 대기 (내부적으로 EPoll 클래스의 events_ 벡터에 저장)
    int num_events = epoll_instance_->waitForEvents(100); // 100ms 타임아웃
    if (num_events < 0) {
        if (errno == EINTR) {
            return; // 시그널에 의한 인터럽트는 무시
        }
        LOG_ERROR("[Listener] epoll_wait error: ", strerror(errno));
        return;
    }
    
    // EPoll 클래스로부터 이벤트를 가져옴
    epoll_event events[Listener::MAX_EVENTS];
    int event_count = epoll_instance_->getEvents(events, Listener::MAX_EVENTS);
    
    for (int i = 0; i < event_count; i++) {
        int fd = events[i].data.fd;
        
        if (fd == listening_socket_->getSocketFd()) {
            // 새 연결 수락
            struct sockaddr_in client_addr;
            socklen_t client_addr_len = sizeof(client_addr);
            
            while (true) { // 모든 대기 중인 연결 수락
                int client_fd = accept(listening_socket_->getSocketFd(), (struct sockaddr*)&client_addr, &client_addr_len);
                if (client_fd < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        break; // 더 이상 수락할 연결이 없음
                    }
                    LOG_ERROR("[Listener] Accept error: ", strerror(errno));
                    break;
                }
                
                // 클라이언트 소켓을 Socket 클래스로 래핑
                SocketPtr clientSocket = std::make_shared<Socket>(client_fd);
                
                // 논블로킹 모드 설정
                if (!clientSocket->setNonBlocking(true)) {
                    LOG_ERROR("[Listener] Failed to set non-blocking mode for client ", client_fd);
                    clientSocket.reset(); // 소켓 닫기
                    continue;
                }
                
                LOG_INFO("[Listener] New client connected: ", client_fd);
                
                // SessionManager를 통해 세션에 클라이언트 할당
                session_manager_.assignClientToSession(clientSocket);
            }
        } else {
            // 클라이언트 이벤트는 세션이 자동으로 처리하므로 여기서는 무시
            LOG_DEBUG("[Listener] Ignoring event for fd ", fd, " - handled by session");
        }
    }
}

void Listener::stop() {
    running_ = false;
    
    if (listening_socket_ && listening_socket_->isValid()) {
        int fd = listening_socket_->getSocketFd();
        epoll_instance_->removeEvent(fd);
        listening_socket_.reset();
    }
    
    epoll_instance_.reset();
    
    LOG_INFO("[Listener] Stopped");
}
