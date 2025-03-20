#pragma once
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include "Socket.h"
#include "EPoll.h"
#include "Context.h"

class Session {
public:
    explicit Session(int32_t id);
    ~Session();
    
    int32_t getSessionId() const { return session_id_; }
    std::set<int32_t> getClientFds() const;
    
    void addClient(SocketPtr client_socket);
    void removeClient(SocketPtr client_socket);
    size_t getClientCount() const { return client_sockets_.size(); }
    
    // 이벤트 처리 메서드
    bool processEvents(int timeout_ms);
    
private:
    // I/O 이벤트 핸들러 (EPoll의 이벤트를 처리)
    void handleRead(SocketPtr client_socket);
    void handleWrite(SocketPtr client_socket);
    void handleClose(SocketPtr client_socket);
    
    // 메시지 처리 메서드들
    void processMessage(SocketPtr client_socket, const ChatMessage* message);
    void handleJoinSession(SocketPtr client_socket, const ChatMessage* message);
    void handleLeaveSession(SocketPtr client_socket, const ChatMessage* message);
    void handleChatMessage(SocketPtr client_socket, const ChatMessage* message);
    
    // 메시지 전송 메서드
    void sendMessage(SocketPtr client_socket, MessageType msg_type, const void* data, size_t length);
    void broadcastMessage(SocketPtr sender_socket, const ChatMessage* message);
    
    // 세션 이동 처리
    void onClientJoinSession(SocketPtr client_socket, int32_t target_session_id);
    
    int32_t session_id_;
    epoll_event events[EPoll::MAX_EVENTS];
    // 클라이언트 소켓 맵 (file descriptor -> Socket 객체)
    std::unordered_map<int32_t, SocketPtr> client_sockets_;
    // EPoll 인스턴스
    std::unique_ptr<EPoll> epoll_;
    int event_count;
};
