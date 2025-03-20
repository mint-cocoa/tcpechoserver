#include "include/Session.h"
#include "include/Logger.h"
#include "include/SessionManager.h"
#include "include/EPollBuffer.h"
#include <sys/socket.h>
#include <stdexcept>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sstream>

// 메시지 관련 상수
#define CHAT_MESSAGE_HEADER_SIZE sizeof(ChatMessageHeader)
#define MAX_MESSAGE_SIZE 1021
#define BUFFER_SIZE (CHAT_MESSAGE_HEADER_SIZE + MAX_MESSAGE_SIZE)

Session::Session(int32_t id) : session_id_(id) {
    // EPoll 인스턴스 생성
    epoll_ = std::make_unique<EPoll>();
    epoll_->initEPoll();  // 명시적 초기화 호출
    
    LOG_INFO("[Session ", id, "] Created with epoll instance");
}

Session::~Session() {
    try {
        // 먼저 모든 클라이언트 연결 종료 (개별 핸들링)
        auto clients = client_sockets_;  // 복사본 만들기
        for (const auto& [fd, socket] : clients) {
            try {
                if (socket && socket->isValid()) {
                    // close(fd) 직접 호출 대신 안전한 종료 처리
                    epoll_->removeEvent(fd);
                    close(fd);
                }
            } catch (...) {
                // 개별 예외 무시 (전체 정리 계속 진행)
            }
        }
        
        // 클라이언트 소켓 맵 정리
        client_sockets_.clear();
        
        // EPoll 인스턴스 정리 (마지막에 수행)
        epoll_.reset();
        
        LOG_INFO("[Session ", session_id_, "] Destroyed");
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Error during destruction: ", e.what());
    } catch (...) {
        LOG_ERROR("[Session ", session_id_, "] Unknown error during destruction");
    }
}

std::set<int32_t> Session::getClientFds() const {
    std::set<int32_t> fds;
    for (const auto& [fd, _] : client_sockets_) {
        fds.insert(fd);
    }
    return fds;
}

void Session::addClient(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to add invalid client socket");
        return;
    }
    
    int client_fd = client_socket->getSocketFd();
    
    // 클라이언트 소켓을 읽기 이벤트로 등록
    epoll_->prepareRead(client_fd);
    
    // 세션의 클라이언트 목록에 추가
    client_sockets_[client_fd] = client_socket;
    LOG_INFO("[Session ", session_id_, "] Added client ", client_fd, ", total clients: ", client_sockets_.size());
}

void Session::removeClient(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to remove invalid client socket");
        return;
    }
    
    int client_fd = client_socket->getSocketFd();
    
    // 세션의 클라이언트 목록에서 제거
    size_t count = client_sockets_.erase(client_fd);
    if (count > 0) {
        // EPoll에서 클라이언트 소켓 제거
        epoll_->removeEvent(client_fd);
        LOG_INFO("[Session ", session_id_, "] Removed client ", client_fd, ", remaining clients: ", client_sockets_.size());
    } else {
        LOG_DEBUG("[Session ", session_id_, "] Client ", client_fd, " not found in session");
    }
}

// epoll 기반 이벤트 처리
bool Session::processEvents(int timeout_ms) {
    if (client_sockets_.empty()) {
        return false;
    }
    
    // EPoll 이벤트 대기
    int num_events = epoll_->waitForEvents(50);  // 50ms 타임아웃
    
    if (num_events < 0) {
        if (errno == EINTR) {
            // 시그널에 의한 인터럽트는 무시
            return false;
        }
        LOG_ERROR("[Session ", session_id_, "] waitForEvents failed: ", strerror(errno));
        return false;
    } else if (num_events == 0) {
        // 타임아웃, 이벤트 없음
        return false;
    }
    
    // 이벤트 가져오기
    epoll_event events[EPoll::MAX_EVENTS];
    int event_count = epoll_->getEvents(events, EPoll::MAX_EVENTS);
    
    if (event_count == 0) {
        return false;
    }
    
    for (int i = 0; i < event_count; ++i) {
        int client_fd = events[i].data.fd;
        
        // 클라이언트 소켓 찾기
        auto it = client_sockets_.find(client_fd);
        if (it == client_sockets_.end()) {
            LOG_ERROR("[Session ", session_id_, "] Cannot find socket for client_fd ", client_fd);
            // EPoll에서 제거
            epoll_->removeEvent(client_fd);
            continue;
        }
        
        SocketPtr client_socket = it->second;
        
        // 연결 종료/오류 이벤트 확인
        if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
            LOG_INFO("[Session ", session_id_, "] Client ", client_fd, " disconnected");
            handleClose(client_socket);
            continue;
        }
        
        // 쓰기 이벤트 처리
        if (events[i].events & EPOLLOUT) {
            handleWrite(client_socket);
        }
        
        // 읽기 이벤트 처리
        if (events[i].events & EPOLLIN) {
            handleRead(client_socket);
        }
    }
    
    return true;
}

void Session::handleRead(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket");
        return;
    }
    
    int client_fd = client_socket->getSocketFd();
    EPollBuffer& buffer_manager = epoll_->getBufferManager();
    
    // 무한 루프로 읽을 수 있는 모든 데이터 처리
    // 최대 읽기 시도 횟수 제한
    const int MAX_READ_ATTEMPTS = 100;
    int read_attempts = 0;
    
    while (read_attempts++ < MAX_READ_ATTEMPTS) {
        // 버퍼가 없으면 이벤트 무시
        if (!buffer_manager.hasAvailableBuffers()) {
            LOG_ERROR("[Session ", session_id_, "] Read error due to no buffers on fd: ", client_fd);
            break;
        }
        
        // 버퍼 할당
        IOBuffer io_buffer = buffer_manager.allocateBuffer();
        if (io_buffer.data == nullptr) {
            LOG_ERROR("[Session ", session_id_, "] Failed to allocate buffer for reading");
            break;
        }
        
        // 데이터 읽기
        const auto num_bytes_read = buffer_manager.readToBuffer(client_fd, io_buffer);
        if (num_bytes_read == 0) {
            // EOF 또는 EAGAIN - 더 이상 읽을 데이터 없음
            buffer_manager.releaseBuffer(io_buffer.buffer_id);
            break;
        }
        
        if (num_bytes_read < 0) {
            // 오류 발생
            buffer_manager.releaseBuffer(io_buffer.buffer_id);
            
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 읽을 데이터가 없음 - 정상
                break;
            } else {
                // 실제 오류
                LOG_ERROR("[Session ", session_id_, "] Read error on fd ", client_fd, ": ", strerror(errno));
                
                // 연결 종료
                handleClose(client_socket);
                return;
            }
        }
        
        LOG_INFO("[Session ", session_id_, "] Read ", num_bytes_read, " bytes on fd: ", client_fd);
        
        // 메시지 처리
        if (num_bytes_read >= static_cast<ssize_t>(CHAT_MESSAGE_HEADER_SIZE)) {
            ChatMessage* message = reinterpret_cast<ChatMessage*>(io_buffer.data);
            
            // 헤더와 데이터 길이 확인
            if (message->header.length <= MAX_MESSAGE_SIZE &&
                num_bytes_read >= static_cast<ssize_t>(CHAT_MESSAGE_HEADER_SIZE + message->header.length)) {
                
                // 메시지 처리 (에코 서버로 작동)
                LOG_DEBUG("[Session ", session_id_, "] Processing message type ", static_cast<int>(message->header.type), 
                         ", length: ", message->header.length);
                
                // 에코: 메시지를 복사하고 타입을 SERVER_ECHO로 변경하여 클라이언트에게 전송
                ChatMessage echo_message;
                echo_message.header.type = MessageType::SERVER_ECHO;  // 서버 에코 타입으로 변경
                echo_message.header.length = message->header.length;
                
                // 메시지 내용 복사
                if (message->header.length > 0) {
                    memcpy(echo_message.data, message->data, message->header.length);
                }
                
                size_t total_size = CHAT_MESSAGE_HEADER_SIZE + echo_message.header.length;
                LOG_INFO("[Session ", session_id_, "] Echoing message of size ", total_size, " to client ", client_fd);
                
                if (!epoll_->prepareWrite(client_fd, &echo_message, total_size)) {
                    LOG_ERROR("[Session ", session_id_, "] Failed to prepare write for echo response");
                } else {
                    LOG_DEBUG("[Session ", session_id_, "] Echo response queued successfully");
                    
                    // 쓰기 이벤트를 활성화 (EPOLLOUT 추가)
                    if (!epoll_->modifyEvent(client_fd, EPoll::BASE_EVENTS | EPOLLOUT)) {
                        LOG_ERROR("[Session ", session_id_, "] Failed to modify events for write");
                    }
                }
            } else {
                LOG_ERROR("[Session ", session_id_, "] Invalid message size: header length=", message->header.length, 
                         ", bytes read=", num_bytes_read);
            }
        } else {
            LOG_ERROR("[Session ", session_id_, "] Incomplete message header: ", num_bytes_read, " bytes");
        }
        
        // 버퍼 해제
        buffer_manager.releaseBuffer(io_buffer.buffer_id);
        
        // 버퍼 크기보다 적게 읽었다면 모든 데이터를 읽은 것으로 간주
        if (num_bytes_read < static_cast<ssize_t>(buffer_manager.getBufferSize())) {
            break;
        }
    }
    
    if (read_attempts >= MAX_READ_ATTEMPTS) {
        LOG_WARN("[Session ", session_id_, "] Maximum read attempts reached for fd: ", client_fd);
    }
}

void Session::handleWrite(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket for write");
        return;
    }
    
    int client_fd = client_socket->getSocketFd();
    EPollBuffer& buffer_manager = epoll_->getBufferManager();
    
    // 대기 중인 모든 데이터 전송
    bool need_more_write = false;
    
    while (buffer_manager.hasDataToWrite(client_fd)) {
        IOBuffer& current_buffer = buffer_manager.getNextBufferToWrite(client_fd);
        if (!current_buffer.data) {
            LOG_ERROR("[Session ", session_id_, "] Invalid buffer for write");
            break;
        }
        
        // 버퍼 데이터 쓰기
        ssize_t written = buffer_manager.writeFromBuffer(client_fd, current_buffer);
        
        if (written < 0) {
            // 심각한 오류
            LOG_ERROR("[Session ", session_id_, "] Write error on fd ", client_fd);
            handleClose(client_socket);
            return;
        } else if (written == 0) {
            // EAGAIN - 지금은 쓸 수 없음, 나중에 다시 시도
            need_more_write = true;
            break;
        }
        
        // 버퍼의 모든 데이터를 썼는지 확인
        if (current_buffer.write_offset >= current_buffer.length) {
            LOG_DEBUG("[Session ", session_id_, "] Completed writing buffer ", current_buffer.buffer_id, 
                     " for client ", client_fd);
            
            // 처리 완료된 버퍼 제거 및 해제
            buffer_manager.removeProcessedBuffer(client_fd);
        } else {
            LOG_DEBUG("[Session ", session_id_, "] Partial write of buffer ", current_buffer.buffer_id, 
                     " for client ", client_fd, " (", current_buffer.write_offset, "/", current_buffer.length, ")");
            
            // 부분 쓰기, 나중에 계속 처리
            need_more_write = true;
            break;
        }
    }
    
    // 대기 중인 데이터가 있으면 EPOLLOUT 유지, 없으면 제거
    if (need_more_write || buffer_manager.hasDataToWrite(client_fd)) {
        if (!epoll_->modifyEvent(client_fd, EPoll::BASE_EVENTS | EPOLLOUT)) {
            LOG_ERROR("[Session ", session_id_, "] Failed to modify events to keep EPOLLOUT");
        }
    } else {
        if (!epoll_->modifyEvent(client_fd, EPoll::BASE_EVENTS)) {
            LOG_ERROR("[Session ", session_id_, "] Failed to modify events to remove EPOLLOUT");
        }
    }
}

void Session::handleClose(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket for close");
        return;
    }
    
    int client_fd = client_socket->getSocketFd();
    LOG_INFO("[Session ", session_id_, "] Closing connection for fd ", client_fd);
    
    try {
        // EPoll에서 먼저 이벤트 제거 (예외 방지)
        if (epoll_) {
            epoll_->removeEvent(client_fd);
        }
        
        // 세션에서 클라이언트 제거
        client_sockets_.erase(client_fd);
        
        // 소켓 자원 정리
        if (epoll_) {
            epoll_->prepareClose(client_fd);
        } else {
            // epoll이 이미 해제된 경우 직접 소켓 닫기
            close(client_fd);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Error during client close: ", e.what());
    }
}

void Session::sendMessage(SocketPtr client_socket, MessageType msg_type, const void* data, size_t length) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Attempted to send message to invalid client socket");
        throw std::runtime_error("Invalid client socket");
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    try {
        // 메시지 구성
        ChatMessage message;
        message.header.type = msg_type;
        message.header.length = static_cast<uint16_t>(length);
        
        if (length > MAX_MESSAGE_SIZE) {
            throw std::runtime_error("메시지 크기 초과");
        }
        
        // 페이로드 복사
        if (length > 0 && data != nullptr) {
            memcpy(message.data, data, length);
        }
        
        // 실제 메시지 크기 (헤더 + 페이로드)
        size_t total_size = CHAT_MESSAGE_HEADER_SIZE + length;
        
        // EPoll을 통해 쓰기 작업 요청
        epoll_->prepareWrite(client_fd, &message, total_size);
        
        LOG_DEBUG("[Session ", session_id_, "] Sent message type ", static_cast<int>(msg_type),
                  " to client ", client_fd, ", length: ", length);
    }
    catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Send failed: ", e.what());
        // 오류 시 클라이언트 연결 종료
        handleClose(client_socket);
        throw; // 호출자에게 오류 전파
    }
}

void Session::processMessage(SocketPtr client_socket, const ChatMessage* message) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket for message processing");
        return;
    }
    
    try {
        // 메시지 타입에 따라 처리
        switch (message->header.type) {
            case MessageType::CLIENT_JOIN:
                handleJoinSession(client_socket, message);
                break;
                
            case MessageType::CLIENT_LEAVE:
                handleLeaveSession(client_socket, message);
                break;
                
            case MessageType::CLIENT_CHAT:
                handleChatMessage(client_socket, message);
                break;
                
            default:
                LOG_ERROR("[Session ", session_id_, "] Unhandled message type: ", static_cast<int>(message->header.type));
                // 알 수 없는 메시지 타입은 무시
                break;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Message processing error: ", e.what());
    }
}

void Session::handleJoinSession(SocketPtr client_socket, const ChatMessage* message) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket for JOIN");
        return;
    }
    
    LOG_INFO("[Session ", session_id_, "] Client ", client_socket->getSocketFd(), " requested JOIN");
    
    try {
        // 이미 이 세션에 있는지 확인
        int32_t client_fd = client_socket->getSocketFd();
        auto it = client_sockets_.find(client_fd);
        
        if (it != client_sockets_.end()) {
            // 이미 세션에 속해 있음
            LOG_DEBUG("[Session ", session_id_, "] Client ", client_fd, " already in this session");
            
            std::stringstream ss;
            ss << "Already in session " << session_id_;
            std::string msg = ss.str();
            sendMessage(client_socket, MessageType::SERVER_ACK, msg.c_str(), msg.length());
            return;
        }
        
        // 대상 세션으로 이동 처리
        int32_t target_session_id = session_id_;
        if (message->header.length >= sizeof(int32_t)) {
            memcpy(&target_session_id, message->data, sizeof(int32_t));
        }
        
        onClientJoinSession(client_socket, target_session_id);
    } catch (const std::exception& e) {
        LOG_ERROR("[Session ", session_id_, "] Error handling JOIN: ", e.what());
        
        std::stringstream ss;
        ss << "Failed to join session: " << e.what();
        std::string error_message = ss.str();
        sendMessage(client_socket, MessageType::SERVER_ERROR, error_message.c_str(), error_message.length());
    }
}

void Session::handleLeaveSession(SocketPtr client_socket, const ChatMessage* /* message */) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket for LEAVE");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_INFO("[Session ", session_id_, "] Client ", client_fd, " is leaving the session");
    
    // 기본 세션으로 복귀시키기
    handleClose(client_socket);
}

void Session::handleChatMessage(SocketPtr client_socket, const ChatMessage* message) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[Session ", session_id_, "] Invalid client socket for CHAT");
        return;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    LOG_INFO("[Session ", session_id_, "] Received chat message from client ", client_fd, 
             ", length: ", message->header.length);
    
    // 송신자에게 에코 메시지 전송
    sendMessage(client_socket, MessageType::SERVER_ECHO, 
                message->data, message->header.length);
}

void Session::broadcastMessage(SocketPtr sender_socket, const ChatMessage* message) {
    if (!sender_socket || !sender_socket->isValid() || !message) {
        return;
    }
    
    int32_t sender_fd = sender_socket->getSocketFd();
    
    // 메시지 복사하여 서버 메시지로 변환
    ChatMessage broadcast_msg;
    broadcast_msg.header.type = MessageType::SERVER_CHAT;
    broadcast_msg.header.length = message->header.length;
    memcpy(broadcast_msg.data, message->data, message->header.length);
    
    size_t total_size = CHAT_MESSAGE_HEADER_SIZE + message->header.length;
    
    // 모든 클라이언트에게 전송 (송신자 제외)
    for (const auto& [fd, client_socket] : client_sockets_) {
        if (fd != sender_fd && client_socket && client_socket->isValid()) {
            // EPoll을 통해 쓰기 작업 수행 (버퍼는 즉시 해제됨)
            epoll_->prepareWrite(fd, &broadcast_msg, total_size);
        }
    }
    
    LOG_DEBUG("[Session ", session_id_, "] Broadcasted message from client ", sender_fd, 
               " to ", client_sockets_.size() - 1, " other clients");
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


