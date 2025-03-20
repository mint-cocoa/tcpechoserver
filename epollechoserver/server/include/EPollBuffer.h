#pragma once

#include <vector>
#include <list>
#include <queue>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <sys/types.h>
#include "include/Context.h"

// 입출력 버퍼 구조체
struct IOBuffer {
    static constexpr size_t IO_BUFFER_SIZE = 1024;
    uint8_t* data;           // 버퍼 데이터
    size_t length;           // 현재 저장된 데이터 길이
    size_t write_offset;     // 쓰기 오프셋
    int buffer_id;           // 버퍼 ID

    IOBuffer() : data(nullptr), length(0), write_offset(0), buffer_id(-1) {}
    IOBuffer(uint8_t* buf, int id) : data(buf), length(0), write_offset(0), buffer_id(id) {}
};

// EPoll 버퍼 관리 클래스
class EPollBuffer {
public:
    explicit EPollBuffer(size_t buffer_size = IOBuffer::IO_BUFFER_SIZE, size_t buffer_count = 256);
    ~EPollBuffer();
    
    // 버퍼 할당 및 해제
    IOBuffer allocateBuffer();
    void releaseBuffer(int buffer_id);
    
    // 클라이언트별 버퍼 관리
    void addToClientQueue(int client_fd, IOBuffer& buffer);
    bool hasDataToWrite(int client_fd) const;
    IOBuffer& getNextBufferToWrite(int client_fd);
    void removeProcessedBuffer(int client_fd);
    void clearClientBuffers(int client_fd);
    
    // 데이터 관리
    ssize_t readToBuffer(int fd, IOBuffer& buffer);
    ssize_t writeFromBuffer(int fd, IOBuffer& buffer);
    
    // 메시지 처리
    bool getMessageFromBuffer(IOBuffer& buffer, ChatMessage& message);
    void makeMessage(IOBuffer& buffer, MessageType type, const void* data, size_t length);
    
    // 버퍼 풀 상태 확인
    bool hasAvailableBuffers() const { return !free_buffers_.empty(); }
    size_t getBufferSize() const { return buffer_size_; }
    size_t getBufferCount() const { return buffer_count_; }
    
private:
    std::vector<uint8_t*> buffer_pool_;
    std::list<int> free_buffers_;
    std::mutex buffer_mutex_;
    size_t buffer_size_;
    size_t buffer_count_;
    
    // 클라이언트별 버퍼 큐 관리
    std::unordered_map<int, std::queue<IOBuffer>> client_buffers_;
    mutable std::mutex client_mutex_;
}; 