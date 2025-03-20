#include "SessionManager.h"
#include "SocketManager.h"
#include "Listener.h"
#include "Utils.h"
#include "Logger.h"
#include <csignal>
#include <thread>

std::atomic<bool> running(true);

int main(int argc, char* argv[]) {
    if (argc < 3 || argc > 4) {
        LOG_ERROR("Usage: ", argv[0], " <host> <port> [num_threads]");
        return 1;
    }

    try {
        const char* host = argv[1];
        int port = std::stoi(argv[2]);
        
        // 쓰레드 수 인수 처리 (선택적)
        unsigned int num_threads = 0;  // 기본값 0은 CPU 코어 수 사용
        if (argc == 4) {
            num_threads = static_cast<unsigned int>(std::stoi(argv[3]));
            if (num_threads == 0) {
                LOG_ERROR("Number of threads must be greater than 0");
                return 1;
            }
        }

        // 현재 로그 레벨 출력
        auto& logger = Logger::getInstance();
        
        // 로그 레벨을 설정
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
        if (num_threads > 0) {
            LOG_INFO("Using specified thread count: ", num_threads);
        } else {
            LOG_INFO("Using hardware concurrency: ", std::thread::hardware_concurrency(), " cores");
        }

        // 세션 매니저 초기화 및 시작
        auto& session_manager = SessionManager::getInstance();
        session_manager.initialize(num_threads);
        session_manager.start();

        // 리스너 생성 및 시작 (클라이언트 연결 수락 담당)
        auto& listener = Listener::getInstance(port);
        listener.start();

        LOG_INFO("Server started successfully");

        // 메인 루프
        while (running) {
            // 소켓 매니저가 새 연결을 수락하고 세션 매니저에 할당
            listener.processEvents();
            
            // 각 세션은 이제 자체 쓰레드에서 이벤트를 처리하므로 여기서 호출하지 않음
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        LOG_INFO("Shutting down server...");
        
        // 정리
        listener.stop();
        session_manager.stop();
        
        LOG_INFO("Server shutdown complete");
        return 0;
    }
    catch (const std::exception& e) {
        LOG_FATAL("Fatal error: ", e.what());
        return 1;
    }
}
