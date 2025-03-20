#include "Listener.h"
#include "SessionManager.h"
#include "SocketManager.h"
#include "Logger.h"
#include <stdexcept>
#include "Context.h"
#include <string>
#include <liburing.h>

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
    : port_(port), running_(false), session_manager_(SessionManager::getInstance()) {
    io_ring_ = std::make_unique<IOUring>();
    LOG_INFO("[Listener] Created with dedicated IOUring");
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

    // SocketUtils 네임스페이스 함수를 직접 사용
    listening_socket_ = SocketUtils::createListeningSocket("0.0.0.0", port_);
    
    if (!listening_socket_ || !listening_socket_->isValid()) {
        throw std::runtime_error("Failed to create listening socket");
    }
    
    LOG_INFO("[Listener] Server listening on port ", port_, ", socket: ", listening_socket_->getSocketFd());

    running_ = true;
    io_ring_->prepareAccept(listening_socket_->getSocketFd());
}

void Listener::processEvents() {
    if (!io_ring_) {
        LOG_ERROR("[Listener] IOUring is null");
        return;
    }

    // 로컬 배열 대신 멤버 변수 사용
    unsigned num_cqes = io_ring_->peekCQE(cqes_);
    
   

    if (num_cqes == 0) {
        // 이벤트가 없으면 새 이벤트가 있을 때까지 대기
        const int result = io_ring_->submitAndWait();
        if (result < 0 && result != -EINTR) {
            LOG_ERROR("[Listener] io_uring_submit_and_wait failed: ", result);
            return;
        }
        num_cqes = io_ring_->peekCQE(cqes_);
    }

    for (unsigned i = 0; i < num_cqes; ++i) {
        io_uring_cqe* cqe = cqes_[i];
        if (!cqe) {
            LOG_ERROR("[Listener] Null CQE at index ", i);
            continue;
        }

        Operation ctx = getContext(cqe);

        if (ctx.op_type == OperationType::ACCEPT) {
            if (cqe->res < 0) {
                LOG_ERROR("[Listener] Accept failed: ", -cqe->res);
            } else {
                int client_fd = cqe->res;
                
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
                auto& sessionManager = SessionManager::getInstance();
                sessionManager.assignClientToSession(clientSocket);
                
                // 새로운 ACCEPT 작업 등록
                io_ring_->prepareAccept(listening_socket_->getSocketFd());
            }
        }
    }

    io_ring_->advanceCQ(num_cqes);
    io_ring_->submit();
}

void Listener::stop() {
    running_ = false;
    if (listening_socket_ && listening_socket_->isValid()) {
        // Socket 객체를 reset하면 소멸자에서 자동으로 close 처리
        listening_socket_.reset();
    }
    LOG_INFO("[Listener] Stopped");
} 