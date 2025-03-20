#pragma once
#include <liburing.h>
#include <memory>
#include <atomic>
#include "UringBuffer.h"
#include "Context.h"
#include <vector>
#include <mutex>
#include <unordered_map>

class IOUring {
public:
    static constexpr unsigned NUM_SUBMISSION_QUEUE_ENTRIES = 8192;
    static constexpr unsigned CQE_BATCH_SIZE = 512;
    static constexpr unsigned NUM_WAIT_ENTRIES = 1;
    IOUring();
    ~IOUring();

    // IO 준비 메서드
    void prepareAccept(int socket_fd);
    void prepareRead(int client_fd);
    void prepareWrite(int client_fd, const void* buf, unsigned len, uint16_t bid);
    void prepareClose(int client_fd);
    
    // IO 이벤트 처리 관련 메서드 (Session에서 처리하므로 중복 제거)
    unsigned peekCQE(io_uring_cqe** cqes);
    void advanceCQ(unsigned count);
    int submitAndWait();

    // Non-blocking submit
    int submit() {
        return io_uring_submit(&ring_);
    }

    // 버퍼 관리 관련 메서드    
    void releaseBuffer(uint16_t idx) { buffer_manager_->releaseBuffer(idx, buffer_manager_->getBaseAddr()); }
    void handleWriteComplete(int32_t client_fd, uint16_t buffer_idx, int32_t bytes_written);

    UringBuffer& getBufferManager() { return *buffer_manager_; }
    const UringBuffer& getBufferManager() const { return *buffer_manager_; }
    
    // 링 및 버퍼 관리자 접근자
    io_uring* getRing() { return &ring_; }

private:
    void initRing();
    io_uring_sqe* getSQE();
    void setContext(io_uring_sqe* sqe, OperationType type, int client_fd = -1, uint16_t buffer_idx = 0);

    io_uring ring_;
    bool ring_initialized_;
    std::unique_ptr<UringBuffer> buffer_manager_;
    std::atomic<uint64_t> total_messages_{0};
}; 
