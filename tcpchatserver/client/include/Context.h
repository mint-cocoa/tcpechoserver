#pragma once
#include <liburing.h>
#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)  // 1바이트 정렬 시작

enum class MessageType : uint8_t {
    // 서버 메시지 (0x00 ~ 0x0F)
    SERVER_ACK = 0x01,           // 일반적인 응답
    SERVER_ERROR = 0x02,         // 에러
    SERVER_CHAT = 0x03,          // 채팅 메시지
    SERVER_NOTIFICATION = 0x04,  // 시스템 알림
    SERVER_ECHO = 0x05,          // 에코 메시지
    
    // 클라이언트 메시지 (0x10 ~ 0x1F)
    CLIENT_JOIN = 0x11,          // 세션 참가
    CLIENT_LEAVE = 0x12,         // 세션 퇴장
    CLIENT_CHAT = 0x13,          // 채팅 메시지
    CLIENT_COMMAND = 0x14        // 명령어 (상태변경, 귓속말 등)
};

enum class OperationType : uint8_t {
    ACCEPT = 1,
    READ = 2,
    WRITE = 3,
    CLOSE = 4
};

// 서버 내부에서 사용하는 작업 컨텍스트
struct Operation {
    int32_t client_fd;        // 4 bytes
    OperationType op_type;    // 1 byte
    uint16_t buffer_idx;      // 2 bytes
};

// io_uring_cqe에서 Operation 컨텍스트를 추출하는 인라인 함수
inline Operation getContext(io_uring_cqe* cqe) {
    Operation ctx{};
    auto* buffer = reinterpret_cast<uint8_t*>(&cqe->user_data);
    
    ctx.client_fd = *(reinterpret_cast<int32_t*>(buffer));
    buffer += 4;
    ctx.op_type = static_cast<OperationType>(*buffer);
    buffer += 1;
    ctx.buffer_idx = *(reinterpret_cast<uint16_t*>(buffer));
    
    return ctx;
}

// 네트워크로 전송되는 메시지 헤더 구조체
struct ChatMessageHeader {
    MessageType type;         // 1 byte
    uint16_t length;          // 2 bytes - 페이로드 길이
};

// 고정 크기 버퍼를 사용하는 메시지 구조체 (내부용)
struct ChatMessage {
    ChatMessageHeader header;  // 3 bytes
    char data[1021];           // 1021 bytes의 최대 페이로드
    
    // 헤더와 실제 데이터를 합한 전체 크기 계산
    inline size_t getTotalSize() const {
        return sizeof(ChatMessageHeader) + header.length;
    }
    
    // data 배열에 저장된 실제 데이터의 크기(바이트)
    inline uint16_t getDataSize() const {
        return header.length;
    }
    
    // type과 length만 초기화하는 생성자
    inline void init(MessageType type, uint16_t dataLength) {
        header.type = type;
        header.length = dataLength;
    }
};

#pragma pack(pop)   // 정렬 설정 복원

static constexpr size_t MAX_MESSAGE_SIZE = 1021;  // 최대 데이터 크기
static constexpr size_t CHAT_MESSAGE_HEADER_SIZE = sizeof(ChatMessageHeader);  // 헤더 크기