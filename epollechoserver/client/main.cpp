#include "include/ChatClient.h"
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

void printHelp() {
    std::cout << "\n사용 가능한 명령어:\n"
              << "/echo <메시지> - 에코 테스트\n"
              << "/quit - 프로그램 종료\n"
              << "/help - 도움말 보기\n" << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "사용법: " << argv[0] << " <서버IP> <포트>" << std::endl;
        return 1;
    }

    // 출력 버퍼링 비활성화
    std::cout.setf(std::ios::unitbuf);
    setvbuf(stdout, nullptr, _IONBF, 0);

    ChatClient client;
    std::atomic<bool> running(true);

    // 콜백 설정
    client.setMessageCallback([](const std::string& msg) {
        // 수신된 메시지를 즉시 출력 (버퍼링 없이)
        std::cout << msg << std::flush;
    });

    std::cout << "서버 연결 중... " << argv[1] << ":" << argv[2] << std::endl;

    // 서버 연결
    if (!client.connect(argv[1], std::stoi(argv[2]))) {
        return 1;
    }

    std::cout << "서버 테스트 클라이언트가 시작되었습니다.\n"
              << "명령어 목록을 보려면 /help를 입력하세요." << std::flush;

    // 자동으로 에코 테스트 메시지 전송 (최초 1회)
    std::thread([&client]() {
        // 서버 연결 및 세션 참가 시간을 위해 2초 대기
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // 자동 에코 테스트 메시지 전송
        std::string test_msg = "자동 에코 테스트 메시지";
        std::cout << "\n에코 테스트 전송: " << test_msg << std::endl;
        client.sendChat(test_msg);
    }).detach();

    std::string input;
    while (running && std::getline(std::cin, input)) {
        if (input.empty()) continue;

        if (input[0] == '/') {
            std::string cmd = input.substr(1);
            if (cmd.substr(0, 4) == "quit") {
                running = false;
                break;
            } else if (cmd.substr(0, 4) == "help") {
                printHelp();
            } else if (cmd.substr(0, 4) == "echo") {
                // /echo 명령어 처리
                if (cmd.length() > 5) {
                    std::string echo_msg = cmd.substr(5);
                    std::cout << "에코 테스트 전송: " << echo_msg << std::endl;
                    client.sendChat(echo_msg);
                } else {
                    std::cout << "사용법: /echo <메시지>" << std::endl;
                }
            } else {
                std::cout << "알 수 없는 명령어입니다. /help를 입력하여 도움말을 확인하세요." << std::flush;
            }
        } else {
            // 일반 채팅 메시지 전송
            std::cout << "메시지 전송: " << input << std::endl;
            client.sendChat(input);
        }
    }

    client.disconnect();
    return 0;
} 