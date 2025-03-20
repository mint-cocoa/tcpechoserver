#pragma once

#include <cstdint>
#include <liburing.h>
#include <iostream>
#include <chrono>
#include <mutex>
#include <iomanip>

class UringBuffer {
public:
 
   
    static constexpr unsigned IO_BUFFER_SIZE = 1024;
    // The number of IO buffers to pre-allocate
    static constexpr uint16_t NUM_IO_BUFFERS = 4096;


    // 생성자 및 소멸자
    explicit UringBuffer(io_uring* ring);
    ~UringBuffer();

    // 버퍼 관리 메서드

    void releaseBuffer(uint16_t idx, uint8_t* buf_base_addr);                        // 버퍼 사용 완료 표시
    uint8_t* getBufferAddr(uint16_t idx, uint8_t* buf_base_addr);                   // 버퍼 주소 반환

    // 버퍼 기본 주소 반환
    uint8_t* getBaseAddr() const { return buffer_base_addr_; }

    UringBuffer(const UringBuffer&) = delete;
    UringBuffer& operator=(const UringBuffer&) = delete;
    UringBuffer(UringBuffer&&) = delete;
    UringBuffer& operator=(UringBuffer&&) = delete;

private:
    // 초기화 메서드
    void initBufferRing();
    

    // 멤버 변수
    io_uring* ring_;                // io_uring 인스턴스 (소유권 없음)
    io_uring_buf_ring* buf_ring_;   // 버퍼 링
    uint8_t* buffer_base_addr_;     // 버퍼 메모리 시작 주소
    const unsigned ring_size_;      // 전체 버퍼 링 크기
}; 