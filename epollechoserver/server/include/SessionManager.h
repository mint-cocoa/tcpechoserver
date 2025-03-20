#pragma once
#include "Session.h"
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <set>
#include <atomic>
#include <thread>
class SessionManager {
public:
    static SessionManager& getInstance() {
        static SessionManager instance;
        return instance;
    }

    void initialize();
    // 쓰레드 수를 설정하는 메서드 추가
    void setThreadCount(unsigned int thread_count) {
        thread_count_ = thread_count > 0 ? thread_count : std::thread::hardware_concurrency();
    }
    void start();
    void stop();
    
    int32_t assignClientToSession(int32_t client_fd);
    void joinSession(int32_t client_fd, int32_t session_id);
    void removeSession(int32_t client_fd);
    std::shared_ptr<Session> getSession(int32_t client_fd);
    std::shared_ptr<Session> getSessionByIndex(size_t index);
    const std::set<int32_t>& getSessionClients(int32_t session_id);
    const std::vector<int32_t>& getAvailableSessions() const { return available_sessions_; }
    
    int32_t assignClientToSession(SocketPtr client_socket);
    void removeSessionByClient(SocketPtr client_socket);

private:
    SessionManager();
    ~SessionManager();
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    
    std::unordered_map<int32_t, std::thread> session_threads_;   

    void sessionWorker(std::shared_ptr<Session> session);
    std::unordered_map<int32_t, std::shared_ptr<Session>> sessions_;  // session_id -> Session
    std::unordered_map<int32_t, int32_t> client_sessions_;           // client_fd -> session_id
    
    std::mutex mutex_;
    size_t next_session_id_{0};
    std::vector<int32_t> available_sessions_;  // 사용 가능한 세션 목록
    
    // 라운드 로빈 분배를 위한 인덱스
    std::atomic<size_t> next_session_index_{0};
    
    // 쓰레드 수 설정
    unsigned int thread_count_{0};
    std::atomic<bool> should_terminate_{false};  
    // 쓰레드 실행 상태
    std::atomic<bool> running_{false};
};