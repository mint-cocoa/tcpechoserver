#include "SessionManager.h"
#include "Utils.h"
#include "Logger.h"
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <iomanip>

SessionManager::SessionManager() : running_(false), should_terminate_(false) {
    LOG_INFO("[SessionManager] Initialized");
}

SessionManager::~SessionManager() {
    stop();
}

void SessionManager::initialize(unsigned int num_threads) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 지정된 쓰레드 수 또는 CPU 코어 수만큼 세션 생성 (최소 1개)
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 1;
    }
    
    LOG_INFO("[SessionManager] Initializing with ", num_threads, " sessions");
    
    // 이미 생성된 세션이 있을 경우 모두 정리하고 새로 생성
    sessions_.clear();
    available_sessions_.clear();
    next_session_id_ = 0;
    
    for (unsigned int i = 0; i < num_threads; ++i) {
        int32_t session_id = static_cast<int32_t>(next_session_id_++);
        auto session = std::make_shared<Session>(session_id);
        sessions_[session_id] = session;
        available_sessions_.push_back(session_id);
        LOG_DEBUG("[SessionManager] Created session ", session_id, " with dedicated IOUring");
    }
}

void SessionManager::start() {
    running_ = true;
    should_terminate_ = false;
    
    // 각 세션별로 전용 쓰레드 시작
    for (const auto& session_pair : sessions_) {
        int32_t session_id = session_pair.first;
        auto session = session_pair.second;
        
        // 이미 실행 중인 쓰레드가 있다면 종료 후 새로 생성
        auto thread_it = session_threads_.find(session_id);
        if (thread_it != session_threads_.end()) {
            if (thread_it->second.joinable()) {
                thread_it->second.join();
            }
            session_threads_.erase(thread_it);
        }
        
        // 세션별 워커 쓰레드 생성
        session_threads_[session_id] = std::thread(&SessionManager::sessionWorker, this, session);
        LOG_INFO("[SessionManager] Started worker thread for session ", session_id);
    }
    
    LOG_INFO("[SessionManager] Started session manager with ", available_sessions_.size(), " sessions and worker threads");
}

void SessionManager::stop() {
    // Signal all threads to terminate
    running_ = false;
    should_terminate_ = true;
    
    LOG_INFO("[SessionManager] Stopping all session threads...");
    
    // Wait for all session threads to terminate
    for (auto& thread_pair : session_threads_) {
        int32_t session_id = thread_pair.first;
        std::thread& thread = thread_pair.second;
        
        if (thread.joinable()) {
            LOG_INFO("[SessionManager] Waiting for session ", session_id, " thread to terminate...");
            thread.join();
            LOG_INFO("[SessionManager] Session ", session_id, " thread terminated");
        }
    }
    
    // Clear thread objects
    session_threads_.clear();
    
    LOG_INFO("[SessionManager] All session threads stopped");
}

void SessionManager::sessionWorker(std::shared_ptr<Session> session) {
    if (!session) {
        LOG_ERROR("[SessionManager] Null session passed to sessionWorker");
        return;
    }

    const int32_t session_id = session->getSessionId();
    LOG_INFO("[SessionManager] Session ", session_id, " worker thread started");
    
    try {
        // Use a reference to the shared_ptr to avoid copies in the loop
        while (running_ && !should_terminate_) {
            // Wait if session is empty
            if (session->getClientCount() == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            try {
                // Process session events
                session->processEvents();
            } catch (const std::exception& e) {
                LOG_ERROR("[SessionManager] Exception in session ", session_id, " processEvents: ", e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait briefly after errors
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[SessionManager] Fatal exception in session ", session_id, " worker: ", e.what());
    }
    
    LOG_INFO("[SessionManager] Session ", session_id, " worker thread terminated");
}

int32_t SessionManager::assignClientToSession(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[SessionManager] Invalid socket passed to assignClientToSession");
        return -1;
    }
    
    int32_t client_fd = client_socket->getSocketFd();
    if (client_fd < 0) {
        LOG_ERROR("[SessionManager] Socket has invalid file descriptor");
        return -1;
    }
    
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (available_sessions_.empty()) {
            LOG_ERROR("[SessionManager] No available sessions to assign client ", client_fd);
            return -1;
        }
        
        // 세션 목록이 비어있지 않다면 라운드 로빈 방식으로 순환
        size_t session_index = next_session_index_.fetch_add(1) % available_sessions_.size();
        int32_t session_id = available_sessions_[session_index];
        
        // 선택된 세션에 클라이언트 추가
        auto session_it = sessions_.find(session_id);
        if (session_it == sessions_.end()) {
            LOG_ERROR("[SessionManager] Session not found: ", session_id);
            return -1;
        }
        
        // 세션 매핑 정보 추가
        client_sessions_[client_fd] = session_id;
        
        // 세션에 클라이언트 추가
        session_it->second->addClient(client_socket);
        
        LOG_INFO("[SessionManager] Assigned client ", client_fd, " to session ", session_id);
        return session_id;
    } catch (const std::exception& e) {
        LOG_ERROR("[SessionManager] Exception in assignClientToSession for client ", client_fd, ": ", e.what());
        return -1;
    }
}

bool SessionManager::processEvents() {
    // 멀티쓰레드 모드에서는 이 메서드는 더 이상 사용되지 않음 (각 세션이 자체 쓰레드에서 처리)
    // 하지만 호환성을 위해 유지
    return running_;
}

void SessionManager::removeSession(int32_t client_fd) {
    if (client_fd < 0) {
        LOG_ERROR("[SessionManager] Attempted to remove invalid client_fd: ", client_fd);
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    try {
        auto it = client_sessions_.find(client_fd);
        if (it != client_sessions_.end()) {
            int32_t session_id = it->second;
            
            auto session_it = sessions_.find(session_id);
            if (session_it != sessions_.end()) {
                auto session = session_it->second;
                // 파일 디스크립터를 사용하는 대신 Socket 객체 생성
                auto socket_ptr = std::make_shared<Socket>(client_fd);
                session->removeClient(socket_ptr);
                LOG_INFO("[SessionManager] Removed client ", client_fd, " from session ", session_id);
                
                // 세션이 비어있으면 세션 정리 로직을 여기에 추가할 수 있음
            } else {
                LOG_ERROR("[SessionManager] Session ", session_id, " not found for client ", client_fd);
            }
            
            client_sessions_.erase(it);
            LOG_DEBUG("[SessionManager] Removed client-session mapping for client ", client_fd);
        } else {
            LOG_DEBUG("[SessionManager] Client ", client_fd, " not found in any session");
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("[SessionManager] Exception in removeSession for client ", client_fd, ": ", e.what());
    }
}

std::shared_ptr<Session> SessionManager::getSession(int32_t client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = client_sessions_.find(client_fd);
    if (it != client_sessions_.end()) {
        int32_t session_id = it->second;
        auto session_it = sessions_.find(session_id);
        
        if (session_it != sessions_.end()) {
            return session_it->second;
        }
    }
    
    return nullptr;
}

const std::set<int32_t>& SessionManager::getSessionClients(int32_t session_id) {
    static std::set<int32_t> empty_set;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto session_it = sessions_.find(session_id);
    if (session_it == sessions_.end()) {
        LOG_ERROR("[SessionManager] Session not found: ", session_id);
        return empty_set;
    }
    
    return session_it->second->getClientFds();
}

int32_t SessionManager::getNextAvailableSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (available_sessions_.empty()) {
        return -1;
    }
    
    // 라운드 로빈 방식으로 선택
    size_t index = next_session_index_.fetch_add(1) % available_sessions_.size();
    return available_sessions_[index];
}

std::shared_ptr<Session> SessionManager::getSessionByIndex(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < available_sessions_.size()) {
        int32_t session_id = available_sessions_[index];
        return sessions_[session_id];
    }
    
    return nullptr;
}

 