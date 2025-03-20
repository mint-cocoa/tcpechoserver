# Locust를 이용한 TCP 채팅 서버 부하 테스트 가이드

Locust는 파이썬으로 작성된 오픈 소스 부하 테스트 도구로, 일반적으로 웹 애플리케이션의 성능 테스트에 사용됩니다. 이 가이드에서는 Locust를 사용하여 TCP 채팅 서버의 부하 테스트를 수행하는 방법을 설명합니다.

## 1. Locust 설치

먼저 Locust를 설치합니다:

```bash
pip install locust
```

## 2. TCP 채팅 서버 테스트를 위한 Locust 스크립트

이 저장소에 포함된 `locustfile.py`는 TCP 채팅 서버를 테스트하기 위한 Locust 스크립트입니다. 이 스크립트는 일반적인 HTTP 테스트 대신 TCP 소켓 통신을 사용하여 채팅 서버에 연결하고 메시지를 전송합니다.

### 주요 구성요소

1. **TCPSocketClient**: 채팅 서버와의 TCP 소켓 통신을 담당하는 클라이언트
2. **TCPChatUser**: Locust의 User 클래스를 상속하는 추상 클래스
3. **ChatClient**: 실제 테스트에서 채팅 메시지를 보내는 사용자 클래스

## 3. 테스트 실행 방법

### 기본 실행

Locust를 실행하려면 다음 명령을 사용합니다:

```bash
# 포트를 호스트 주소에 포함하여 전달
locust -f locustfile.py --host=localhost:12345
```

또는 환경 변수를 사용해 포트를 지정할 수 있습니다:

```bash
TCP_PORT=12345 locust -f locustfile.py --host=localhost
```

이 명령은 Locust 웹 인터페이스를 시작합니다. 기본적으로 `http://localhost:8089`에서 접근 가능합니다.

### 명령줄 옵션

웹 인터페이스를 사용하지 않고 명령줄에서 직접 테스트를 실행할 수도 있습니다:

```bash
locust -f locustfile.py --host=localhost:12345 --users=100 --spawn-rate=10 --run-time=1m --headless
```

주요 옵션:
- `--host`: 서버 호스트 주소 (포트는 host:port 형식으로 입력)
- `--users`: 동시 사용자 수
- `--spawn-rate`: 초당 생성할 사용자 수
- `--run-time`: 테스트 실행 시간 (예: 1m, 30s, 5h)
- `--headless`: 웹 인터페이스 없이 실행

### 분산 실행

대규모 테스트의 경우 여러 머신에서 Locust를 분산 실행할 수 있습니다:

**마스터**:
```bash
locust -f locustfile.py --master --host=localhost:12345
```

**워커**:
```bash
locust -f locustfile.py --worker --master-host=192.168.1.10 --host=localhost:12345
```

## 4. 테스트 시나리오

`locustfile.py`에 정의된 세 가지 테스트 작업과 가중치:

1. **일반 채팅 메시지 전송** (가중치: 5): 짧은 채팅 메시지 전송
2. **긴 채팅 메시지 전송** (가중치: 2): 중간 길이의 메시지 전송
3. **매우 긴 채팅 메시지 전송** (가중치: 1): 512바이트를 초과하는 메시지 전송

가중치는 각 작업이 실행될 상대적 빈도를 의미합니다. 위 설정에서는 일반 메시지가 긴 메시지보다 2.5배, 매우 긴 메시지보다 5배 자주 전송됩니다.

## 5. 결과 해석

Locust는 테스트 결과를 웹 인터페이스에 표시합니다:

- **Total Requests**: 총 요청 수
- **Fails**: 실패한 요청 수
- **RPS (Requests Per Second)**: 초당 요청 수
- **Response Time**: 응답 시간 통계 (최소, 최대, 평균, 중앙값)

TCP 소켓 통신의 경우 다음과 같은 요청 유형이 기록됩니다:

- **connect**: 서버 연결
- **send_chat**: 채팅 메시지 전송
- **echo_response**: 에코 응답 수신

## 6. 주의사항

1. 테스트 시작 전 서버가 실행 중인지 확인하세요.
2. 실제 네트워크 환경에서는 방화벽 설정을 확인하세요.
3. 대규모 테스트의 경우 서버와 테스트 클라이언트의 시스템 리소스 제한을 확인하세요.
4. 테스트 결과는 네트워크 환경, 서버 사양 등 여러 요소에 영향을 받을 수 있습니다.
5. **중요**: Locust는 `--port` 옵션을 TCP 포트 지정에 사용하지 않습니다. 대신 `--host=호스트:포트` 형식으로 사용하거나 환경 변수 `TCP_PORT`를 설정하세요.

## 7. 단독 테스트 모드

`locustfile.py`는 단독으로 실행해 연결 테스트를 할 수 있습니다:

```bash
python locustfile.py [호스트 IP] [포트]
```

예:
```bash
python locustfile.py 127.0.0.1 12345
```

이 모드에서는 Locust 엔진 없이 5개의 테스트 메시지를 보내고 서버 응답을 확인합니다.

## 8. 부하 테스트 적용 사례

- **동시 사용자 수 증가 테스트**: 서버가 처리할 수 있는 최대 동시 연결 수 확인
- **높은 메시지 처리량 테스트**: 서버가 초당 처리할 수 있는 메시지 수 확인
- **장시간 안정성 테스트**: 서버가 장시간 안정적으로 동작하는지 확인
- **에러 처리 테스트**: 비정상적인 메시지나 연결 해제에 대한 서버 동작 확인

## 9. 커스텀 테스트 시나리오 추가

자신만의 테스트 시나리오를 추가하려면:

1. `ChatClient` 클래스에 새로운 메서드를 추가합니다.
2. 메서드에 `@task` 데코레이터를 적용하고 가중치를 설정합니다.
3. 메서드 내에서 `self.client`를 사용하여 메시지를 전송합니다.

예:
```python
@task(3)
def send_special_command(self):
    """특수 명령어 전송 (가중치: 3)"""
    command = f"/command special_{random.randint(1, 100)}"
    self.client.send_chat(command)
```

## 10. 문제 해결

### "unrecognized arguments: --port=xxxxx" 오류

Locust는 TCP 포트를 별도의 `--port` 옵션으로 지정하는 것을 지원하지 않습니다. 대신 다음 방법 중 하나를 사용하세요:

1. 호스트 주소에 포트 포함: `--host=localhost:12345`
2. 환경 변수 사용: `TCP_PORT=12345 locust -f locustfile.py --host=localhost`

### 연결 실패 문제

서버에 연결하지 못하는 경우 다음을 확인하세요:
- 서버가 실행 중인지 확인
- 방화벽이 해당 포트를 차단하고 있지 않은지 확인
- 호스트 주소가 올바른지 확인 