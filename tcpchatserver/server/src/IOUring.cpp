#include <stdexcept>
#include <iostream>
#include "IOUring.h"
#include "SessionManager.h"
#include "Logger.h"
#include <string.h>
#include <sstream>
#include <iomanip>

IOUring::IOUring() : ring_initialized_(false) {
    initRing();
    buffer_manager_ = std::make_unique<UringBuffer>(&ring_);
}

IOUring::~IOUring() {
    try {
        // First release the buffer_manager_ which depends on the ring_
        buffer_manager_.reset();
        
        // Then clean up the io_uring instance
        if (ring_initialized_) {
            io_uring_queue_exit(&ring_);
            ring_initialized_ = false;
        }
        LOG_INFO("IOUring destroyed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error during IOUring destruction: ", e.what());
    }
}

void IOUring::initRing() {
    io_uring_params params{};
    memset(&params, 0, sizeof(params));

    int ret = io_uring_queue_init_params(NUM_SUBMISSION_QUEUE_ENTRIES, &ring_, &params);
    if (ret < 0) {
        LOG_FATAL("Failed to initialize io_uring: ", ret);
        throw std::runtime_error("Failed to initialize io_uring");
    }
    LOG_INFO("io_uring initialized successfully");
    ring_initialized_ = true;
}

io_uring_sqe* IOUring::getSQE() {
    if (!ring_initialized_) {
        LOG_ERROR("Attempting to get SQE with uninitialized ring");
        throw std::runtime_error("Ring not initialized in getSQE");
    }

    io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            LOG_ERROR("Failed to get SQE after submit");
            throw std::runtime_error("Failed to get SQE even after submit");
        }
    }
    return sqe;
}

int IOUring::submitAndWait() {
    int ret = io_uring_submit_and_wait(&ring_, NUM_WAIT_ENTRIES);
    if (ret < 0) {
        if (ret != -EINTR) {
            LOG_ERROR("io_uring_submit_and_wait failed: ", ret);
        }
        return ret;
    }
    return 0;
}

void IOUring::setContext(io_uring_sqe* sqe, OperationType type, int client_fd, uint16_t buffer_idx) {
    static_assert(8 == sizeof(__u64));  // user_data 크기 확인
    
    if (!sqe) {
        LOG_ERROR("IOUring::setContext called with null sqe pointer");
        return;
    }
    
    // 유효하지 않은 데이터 검사
    if (client_fd < -1) {
        LOG_ERROR("IOUring::setContext called with invalid client_fd: ", client_fd);
        return;
    }
    
    // user_data 메모리 접근 전 포인터 유효성 검사
    try {
        auto* buffer = reinterpret_cast<uint8_t*>(&sqe->user_data);

        // client_fd 쓰기 (4 bytes)
        *(reinterpret_cast<int32_t*>(buffer)) = client_fd;
        buffer += 4;
        // type 쓰기 (1 byte)
        *buffer = static_cast<uint8_t>(type);
        buffer += 1;
        // buffer_idx 쓰기 (2 bytes)
        *(reinterpret_cast<uint16_t*>(buffer)) = buffer_idx;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in IOUring::setContext: ", e.what());
    }
}

void IOUring::prepareAccept(int socket_fd) {
    io_uring_sqe* sqe = getSQE();
    setContext(sqe, OperationType::ACCEPT, -1, 0);
    const int flags = 0;
    io_uring_prep_multishot_accept(sqe, socket_fd, nullptr, 0, flags);
}

void IOUring::prepareRead(int client_fd) {
    if (client_fd < 0) {
        LOG_ERROR("IOUring::prepareRead called with invalid client_fd: ", client_fd);
        return;
    }
    
    try {
        io_uring_sqe* sqe = getSQE();
        if (!sqe) {
            LOG_ERROR("Failed to get SQE for prepareRead, client_fd: ", client_fd);
            return;
        }
        
        setContext(sqe, OperationType::READ, client_fd, 0);
        io_uring_prep_recv_multishot(sqe, client_fd, nullptr, 0, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = 1;  // Buffer group ID
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in prepareRead for client_fd ", client_fd, ": ", e.what());
    }
}

void IOUring::prepareWrite(int client_fd, const void* buf, unsigned len, uint16_t bid) {
    io_uring_sqe* sqe = getSQE();
    if (!sqe) {
        LOG_ERROR("Failed to get SQE for prepareWrite, client_fd: ", client_fd);
        return;
    }

    // 버퍼 주소 유효성 검사 추가
    if (buf == nullptr) {
        LOG_ERROR("Invalid buffer address in prepareWrite, client_fd: ", client_fd);
        return;
    }

    io_uring_prep_write(sqe, client_fd, buf, len, 0);
    setContext(sqe, OperationType::WRITE, client_fd, bid);
}

void IOUring::prepareClose(int client_fd) {
    io_uring_sqe* sqe = getSQE();
    setContext(sqe, OperationType::CLOSE, client_fd);
    io_uring_prep_close(sqe, client_fd);
}

void IOUring::handleWriteComplete(int32_t client_fd, uint16_t buffer_idx, int32_t bytes_written) {
    if (bytes_written < 0) {
        LOG_ERROR("Write failed for client ", client_fd, ": ", bytes_written);
    }

    // 버퍼 재사용을 위해 io_uring_buf_ring에 다시 추가
    releaseBuffer(buffer_idx);
}

unsigned IOUring::peekCQE(io_uring_cqe** cqes) {
    if (!ring_initialized_) {
        LOG_ERROR("Attempting to peek CQE with uninitialized ring");
        return 0;
    }
    
    if (!cqes) {
        LOG_ERROR("Invalid cqes array pointer in peekCQE");
        return 0;
    }
    
    return io_uring_peek_batch_cqe(&ring_, cqes, CQE_BATCH_SIZE);
}

void IOUring::advanceCQ(unsigned count) {
    io_uring_cq_advance(&ring_, count);
}

