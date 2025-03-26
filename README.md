# TCP 에코 서버 벤치마크

이 프로젝트는 다양한 TCP 에코 서버 구현의 성능을 벤치마크하기 위한 도구입니다.

## 구성 요소

- `tcpchatserver/`: 멀티스레드 TCP 채팅 서버 구현
- `epollechoserver/`: epoll 기반 TCP 에코 서버 구현
- `run_benchmark.py`: 벤치마크 실행 스크립트

## 서버 빌드 방법

### TCP 채팅 서버 빌드

```bash
cd tcpchatserver
mkdir -p build && cd build
cmake ..
make -j
```

### epoll 에코 서버 빌드

```bash
cd epollechoserver
mkdir -p build && cd build
cmake ..
make -j
```

## 벤치마크 실행 방법

벤치마크는 다음과 같이 실행할 수 있습니다:

```bash
python run_benchmark.py -s /경로/서버바이너리 -H 127.0.0.1 -p 8080 -c 10,50,100 -t 1,2,4
```

### 명령줄 인수

- `-s`, `--server-path`: 서버 바이너리 경로 (필수)
- `-H`, `--server-host`: 서버 호스트 (기본값: 127.0.0.1)
- `-p`, `--server-port`: 서버 포트 (기본값: 8080)
- `-c`, `--connections`: 테스트할 연결 수 (쉼표로 구분, 기본값: 10,50,100,500,1000,2000)
- `-t`, `--threads`: 테스트할 스레드 수 (쉼표로 구분, 기본값: 1,2,4,8,16)
- `-d`, `--duration`: 각 벤치마크 지속 시간(초) (기본값: 30)

## 서버 실행 방법

### TCP 채팅 서버

```bash
./tcpchatserver/build/chat_server <host> <port> [num_threads]
```

### epoll 에코 서버

```bash
./epollechoserver/build/echo_server <host> <port> [num_threads]
```

## 벤치마크 결과

벤치마크 결과는 `results/` 디렉토리에 저장됩니다. 각 벤치마크 실행은 타임스탬프가 있는 별도의 폴더를 생성합니다.

### 결과 파일 형식

- `regular_results_[timestamp].csv`: 일반 벤치마크 결과
- `strace_results_[timestamp].csv`: strace 벤치마크 결과
- `combined_benchmark_[timestamp]/`: 각 벤치마크 실행에 대한 자세한 정보 