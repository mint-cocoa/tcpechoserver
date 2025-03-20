#pragma once
#include "Session.h"
#include "Socket.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <set>
#include <thread>
#include <atomic>

class SessionManager {
public:
    static SessionManager& getInstance() {
        static SessionManager instance;
        return instance;
    }

    void initialize(unsigned int num_threads = 0);
    void start();
    void stop();
    
    int32_t getNextAvailableSession();
    void removeSession(int32_t client_fd);
    std::shared_ptr<Session> getSession(int32_t client_fd);
    std::shared_ptr<Session> getSessionByIndex(size_t index);
    const std::set<int32_t>& getSessionClients(int32_t session_id);
    const std::vector<int32_t>& getAvailableSessions() const { return available_sessions_; }
    
    // 모든 세션의 이벤트 처리
    bool processEvents();
    
    // 클라이언트를 라운드 로빈 방식으로 세션에 배정
    int32_t assignClientToSession(SocketPtr client_socket);
    

private:
    SessionManager();
    ~SessionManager();
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // 세션별 워커 쓰레드 함수
    void sessionWorker(std::shared_ptr<Session> session);
    
    std::unordered_map<int32_t, std::shared_ptr<Session>> sessions_;  // session_id -> Session
    std::unordered_map<int32_t, int32_t> client_sessions_;           // client_fd -> session_id
    
    // 세션별 쓰레드 관리
    std::unordered_map<int32_t, std::thread> session_threads_;       // session_id -> thread
    std::atomic<bool> should_terminate_{false};                      // 종료 플래그
    
    std::mutex mutex_;
    size_t next_session_id_{0};
    std::vector<int32_t> available_sessions_;  // 사용 가능한 세션 목록
    std::atomic<bool> running_{false};
    
    // 라운드 로빈 분배를 위한 인덱스
    std::atomic<size_t> next_session_index_{0};
};