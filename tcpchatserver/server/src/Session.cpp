#include "Session.h"
#include "IOUring.h"
#include "Context.h"
#include "Utils.h"
#include "Logger.h"
#include "SessionManager.h"
#include "SocketManager.h"
#include <sstream>
#include <string.h>
#include <functional>

Session::Session(int32_t id) : session_id_(id) {
    // 세션별 전용 IOUring 생성 (내부적으로 초기화 수행)
    try {
        io_ring_ = std::make_unique<IOUring>();
        LOG_INFO("[Session ", id, "] Created with dedicated IOUring");
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", id, "] Failed to create IOUring: ", e.what());
        throw std::runtime_error("Failed to create session " + std::to_string(id));
    }
}

Session::~Session() {
    try {
        // First close all client connections
        // client_sockets_ 맵에서 소켓 포인터 복사본 생성
        std::vector<SocketPtr> client_sockets;
        for (const auto& pair : client_sockets_) {
            client_sockets.push_back(pair.second);
        }
        
        for (SocketPtr socket : client_sockets) {
            try {
                handleClose(socket);
            } catch (const std::exception& e) {
                LOG_ERROR("[Session ", session_id_, "] Error closing client ", 
                         socket->getSocketFd(), ": ", e.what());
            }
        }
        
        // Clear client collections
        client_sockets_.clear();
        
        // Release IOUring (will call IOUring's destructor which handles its own cleanup)
        io_ring_.reset();
        
        LOG_INFO("[Session ", session_id_, "] Destroyed successfully");
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Error during destruction: ", e.what());
    }
}

// getClientFds 메소드 구현 추가: client_sockets_의 키를 반환
std::set<int32_t> Session::getClientFds() const {
    std::set<int32_t> result;
    for (const auto& pair : client_sockets_) {
        result.insert(pair.first);
    }
    return result;
}

void Session::addClient(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to add invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    if (client_fd < 0) {
        LOG_ERROR("[Session ", session_id_, "] Client socket has invalid file descriptor");
        return;
    }
    
    try {
        client_sockets_[client_fd] = client_socket;
        LOG_INFO("[Session ", session_id_, "] Added client ", client_fd);
        
        // 클라이언트가 추가되면 즉시 읽기 작업 준비
        // 직접 IOUring의 prepareRead 호출
        if (io_ring_) {
            LOG_TRACE("[Session ", session_id_, "] Preparing read for client ", client_fd);
            io_ring_->prepareRead(client_fd);
        } else {
            LOG_ERROR("[Session ", session_id_, "] IOUring is null in addClient");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Exception adding client ", client_fd, ": ", e.what());
    }
}

void Session::removeClient(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to remove invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    if (client_fd < 0) {
        LOG_ERROR("[Session ", session_id_, "] Client socket has invalid file descriptor");
        return;
    }
    
    try {
        client_sockets_.erase(client_fd);
        LOG_INFO("[Session ", session_id_, "] Removed client ", client_fd);
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Exception removing client ", client_fd, ": ", e.what());
    }
}

bool Session::processEvents() {
    if (client_sockets_.empty() || !io_ring_) {
        return false;
    }
    
    unsigned num_cqes = io_ring_->peekCQE(cqes_);
    
    if (num_cqes == 0) {
        const int result = io_ring_->submitAndWait();
        if (result == -EINTR) {
            return true; // 인터럽트는 오류가 아님
        }
        if (result < 0) {
            LOG_ERROR("[Session ", session_id_, "] io_uring_submit_and_wait failed: ", result);
            return false;
        }
        num_cqes = io_ring_->peekCQE(cqes_);
    }
    
    // CQE 배치 크기 제한 확인
    if (num_cqes > CQE_BATCH_SIZE) {
        LOG_ERROR("[Session ", session_id_, "] Excessive CQEs returned: ", num_cqes, ", limiting to ", CQE_BATCH_SIZE);
        num_cqes = CQE_BATCH_SIZE;
    }
    
    for (unsigned i = 0; i < num_cqes; ++i) {
        io_uring_cqe* cqe = cqes_[i];
        if (!cqe) {
            LOG_ERROR("[Session ", session_id_, "] Null CQE at index ", i);
            continue;
        }
        
        Operation ctx = getContext(cqe);
        
        // 허용 가능한 오류인 경우 계속 진행
        const bool isFatalError = (cqe->res < 0 && 
                                  cqe->res != -EAGAIN && 
                                  cqe->res != -ECONNRESET);
        if (cqe->res == -EBADF) {
        // 이미 닫힌 소켓에 대한 작업이므로 오류 로그만 남기고 계속 진행
        LOG_DEBUG("[Session ", session_id_, "] Operation on already closed socket: fd=", ctx.client_fd);
        continue;
}
        if (isFatalError) {
            LOG_ERROR("[Session ", session_id_, "] Fatal error in CQE: ", cqe->res);
            continue; // 치명적 오류가 있더라도 다른 CQE는 처리
        }
        
        switch (ctx.op_type) {
            case OperationType::READ:
                handleRead(cqe, ctx);
                break;
            case OperationType::WRITE:
                handleWrite(cqe, ctx);
                break;
            case OperationType::CLOSE:
                break;
            default:
                LOG_ERROR("[Session ", session_id_, "] Unknown operation type: ", static_cast<int>(ctx.op_type));
                break;
        }
    }
    
    io_ring_->advanceCQ(num_cqes);
    
    // 모든 작업 처리 후 한 번만 submit 호출
    io_ring_->submit();
    return true;
}

void Session::handleRead(io_uring_cqe* cqe, const Operation& ctx) {
    const int result = cqe->res;
    int client_fd = ctx.client_fd;
    const uint16_t buffer_idx = ctx.buffer_idx;
    bool closed = false;

    LOG_TRACE("[Session ", session_id_, "] Read result for client ", client_fd, ": ", result);
    
    // client_fd로 Socket 객체 찾기
    auto it = client_sockets_.find(client_fd);
    if (it == client_sockets_.end()) {
        LOG_ERROR("[Session ", session_id_, "] Cannot find socket for client_fd ", client_fd);
        return;
    }
    SocketPtr client_socket = it->second;

    if (result == 0 || result == -EBADF || result == -ECONNRESET) {
        // EOF, 파일 디스크립터 오류 또는 연결 재설정
        LOG_INFO("[Session ", session_id_, "] Client ", client_fd, " disconnected");
        handleClose(client_socket);
        closed = true;
        return;
    } else if (result < 0) {
        // 기타 오류
        LOG_ERROR("[Session ", session_id_, "] Read error for client ", client_fd, ": ", -result);
        
        if (result == -ENOBUFS) {
            // 버퍼 부족 - 이 경우 일반적으로 재시도 가능
            LOG_WARN("[Session ", session_id_, "] No buffer available for client ", client_fd);
        } else {
            // 다른 오류는 연결 종료로 처리
            handleClose(client_socket);
            closed = true;
        }
        return;
    }

    // 데이터 읽기 성공
    LOG_DEBUG("[Session ", session_id_, "] Read ", result, " bytes from client ", client_fd);
    
    // IORING_CQE_F_BUFFER 플래그 확인 (버퍼 데이터가 있는지)
    if (!(cqe->flags & IORING_CQE_F_BUFFER)) {
        LOG_ERROR("[Session ", session_id_, "] No buffer flag set for client ", client_fd);
        handleClose(client_socket);
        closed = true;
        return;
    }
    
    
    // 버퍼 매니저에서 데이터 주소 가져오기
    auto& buffer_manager = io_ring_->getBufferManager();
    uint8_t* addr = buffer_manager.getBufferAddr(buffer_idx, buffer_manager.getBaseAddr());
    
    if (!addr) {
        LOG_ERROR("[Session ", session_id_, "] Failed to get buffer address for index ", buffer_idx);
        handleClose(client_socket);
        closed = true;
        return;
    }

    // 최소한 헤더는 읽었는지 확인
    if (result < CHAT_MESSAGE_HEADER_SIZE) {
        LOG_ERROR("[Session ", session_id_, "] Incomplete message header from client ", client_fd, 
                ": received only ", result, " bytes");
        handleClose(client_socket);
        closed = true;
        return;
    }
    
    auto* message = reinterpret_cast<ChatMessage*>(addr);
    
    // 메시지 검증
    uint8_t msg_type = static_cast<uint8_t>(message->header.type);
    uint8_t client_min_type = static_cast<uint8_t>(MessageType::CLIENT_JOIN);
    uint8_t client_max_type = static_cast<uint8_t>(MessageType::CLIENT_COMMAND);
    
    if (msg_type < client_min_type || msg_type > client_max_type) {
        LOG_ERROR("[Session ", session_id_, "] Invalid message type from client ", client_fd, 
                ": 0x", std::hex, static_cast<int>(msg_type), std::dec);
        handleClose(client_socket);
        closed = true;
    } else if (message->header.length > MAX_MESSAGE_SIZE) {
        LOG_ERROR("[Session ", session_id_, "] Message too long from client ", client_fd, 
                ": ", message->header.length, " bytes (max: ", MAX_MESSAGE_SIZE, ")");
        handleClose(client_socket);
        closed = true;
    } else if (message->header.length == 0) {
        LOG_ERROR("[Session ", session_id_, "] Empty message from client ", client_fd);
        handleClose(client_socket);
        closed = true;
    // 전체 메시지가 완전히 수신되었는지 확인
    } else if (result < (CHAT_MESSAGE_HEADER_SIZE + message->header.length)) {
        LOG_ERROR("[Session ", session_id_, "] Incomplete message body from client ", client_fd, 
                ": expected ", (CHAT_MESSAGE_HEADER_SIZE + message->header.length),
                " bytes, received only ", result, " bytes");
        handleClose(client_socket);
        closed = true;
    } else {
        // 메시지 직접 처리
        processMessage(client_socket, message, buffer_idx);
    }
    
    // 연결이 종료되지 않았고, 더 이상 데이터가 없으면 새 recv 작업 추가
    if (!closed && !(cqe->flags & IORING_CQE_F_MORE)) {
        if (io_ring_) {
            io_ring_->prepareRead(client_fd);
        }
    }
}

void Session::handleWrite(io_uring_cqe* cqe, const Operation& ctx) {
    if (cqe->res < 0 && cqe->res != -EAGAIN && cqe->res != -ECONNRESET) {
        LOG_ERROR("[Session ", session_id_, "] Write failed for client ", ctx.client_fd, ": ", -cqe->res);
        // client_fd로 Socket 객체 찾기
        auto it = client_sockets_.find(ctx.client_fd);
        if (it != client_sockets_.end()) {
            handleClose(it->second);
        }
        return;
    }
    
    // 버퍼 사용 완료 처리
    io_ring_->handleWriteComplete(ctx.client_fd, ctx.buffer_idx, cqe->res);
}

void Session::handleClose(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to close invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_INFO("[Session ", session_id_, "] Closing connection for client ", client_fd);
    
    // 세션에서 클라이언트 제거
    removeClient(client_socket);
    
    // SessionManager에서도 클라이언트-세션 매핑 제거
    SessionManager::getInstance().removeSession(client_fd);
    
    // 소켓 닫기 작업 예약
    if (io_ring_) {
        io_ring_->prepareClose(client_fd);
    }
}

void Session::onClientJoinSession(SocketPtr client_socket, int32_t target_session_id) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to move invalid client socket");
        throw std::runtime_error("Invalid client socket");
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_DEBUG("[Session ", session_id_, "] Processing session join request from client ", client_fd, 
             " to session ", target_session_id);
    
    try {
        // 현재 세션에서 제거
        removeClient(client_socket);
        
        // 세션매니저를 통해 새 세션으로 이동
        auto& sessionManager = SessionManager::getInstance();
        auto targetSession = sessionManager.getSessionByIndex(target_session_id);
        if (!targetSession) {
            throw std::runtime_error("요청한 세션을 찾을 수 없음");
        }
        
        // 세션 매니저에서 클라이언트 매핑 제거
        sessionManager.removeSession(client_fd);
        
        // 새 세션에 클라이언트 추가
        targetSession->addClient(client_socket);
        
        LOG_DEBUG("[Session ", session_id_, "] Client ", client_fd, " moved to session ", target_session_id);
    }
    catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Error moving client to session: ", e.what());
        throw; // 호출자에게 예외 전파
    }
}

void Session::sendMessage(SocketPtr client_socket, MessageType msg_type, const void* data, size_t length, uint16_t buffer_idx) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to send message to invalid client socket");
        throw std::runtime_error("Invalid client socket");
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    try {
        // 수신된 버퍼 재활용
        ChatMessage* mutable_message = const_cast<ChatMessage*>(static_cast<const ChatMessage*>(data));
        mutable_message->init(msg_type, static_cast<uint16_t>(length));
        
        if (length > MAX_MESSAGE_SIZE) {
            throw std::runtime_error("메시지 크기 초과");
        }
        
        // 실제 메시지 크기만큼만 전송 (헤더 + 페이로드)
        size_t total_size = mutable_message->getTotalSize();
        
        if (io_ring_) {
            io_ring_->prepareWrite(client_fd, mutable_message, total_size, buffer_idx);
            LOG_DEBUG("[Session ", session_id_, "] Sending message type ", static_cast<int>(msg_type),
                     " to client ", client_fd, ", length: ", length);
        } else {
            LOG_ERROR("[Session ", session_id_, "] IOUring is null in sendMessage");
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Send failed: ", e.what());
        throw;
    }
}

void Session::processMessage(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to process message from invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_DEBUG("[Session ", session_id_, "] Processing message type ", static_cast<int>(message->header.type), 
              " from client ", client_fd);
              
    switch (message->header.type) {
        case MessageType::CLIENT_JOIN:
            handleJoinSession(client_socket, message, buffer_idx);
            break;
        case MessageType::CLIENT_LEAVE:
            handleLeaveSession(client_socket, message, buffer_idx);
            break;
        case MessageType::CLIENT_CHAT:
            handleChatMessage(client_socket, message, buffer_idx);
            break;
        default:
            LOG_ERROR("[Session ", session_id_, "] Unknown message type: ", static_cast<int>(message->header.type));
            break;
    }
    
    total_messages_++;
}

void Session::handleJoinSession(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to process JOIN from invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_DEBUG("[Session ", session_id_, "] Processing JOIN request from client ", client_fd);
    
    if (!message || message->header.length < sizeof(int32_t)) {
        LOG_ERROR("[Session ", session_id_, "] Invalid JOIN message format");
        return;
    }

    const int32_t* session_id_ptr = reinterpret_cast<const int32_t*>(message->data);
    int32_t requested_session_id = *session_id_ptr;
    
    LOG_DEBUG("[Session ", session_id_, "] Client ", client_fd, " requesting to join session ", requested_session_id);
    
    try {
        // 현재 세션과 요청한 세션이 동일하면 그대로 유지
        if (requested_session_id == session_id_) {
            std::stringstream ss;
            ss << "Already in session " << session_id_;
            std::string msg = ss.str();
            sendMessage(client_socket, MessageType::SERVER_ACK, msg.c_str(), msg.length(), buffer_idx);
            return;
        }
        
        // 세션 이동 처리
        onClientJoinSession(client_socket, requested_session_id);
        
        // 성공 메시지는 이동된 세션에서 보내지 않음 (클라이언트가 이미 이동됨)
    }
    catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Error joining session: ", e.what());
        
        std::stringstream ss;
        ss << "Failed to join session: " << e.what();
        std::string error_message = ss.str();
        sendMessage(client_socket, MessageType::SERVER_ERROR, error_message.c_str(), error_message.length(), buffer_idx);
    }
}

void Session::handleLeaveSession(SocketPtr client_socket, const ChatMessage* /* message */, uint16_t /* buffer_idx */) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to process LEAVE from invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_INFO("[Session ", session_id_, "] Client ", client_fd, " leaving session");
    handleClose(client_socket);
}

void Session::handleChatMessage(SocketPtr client_socket, const ChatMessage* message, uint16_t buffer_idx) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to process CHAT from invalid client socket");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    
    if (!message || message->header.length == 0 || message->header.length > MAX_MESSAGE_SIZE) {
        LOG_WARN("[Session ", session_id_, "] Invalid message length from client ", client_fd);
        return;
    }
    
    // 메시지 수신 로그
    LOG_INFO("[Session ", session_id_, "] Received chat message from client ", client_fd, 
             ", length: ", message->header.length);
    
    // 송신자에게 에코 메시지 전송
    sendMessage(client_socket, MessageType::SERVER_ECHO, 
               message->data, message->header.length, buffer_idx);
    
    // 여기에 세션 내 다른 모든 클라이언트에게 메시지 전달 로직을 추가할 수 있음
    // 현재는 에코만 구현
} 

