#include "include/SessionManager.h"
#include "include/Logger.h"
#include <stdexcept>
#include <thread>

SessionManager::SessionManager() {
    LOG_INFO("[SessionManager] Initialized");
}

SessionManager::~SessionManager() {
    try {
        stop();
        std::lock_guard<std::mutex> lock(mutex_);
        sessions_.clear();
        client_sessions_.clear();
        available_sessions_.clear();
        LOG_INFO("[SessionManager] Destroyed");
    } catch (const std::exception& e) {
        LOG_ERROR("[SessionManager] Error during destruction: ", e.what());
    }
}

void SessionManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 설정된 쓰레드 수 또는 CPU 코어 수만큼 세션 생성 (최소 1개)
    unsigned int num_sessions = thread_count_;
    if (num_sessions == 0) {
        num_sessions = std::thread::hardware_concurrency();
        if (num_sessions == 0) num_sessions = 1;
    }
    
    LOG_INFO("[SessionManager] Initializing with ", num_sessions, " sessions");
    
    // 이미 생성된 세션이 있을 경우 모두 정리하고 새로 생성
    sessions_.clear();
    client_sessions_.clear();
    available_sessions_.clear();
    next_session_id_ = 0;
    
    for (unsigned int i = 0; i < num_sessions; ++i) {
        int32_t session_id = static_cast<int32_t>(next_session_id_++);
        auto session = std::make_shared<Session>(session_id);
        sessions_[session_id] = session;
        available_sessions_.push_back(session_id);
        LOG_DEBUG("[SessionManager] Created session ", session_id);
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
    if (!running_) {
        return;
    }
    
    // 종료 신호 설정
    running_ = false;
    
    // 모든 세션이 종료될 때까지 잠시 대기 (최대 2초)
    LOG_INFO("[SessionManager] 워커 쓰레드 종료 대기 중...");
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // 모든 세션 정리
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.clear();
    client_sessions_.clear();
    available_sessions_.clear();
    
    LOG_INFO("[SessionManager] 모든 리소스 정리 완료");
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
                session->processEvents(100);
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
        LOG_ERROR("[SessionManager] Attempted to assign invalid client socket");
        return -1;
    }

    int32_t client_fd = client_socket->getSocketFd();
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (available_sessions_.empty()) {
            LOG_ERROR("[SessionManager] No available sessions for client ", client_fd);
            return -1;
        }
        
        // 라운드 로빈 방식으로 세션 선택
        size_t index = next_session_index_.fetch_add(1) % available_sessions_.size();
        int32_t session_id = available_sessions_[index];
        
        // 세션 유효성 검사
        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            LOG_ERROR("[SessionManager] Invalid session_id: ", session_id);
            return -1;
        }
        
        client_sessions_[client_fd] = session_id;
        
        it->second->addClient(client_socket);
        
        LOG_INFO("[SessionManager] Assigned client ", client_fd, " to session ", session_id, " (round-robin)");
        
        return session_id;
    } catch (const std::exception& e) {
        LOG_ERROR("[SessionManager] Exception in assignClientToSession: ", e.what());
        return -1;
    }
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
    
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
         return empty_set;
    }
    
    return it->second->getClientFds();
}

std::shared_ptr<Session> SessionManager::getSessionByIndex(size_t index) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (index < available_sessions_.size()) {
        int32_t session_id = available_sessions_[index];
        return sessions_[session_id];
    }
    
    return nullptr;
}


void SessionManager::removeSessionByClient(SocketPtr client_socket) {
    if (!client_socket || !client_socket->isValid()) {
        LOG_ERROR("[SessionManager] Attempted to remove invalid client socket from session");
        return;
    }
    
    removeSession(client_socket->getSocketFd());
}