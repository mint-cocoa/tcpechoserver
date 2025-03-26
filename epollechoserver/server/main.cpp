#include "include/Listener.h"
#include "include/SessionManager.h"
#include "include/SocketManager.h"
#include "include/Logger.h"
#include "include/EPoll.h"
#include <signal.h>
#include <thread>
#include <atomic>
#include <csignal>
#include <cstdlib>  // getenv

std::atomic<bool> running(true);

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        LOG_INFO("Received termination signal. Shutting down...");
        running = false;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        LOG_ERROR("Usage: ", argv[0], " <host> <port> [num_threads]");
        return 1;
    }

    try {
        const char* host = argv[1];
        int port = std::stoi(argv[2]);
        
        // 시그널 핸들러 설정
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        // 현재 로그 레벨 출력
        auto& logger = Logger::getInstance();
        
        // 로그 레벨을 ERROR로 설정
        logger.setLogLevel(LogLevel::WARN);
        
        LogLevel current_level = logger.getLogLevel();
        const char* level_str = "";
        switch (current_level) {
            case LogLevel::TRACE: level_str = "TRACE"; break;
            case LogLevel::DEBUG: level_str = "DEBUG"; break;
            case LogLevel::INFO:  level_str = "INFO"; break;
            case LogLevel::WARN:  level_str = "WARN"; break;
            case LogLevel::ERROR: level_str = "ERROR"; break;
            case LogLevel::FATAL: level_str = "FATAL"; break;
            default:              level_str = "UNKNOWN"; break;
        }
        LOG_WARN("Logger initialized with level: ", level_str);

        LOG_INFO("Starting server on ", host, ":", port);
        LOG_INFO("Hardware concurrency: ", std::thread::hardware_concurrency(), " cores");

        // 쓰레드 수 인수 처리 (선택적)
        unsigned int thread_count = std::thread::hardware_concurrency(); // 기본값은 CPU 코어 수
        if (argc == 4) {
            thread_count = static_cast<unsigned int>(std::stoi(argv[3]));
            if (thread_count == 0) {
                LOG_ERROR("Number of threads must be greater than 0");
                return 1;
            }
            LOG_INFO("Using specified thread count: ", thread_count);
        } else {
            LOG_INFO("Using hardware concurrency: ", thread_count, " threads");
        }

        // 세션 매니저 초기화
        auto& session_manager = SessionManager::getInstance();
        session_manager.setThreadCount(thread_count);
        session_manager.initialize();
        session_manager.start();

        // epoll 기반 리스너 생성 및 시작
        auto& listener = Listener::getInstance(port);
        listener.start();

        LOG_INFO("Server started successfully with ", thread_count, " worker threads");

        // 메인 이벤트 루프
        while (running) {
            // 이벤트 처리
            listener.processEvents();
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        LOG_INFO("Shutting down server...");
        
        // 순서대로 정리 (리스너 먼저, 세션 관리자 나중에)
        try {
            LOG_INFO("Stopping listener...");
            listener.stop();
            
            // 잠시 대기하여 진행 중인 연결이 세션에 할당될 시간 제공
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            LOG_INFO("Stopping session manager...");
            session_manager.stop();
            
            // 모든 리소스가 정리될 시간을 제공
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        } catch (const std::exception& e) {
            LOG_ERROR("Error during shutdown: ", e.what());
        }
        
        LOG_INFO("Server shutdown complete");
        return 0;
    }
    catch (const std::exception& e) {
        LOG_FATAL("Fatal error: ", e.what());
        return 1;
    }
}