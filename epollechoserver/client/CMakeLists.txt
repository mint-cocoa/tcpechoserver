cmake_minimum_required(VERSION 3.10)
project(chat_client)

# C++17 표준 사용
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 소스 및 헤더 디렉토리 설정
set(CLIENT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(CLIENT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 헤더 파일 포함 경로 설정
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CLIENT_INCLUDE_DIR}
)

# 소스 파일 목록
set(CLIENT_SOURCES
    ${CLIENT_SOURCE_DIR}/ChatClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

# 헤더 파일 목록
set(CLIENT_HEADERS
    ${CLIENT_INCLUDE_DIR}/ChatClient.h
    ${CLIENT_INCLUDE_DIR}/Context.h
)

# 실행 파일 생성
add_executable(chat_client ${CLIENT_SOURCES} ${CLIENT_HEADERS})

# 필요한 라이브러리 링크
find_package(Threads REQUIRED)
target_link_libraries(chat_client PRIVATE Threads::Threads)

# 설치 규칙
install(TARGETS chat_client DESTINATION bin) 