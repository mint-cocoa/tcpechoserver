#pragma once
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include "IOUring.h"
#include "Socket.h"
#include "Context.h"

// 전방 선언
struct io_uring_cqe;
class ChatMessage;

/**
 * @brief 클라이언트 세션 관리를 담당하는 클래스
 * 
 * 이 클래스는 세션에 속한 클라이언트를 관리하고 I/O 작업을 처리합니다.
 * 메시지 처리 로직이 직접 통합되어 있습니다.
 */
class Session {
public:
    static constexpr unsigned CQE_BATCH_SIZE = 256;  // 한 번에 처리할 최대 이벤트 수
    
    explicit Session(int32_t id);
    ~Session();
    
    int32_t getSessionId() const { return session_id_; }
    
    // 이제 소켓 파일 디스크립터 집합을 반환합니다 (하위 호환성 유지)
    std::set<int32_t> getClientFds() const;
    
    void addClient(SocketPtr client_socket);
    void removeClient(SocketPtr client_socket);
    size_t getClientCount() const { return client_sockets_.size(); }
    
    // 이벤트 처리
    bool processEvents();
    
    // 메시지 전송 헬퍼 메서드
    void sendMessage(SocketPtr client_socket, MessageType msg_type, const void* data, size_t length, uint16_t buffer_idx);

    // IOUring 직접 접근자 - 클라이언트 코드가 Session을 통해 IOUring에 접근할 수 있도록 함
    IOUring* getIOUring() { return io_ring_.get(); }
    
    // UringBuffer 접근자
    UringBuffer* getBuffer() { return &io_ring_->getBufferManager(); }

private:
    // I/O 이벤트 핸들러 (IOUring의 이벤트를 처리)
    void handleRead(io_uring_cqe* cqe, const Operation& ctx);
    void handleWrite(io_uring_cqe* cqe, const Operation& ctx);
    void handleClose(SocketPtr client_socket);
    
    // 메시지 처리 메서드들
    void processMessage(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx);
    void handleJoinSession(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx);
    void handleLeaveSession(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx);
    void handleChatMessage(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx);
    
    // 세션 이동 처리
    void onClientJoinSession(SocketPtr client_socket, int32_t target_session_id);
    
    int32_t session_id_;
    // client_fds_ 세트 제거하고 client_sockets_ 맵만 사용
    std::unordered_map<int32_t, SocketPtr> client_sockets_; // 클라이언트 소켓 맵 (file descriptor -> Socket 객체)
    std::unique_ptr<IOUring> io_ring_;  // 세션별 전용 IOUring
    
    // 통계용 변수
    size_t total_messages_{0};
    
    // 반복적으로 사용되는 변수를 멤버로 이동
    io_uring_cqe* cqes_[CQE_BATCH_SIZE];
}; 