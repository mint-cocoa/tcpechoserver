#include "include/EPollBuffer.h"
#include "include/Logger.h"
#include <unistd.h>
#include <errno.h>
#include <cstring>

// EPollBuffer 구현
EPollBuffer::EPollBuffer(size_t buffer_size, size_t buffer_count)
    : buffer_size_(buffer_size), buffer_count_(buffer_count) {
    // 버퍼 풀 초기화
    buffer_pool_.reserve(buffer_count);
    for (size_t i = 0; i < buffer_count; ++i) {
        uint8_t* buffer = new uint8_t[buffer_size];
        buffer_pool_.push_back(buffer);
        free_buffers_.push_back(i);
    }
    
    LOG_INFO("EPollBuffer initialized with ", buffer_count, " buffers of size ", buffer_size, " bytes");
}

EPollBuffer::~EPollBuffer() {
    // 버퍼 풀 해제
    for (auto buffer : buffer_pool_) {
        delete[] buffer;
    }
    buffer_pool_.clear();
    free_buffers_.clear();
    
    LOG_INFO("EPollBuffer destroyed");
}

IOBuffer EPollBuffer::allocateBuffer() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (free_buffers_.empty()) {
        LOG_WARN("No free buffers available");
        return IOBuffer(); // 빈 버퍼 반환
    }
    
    int buffer_id = free_buffers_.front();
    free_buffers_.pop_front();
    
    return IOBuffer(buffer_pool_[buffer_id], buffer_id);
}

void EPollBuffer::releaseBuffer(int buffer_id) {
    if (buffer_id < 0 || buffer_id >= static_cast<int>(buffer_pool_.size())) {
        LOG_ERROR("Invalid buffer index: ", buffer_id);
        return;
    }
    
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    free_buffers_.push_back(buffer_id);
}

void EPollBuffer::addToClientQueue(int client_fd, IOBuffer& buffer) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    client_buffers_[client_fd].push(std::move(buffer));
    LOG_DEBUG("Added buffer ", buffer.buffer_id, " to client ", client_fd, "'s queue");
}

bool EPollBuffer::hasDataToWrite(int client_fd) const {
    std::lock_guard<std::mutex> lock(client_mutex_);
    auto it = client_buffers_.find(client_fd);
    return it != client_buffers_.end() && !it->second.empty();
}

IOBuffer& EPollBuffer::getNextBufferToWrite(int client_fd) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    auto it = client_buffers_.find(client_fd);
    if (it != client_buffers_.end() && !it->second.empty()) {
        return it->second.front();
    }
    
    static IOBuffer empty_buffer;
    LOG_WARN("No buffer available for client ", client_fd);
    return empty_buffer;
}

void EPollBuffer::removeProcessedBuffer(int client_fd) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    auto it = client_buffers_.find(client_fd);
    if (it != client_buffers_.end() && !it->second.empty()) {
        int buffer_id = it->second.front().buffer_id;
        it->second.pop();
        releaseBuffer(buffer_id);
        LOG_DEBUG("Removed and released buffer ", buffer_id, " from client ", client_fd, "'s queue");
    }
}

void EPollBuffer::clearClientBuffers(int client_fd) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    auto it = client_buffers_.find(client_fd);
    if (it != client_buffers_.end()) {
        size_t count = 0;
        while (!it->second.empty()) {
            releaseBuffer(it->second.front().buffer_id);
            it->second.pop();
            count++;
        }
        client_buffers_.erase(it);
        LOG_DEBUG("Cleared ", count, " buffers for client ", client_fd);
    }
}

ssize_t EPollBuffer::readToBuffer(int fd, IOBuffer& buffer) {
    if (!buffer.data || buffer.length >= buffer_size_) {
        LOG_ERROR("Invalid buffer for reading or buffer full");
        return -1;
    }
    
    ssize_t bytes_read = read(fd, buffer.data + buffer.length, 
                            buffer_size_ - buffer.length);
    
    if (bytes_read > 0) {
        buffer.length += bytes_read;
        LOG_DEBUG("Read ", bytes_read, " bytes to buffer ", buffer.buffer_id, 
                  " (total: ", buffer.length, ")");
    } else if (bytes_read == 0) {
        LOG_DEBUG("EOF reached on fd ", fd);
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LOG_DEBUG("No data available for reading on fd ", fd);
            return 0;
        } else {
            LOG_ERROR("Read error on fd ", fd, ": ", strerror(errno));
        }
    }
    
    return bytes_read;
}

ssize_t EPollBuffer::writeFromBuffer(int fd, IOBuffer& buffer) {
    if (!buffer.data || buffer.write_offset >= buffer.length) {
        LOG_ERROR("Invalid buffer for writing or nothing to write");
        return -1;
    }
    
    size_t remaining = buffer.length - buffer.write_offset;
    ssize_t bytes_written = write(fd, buffer.data + buffer.write_offset, remaining);
    
    if (bytes_written > 0) {
        buffer.write_offset += bytes_written;
        LOG_DEBUG("Wrote ", bytes_written, " bytes from buffer ", buffer.buffer_id, 
                  " (", buffer.write_offset, "/", buffer.length, ")");
    } else if (bytes_written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LOG_DEBUG("Write would block on fd ", fd);
            return 0;
        } else {
            LOG_ERROR("Write error on fd ", fd, ": ", strerror(errno));
        }
    }
    
    return bytes_written;
}

bool EPollBuffer::getMessageFromBuffer(IOBuffer& buffer, ChatMessage& message) {
    if (!buffer.data || buffer.length < sizeof(ChatMessage)) {
        LOG_ERROR("Buffer too small to contain a complete message");
        return false;
    }
    
    // 버퍼에서 메시지 복사
    std::memcpy(&message, buffer.data, sizeof(ChatMessage));
    
    // 메시지 길이 검증
    if (message.header.length > MAX_MESSAGE_SIZE) {
        LOG_ERROR("Invalid message length: ", message.header.length);
        return false;
    }
    
    // 버퍼에 전체 메시지가 있는지 확인
    if (buffer.length < sizeof(ChatMessageHeader) + message.header.length) {
        LOG_DEBUG("Incomplete message in buffer");
        return false;
    }
    
    LOG_DEBUG("Parsed message of type ", static_cast<int>(message.header.type), 
              ", length: ", message.header.length);
    return true;
}

void EPollBuffer::makeMessage(IOBuffer& buffer, MessageType type, const void* data, size_t length) {
    if (!buffer.data || length > MAX_MESSAGE_SIZE) {
        LOG_ERROR("Invalid buffer or data too large");
        return;
    }
    
    // 버퍼 초기화
    buffer.length = 0;
    buffer.write_offset = 0;
    
    // 메시지 헤더 구성
    ChatMessage* message = reinterpret_cast<ChatMessage*>(buffer.data);
    message->header.type = type;
    message->header.length = static_cast<uint16_t>(length);
    
    // 데이터 복사
    if (data && length > 0) {
        std::memcpy(message->data, data, length);
    }
    
    // 버퍼 길이 설정
    buffer.length = sizeof(ChatMessageHeader) + length;
    
    LOG_DEBUG("Created message of type ", static_cast<int>(type), 
              ", length: ", length, ", total: ", buffer.length);
} 