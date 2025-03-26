cmake_minimum_required(VERSION 3.10)
project(epoll_server VERSION 1.0 LANGUAGES CXX)

# C++17 표준 사용
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 컴파일 옵션
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# 릴리즈/디버그 빌드 옵션
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -DDEBUG")

# 경고 설정
add_compile_options(-Wall -Wextra -Wpedantic)

# 매크로 정의
add_definitions(-DUSE_EPOLL)

# 소스 및 헤더 디렉토리 설정
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 헤더 파일 포함 경로
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 소스 파일 목록
set(SOURCES
    ${SOURCE_DIR}/EPoll.cpp
    ${SOURCE_DIR}/EPollBuffer.cpp
    ${SOURCE_DIR}/Session.cpp
    ${SOURCE_DIR}/SessionManager.cpp
    ${SOURCE_DIR}/Listener.cpp
    ${SOURCE_DIR}/SocketManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

# 헤더 파일 목록
set(HEADERS
    ${INCLUDE_DIR}/EPoll.h
    ${INCLUDE_DIR}/EPollBuffer.h
    ${INCLUDE_DIR}/Session.h
    ${INCLUDE_DIR}/SessionManager.h
    ${INCLUDE_DIR}/Socket.h
    ${INCLUDE_DIR}/Listener.h
    ${INCLUDE_DIR}/Context.h
    ${INCLUDE_DIR}/SocketManager.h
    ${INCLUDE_DIR}/Logger.h
)

# 실행 파일 생성
add_executable(epoll_server ${SOURCES} ${HEADERS})

# 필요한 라이브러리 링크
find_package(Threads REQUIRED)
target_link_libraries(epoll_server PRIVATE Threads::Threads)

