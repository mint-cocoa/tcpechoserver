cmake_minimum_required(VERSION 3.10)
project(chat_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# AddressSanitizer 설정
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g -O1")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
# 컴파일 옵션 설정
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

# 로그 레벨 설정 (TRACE=0, DEBUG=1, INFO=2, WARN=3, ERROR=4, FATAL=5)
add_definitions(-DLOG_LEVEL=3)  # WARN 레벨로 설정

# 서버 소스 파일
set(SERVER_SOURCES
    server/main.cpp
    server/src/Listener.cpp
    server/src/Session.cpp
    server/src/IOUring.cpp
    server/src/UringBuffer.cpp
    server/src/SocketManager.cpp
    server/src/SessionManager.cpp
)

# 클라이언트 소스 파일
set(CLIENT_SOURCES
    client/main.cpp
    client/src/ChatClient.cpp
)



# 서버 헤더 파일 디렉토리
include_directories(
    server/include
    client/include
)

# 실행 파일 출력 디렉토리 설정
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# 서버 실행 파일
add_executable(chat_server ${SERVER_SOURCES})

# 클라이언트 실행 파일
add_executable(chat_client ${CLIENT_SOURCES})


# 멀티 클라이언트 테스트 실행 파일

# 서버 라이브러리 링크
target_link_libraries(chat_server
    uring
    pthread
)

# 클라이언트 라이브러리 링크
target_link_libraries(chat_client
    pthread
)



# 심플 클라이언트 라이브러리 링크


# 디버그/릴리즈 설정에 따른 로그 레벨 조정
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DLOG_LEVEL=3)  # WARN 레벨
else()
    add_definitions(-DLOG_LEVEL=3)  # WARN 레벨
endif()

