#include "UringBuffer.h"
#include "Logger.h"
#include <sys/mman.h>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <mutex>
#include <iostream>
#include "Utils.h"

template <unsigned N> constexpr bool is_power_of_two() {
    static_assert(N <= 32768, "N must be N <= 32768");
    return (N == 1 || N == 2 || N == 4 || N == 8 || N == 16 || N == 32 || N == 64 || N == 128 || N == 256 ||
            N == 512 || N == 1024 || N == 2048 || N == 4096 || N == 8192 || N == 16384 || N == 32768);
}

template <unsigned N> constexpr unsigned log2() {
    static_assert(is_power_of_two<N>(), "N must be a power of 2");
    unsigned val = N;
    unsigned ret = 0;
    while (val > 1) {
        val >>= 1;
        ret++;
    }
    return ret;
}

static constexpr unsigned buffer_ring_size() {
    return (UringBuffer::IO_BUFFER_SIZE + sizeof(io_uring_buf)) * UringBuffer::NUM_IO_BUFFERS;
}

static uint8_t* get_buffer_base_addr(void* ring_addr) {
    return static_cast<uint8_t*>(ring_addr) + (sizeof(io_uring_buf) * UringBuffer::NUM_IO_BUFFERS);
}

uint8_t* UringBuffer::getBufferAddr(uint16_t idx, uint8_t* buf_base_addr) {
    if (idx >= NUM_IO_BUFFERS) {
        LOG_ERROR("[Buffer] Invalid buffer index: ", idx);
        return nullptr;
    }
    
    if (!buf_base_addr) {
        LOG_ERROR("[Buffer] Null base address");
        return nullptr;
    }
    
    return buf_base_addr + (idx << log2<UringBuffer::IO_BUFFER_SIZE>());
}

UringBuffer::UringBuffer(io_uring* ring) 
    : ring_(ring), buf_ring_(nullptr), buffer_base_addr_(nullptr), ring_size_(buffer_ring_size())
{
    if (!ring_) {
        LOG_ERROR("Cannot initialize UringBuffer with null io_uring pointer");
        throw std::invalid_argument("Null io_uring pointer");
    }
    
    initBufferRing();
    LOG_INFO("UringBuffer initialized successfully");
}

UringBuffer::~UringBuffer() {
    try {
        if (buf_ring_) {
            // First unregister the buffer ring from io_uring
            if (ring_) {
                io_uring_unregister_buf_ring(ring_, 1);
            }
            
            // Then release the memory-mapped region
            if (munmap(buf_ring_, ring_size_) != 0) {
                LOG_ERROR("Error unmapping buffer ring memory: ", strerror(errno));
            }
            
            buf_ring_ = nullptr;
            buffer_base_addr_ = nullptr;
        }
        
        LOG_INFO("UringBuffer destroyed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("Error during UringBuffer destruction: ", e.what());
    }
}

void UringBuffer::initBufferRing() {
    // Allocate memory-mapped region for buffer ring
    void* ring_addr = mmap(nullptr, ring_size_, 
                          PROT_READ | PROT_WRITE,
                          MAP_ANONYMOUS | MAP_PRIVATE,
                          -1, 0);
    if (ring_addr == MAP_FAILED) {
        LOG_ERROR("Failed to mmap buffer ring: ", strerror(errno));
        throw std::runtime_error("Failed to allocate memory for buffer ring");
    }

    // Register buffer ring with io_uring
    io_uring_buf_reg reg{};
    reg.ring_addr = reinterpret_cast<__u64>(ring_addr);
    reg.ring_entries = NUM_IO_BUFFERS;
    reg.bgid = 1;  // Buffer group ID

    int reg_result = io_uring_register_buf_ring(ring_, &reg, 0);
    if (reg_result < 0) {
        LOG_ERROR("Failed to register buffer ring: ", strerror(-reg_result));
        munmap(ring_addr, ring_size_);
        throw std::runtime_error("Failed to register buffer ring with io_uring");
    }

    // Initialize buffer ring structure
    buf_ring_ = static_cast<io_uring_buf_ring*>(ring_addr);
    io_uring_buf_ring_init(buf_ring_);

    // Calculate base address for the actual buffers
    buffer_base_addr_ = get_buffer_base_addr(ring_addr);

    try {
        // Initialize all buffers in the ring
        for (uint16_t i = 0; i < NUM_IO_BUFFERS; ++i) {
            uint8_t* buf_addr = getBufferAddr(i, buffer_base_addr_);
            if (!buf_addr) {
                throw std::runtime_error("Failed to get buffer address for index " + std::to_string(i));
            }
            
            // Register buffer with io_uring
            io_uring_buf_ring_add(buf_ring_, 
                                buf_addr, 
                                IO_BUFFER_SIZE, 
                                i,
                                io_uring_buf_ring_mask(NUM_IO_BUFFERS), 
                                i);
        }
        
        // Submit all buffers at once
        io_uring_buf_ring_advance(buf_ring_, NUM_IO_BUFFERS);
        
        LOG_DEBUG("Initialized buffer ring with ", NUM_IO_BUFFERS, " buffers of size ", IO_BUFFER_SIZE);
    } catch (const std::exception& e) {
        // Clean up on failure
        LOG_ERROR("Exception during buffer initialization: ", e.what());
        io_uring_unregister_buf_ring(ring_, 1);
        munmap(ring_addr, ring_size_);
        throw;
    }
}

void UringBuffer::releaseBuffer(uint16_t idx, uint8_t* buf_base_addr) {
    if (idx >= NUM_IO_BUFFERS) {
        LOG_ERROR("[Buffer] Invalid buffer index ", idx, " release attempt");
        return;
    }

    
    io_uring_buf_ring_add(buf_ring_, getBufferAddr(idx, buf_base_addr), IO_BUFFER_SIZE, idx,
                         io_uring_buf_ring_mask(NUM_IO_BUFFERS), 0);
    io_uring_buf_ring_advance(buf_ring_, 1);
}



