#include "include/EPoll.h"
#include "include/Logger.h"
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <iostream>

// EPoll 구현
EPoll::EPoll() : epoll_fd_(-1), epoll_initialized_(false),
                 num_events_(0), current_event_(0) {
    buffer_manager_ = std::make_unique<EPollBuffer>();
    events_.resize(MAX_EVENTS);
}

EPoll::~EPoll() {
    try {
        // 버퍼 관리자 정리
        buffer_manager_.reset();
        
        // epoll 인스턴스 정리
        if (epoll_initialized_ && epoll_fd_ != -1) {
            close(epoll_fd_);
            epoll_fd_ = -1;
            epoll_initialized_ = false;
        }
        LOG_INFO("EPoll destroyed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error during EPoll destruction: ", e.what());
    }
}

void EPoll::initEPoll() {
    if (epoll_initialized_) {
        return;
    }
    
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::runtime_error("Failed to create epoll instance");
    }
    epoll_initialized_ = true;
}

bool EPoll::addEvent(int fd, uint32_t events) {
    if (!epoll_initialized_) {
        return false;
    }
    
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
        return false;
    }
    
    return true;
}

bool EPoll::modifyEvent(int fd, uint32_t events) {
    if (!epoll_initialized_) {
        return false;
    }
    
    epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1) {
        return false;
    }
    
    return true;
}

bool EPoll::removeEvent(int fd) {
    if (!epoll_initialized_) {
        return false;
    }
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        return false;
    }
    
    return true;
}

void EPoll::prepareAccept(int socket_fd) {
    addEvent(socket_fd, EPOLLIN | EPOLLET);
}

void EPoll::prepareRead(int client_fd) {
    addEvent(client_fd, EPOLLIN | EPOLLET | EPOLLRDHUP);
}

bool EPoll::prepareWrite(int client_fd, const void* buf, unsigned len) {
    if (!buf || len == 0) {
        LOG_ERROR("Invalid buffer or length for prepareWrite");
        return false;
    }
    
    if (!epoll_initialized_) {
        LOG_ERROR("EPoll not initialized for prepareWrite");
        return false;
    }
    
    EPollBuffer& buffer_manager = getBufferManager();
    
    if (!buffer_manager.hasAvailableBuffers()) {
        LOG_ERROR("No available buffers for prepareWrite");
        return false;
    }
    
    IOBuffer buffer = buffer_manager.allocateBuffer();
    if (!buffer.data) {
        LOG_ERROR("Failed to allocate buffer for prepareWrite");
        return false;
    }
    
    if (len > buffer_manager.getBufferSize()) {
        LOG_ERROR("Data too large for buffer: ", len, " > ", buffer_manager.getBufferSize());
        buffer_manager.releaseBuffer(buffer.buffer_id);
        return false;
    }
    
    // 데이터 복사
    memcpy(buffer.data, buf, len);
    buffer.length = len;
    
    // 클라이언트 큐에 추가
    buffer_manager.addToClientQueue(client_fd, buffer);
    
    LOG_DEBUG("Prepared write of ", len, " bytes for client ", client_fd);
    
    // 이벤트 수정하여 쓰기를 활성화 (EPOLLOUT 추가)
    bool result = modifyEvent(client_fd, BASE_EVENTS | EPOLLOUT);
    if (!result) {
        LOG_ERROR("Failed to modify event for write on client ", client_fd);
    }
    
    return result;
}

void EPoll::prepareClose(int client_fd) {
    try {
        // 클라이언트 버퍼 정리
        if (buffer_manager_) {
            buffer_manager_->clearClientBuffers(client_fd);
        }
        
        // epoll에서 파일 디스크립터 제거
        if (epoll_initialized_ && epoll_fd_ != -1) {
            // 이미 제거되었을 수 있으므로 무시
            removeEvent(client_fd);
        }
        
        // 소켓 닫기
        // EINTR 인터럽트에도 소켓이 확실히 닫히도록 함
        int close_result;
        do {
            close_result = close(client_fd);
        } while (close_result == -1 && errno == EINTR);
        
        // 컨텍스트 제거
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fd_contexts_.erase(client_fd);
        }
        
        LOG_DEBUG("Socket ", client_fd, " closed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error closing socket ", client_fd, ": ", e.what());
    }
}

int EPoll::waitForEvents(int timeout_ms) {
    if (!epoll_initialized_) {
        return -1;
    }
    
    num_events_ = epoll_wait(epoll_fd_, events_.data(), MAX_EVENTS, timeout_ms);
    current_event_ = 0;
    return num_events_;
}

int EPoll::getEvents(epoll_event* events, int max_events) {
    if (!events || max_events <= 0) {
        return 0;
    }
    
    int count = 0;
    while (current_event_ < num_events_ && count < max_events) {
        events[count++] = events_[current_event_++];
    }
    
    return count;
}

ClientContext* EPoll::getClientContext(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = fd_contexts_.find(fd);
    return it != fd_contexts_.end() ? it->second.get() : nullptr;
}

void EPoll::setClientContext(int fd, OperationType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto context = std::make_unique<ClientContext>(fd);
    context->op_type = type;
    fd_contexts_[fd] = std::move(context);
}

int EPoll::submitAndWait(int timeout_ms) {
    return waitForEvents(timeout_ms);
} 