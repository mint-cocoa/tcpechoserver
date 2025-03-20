#include "ChatClient.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <sys/select.h>

ChatClient::ChatClient() : socket_(-1), running_(false) {}

ChatClient::~ChatClient() {
    disconnect();
}

bool ChatClient::connect(const std::string& host, int port) {
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0) {
        return false;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) <= 0) {
        close(socket_);
        return false;
    }

    if (::connect(socket_, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        close(socket_);
        return false;
    }

    running_ = true;
    
    std::cout << "서버에 연결됨." << std::endl;

    // 메인 루프 시작
    mainLoop();
    return true;
}

void ChatClient::disconnect() {
    if (socket_ >= 0) {
        running_ = false;
        close(socket_);
        socket_ = -1;
    }
}

void ChatClient::mainLoop() {
    fd_set readfds;
    struct timeval tv;
    char buffer[1024];
    uint8_t readBuffer[1024]; // 수신 버퍼 

    while (running_) {
        FD_ZERO(&readfds);
        FD_SET(socket_, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        // timeout 설정 (100ms)
        tv.tv_sec = 0;
        tv.tv_usec = 100000;

        int maxfd = std::max(socket_, STDIN_FILENO) + 1;
        int activity = select(maxfd, &readfds, nullptr, nullptr, &tv);

        if (activity < 0) {
            if (errno != EINTR) {
                break;
            }
            continue;
        }

        // 소켓으로부터 데이터 수신
        if (FD_ISSET(socket_, &readfds)) {
            // 먼저 헤더만 읽어서 메시지 길이 확인
            ChatMessageHeader header;
            ssize_t headerBytesRead = recv(socket_, &header, sizeof(header), MSG_PEEK);
            
            if (headerBytesRead <= 0) {
                break; // 연결 종료 또는 오류
            }
            
            if (headerBytesRead < sizeof(header)) {
                continue; // 헤더가 완전히 도착하지 않음, 다음 루프에서 다시 시도
            }
            
            // 헤더의 length 필드 검증
            if (header.length > MAX_MESSAGE_SIZE) {
                std::string error_msg = "비정상 메시지 수신: type=" + std::to_string(static_cast<int>(header.type)) + 
                    ", length=" + std::to_string(header.length) + 
                    " (최대 허용=" + std::to_string(MAX_MESSAGE_SIZE) + ")";
                std::cerr << error_msg << std::endl;
                
                // 유효하지 않은 데이터 건너뛰기 
                recv(socket_, readBuffer, sizeof(header), 0);
                continue;
            }
            
            // 실제 메시지 크기 계산
            size_t totalMessageSize = sizeof(header) + header.length;
            
            // 전체 메시지 읽기
            ssize_t bytesRead = recv(socket_, readBuffer, totalMessageSize, 0);
            if (bytesRead <= 0) {
                break;
            }
            
            // 전체 메시지 수신 검증
            if (bytesRead < static_cast<ssize_t>(totalMessageSize)) {
                std::cerr << "메시지 일부만 수신됨: " << bytesRead << "/" << totalMessageSize << " bytes" << std::endl;
                continue;
            }
            
            // 메시지 구조체로 변환
            ChatMessage message;
            memcpy(&message.header, readBuffer, sizeof(header));
            memcpy(message.data, readBuffer + sizeof(header), message.header.length);
            
            handleMessage(message);
        }

        // 표준 입력 처리 (필요한 경우)
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buffer, sizeof(buffer), stdin) != nullptr) {
                std::string input(buffer);
                if (!input.empty()) {
                    if (input.back() == '\n') {
                        input.pop_back();
                    }
                    sendChat(input);
                }
            }
        }
    }
    running_ = false;
}

bool ChatClient::joinSession(int32_t sessionId) {
    return sendMessage(MessageType::CLIENT_JOIN, &sessionId, sizeof(sessionId));
}

bool ChatClient::leaveSession() {
    return sendMessage(MessageType::CLIENT_LEAVE, nullptr, 0);
}

bool ChatClient::sendChat(const std::string& message) {
    return sendMessage(MessageType::CLIENT_CHAT, message.c_str(), message.length());
}

bool ChatClient::sendMessage(MessageType type, const void* data, size_t length) {
    if (socket_ < 0 || !running_) {
        return false;
    }

    // 데이터 크기 검증
    if (length > MAX_MESSAGE_SIZE) {
        std::cerr << "메시지 크기 초과: " << length << " > " << MAX_MESSAGE_SIZE << std::endl;
        return false;
    }

    // 실제 전송 크기 계산
    size_t totalSize = sizeof(ChatMessageHeader) + length;
    
    // 버퍼 할당 및 메시지 구성
    uint8_t* buffer = new uint8_t[totalSize];
    ChatMessageHeader* header = reinterpret_cast<ChatMessageHeader*>(buffer);
    header->type = type;
    header->length = static_cast<uint16_t>(length);
    
    // 데이터 복사
    if (data && length > 0) {
        memcpy(buffer + sizeof(ChatMessageHeader), data, length);
    }
    
    // 전송
    ssize_t bytesSent = send(socket_, buffer, totalSize, 0);
    delete[] buffer;
    
    if (bytesSent != static_cast<ssize_t>(totalSize)) {
        std::cerr << "메시지 전송 실패: " << bytesSent << "/" << totalSize << " bytes" << std::endl;
        return false;
    }
    
    return true;
}

void ChatClient::handleMessage(const ChatMessage& message) {
    std::string messageData(message.data, message.header.length);
    
    switch (message.header.type) {
        case MessageType::SERVER_ECHO: {
            // 에코 메시지 (클라이언트가 보낸 메시지를 서버가 그대로 돌려보냄)
            if (messageCallback_) {
                messageCallback_("에코: " + messageData);
            } else {
                std::cout << "에코: " << messageData << std::endl;
            }
            break;
        }
            
        case MessageType::SERVER_NOTIFICATION: {
            // 서버 알림 (세션 참가 등)
            if (messageCallback_) {
                messageCallback_("[알림] " + messageData);
            } else {
                std::cout << "[알림] " << messageData << std::endl;
            }
            break;
        }
            
        default: {
            // 다른 메시지 타입은 단순히 로깅
            std::string log_msg = "메시지 타입 " + std::to_string(static_cast<int>(message.header.type)) + ": " + messageData;
            if (messageCallback_) {
                messageCallback_(log_msg);
            } else {
                std::cout << log_msg << std::endl;
            }
            break;
        }
    }
}