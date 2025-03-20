import time
import socket
import struct
from locust import User, task, between, events
import random

# 메시지 타입 정의
class MessageType:
    SERVER_ACK = 0x01
    SERVER_ERROR = 0x02
    SERVER_CHAT = 0x03
    SERVER_NOTIFICATION = 0x04
    SERVER_ECHO = 0x05
    
    CLIENT_JOIN = 0x11
    CLIENT_LEAVE = 0x12
    CLIENT_CHAT = 0x13
    CLIENT_COMMAND = 0x14

class ChatMessage:
    def __init__(self, msg_type, data=""):
        self.type = msg_type
        self.data = data.encode('utf-8') if isinstance(data, str) else data
        self.length = len(self.data)

    def pack(self):
        # 메시지 구조: type(1) + length(2) + data(1021)
        header = struct.pack('=BH', self.type, self.length)
        # 최대 1021바이트로 제한하고 나머지는 NULL로 채움
        data = self.data.ljust(1021, b'\0')
        return header + data[:1021]

    @staticmethod
    def unpack(data):
        msg_type, length = struct.unpack('=BH', data[:3])
        message_data = data[3:3+length].decode('utf-8').rstrip('\0')
        return ChatMessage(msg_type, message_data)

class ChatClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.connected = False

    def connect(self):
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((self.host, self.port))
            self.connected = True
            return True
        except Exception as e:
            print(f"연결 실패: {e}")
            return False

    def disconnect(self):
        if self.sock:
            self.sock.close()
            self.connected = False

    def send_message(self, message):
        if not self.connected:
            return False
        try:
            self.sock.send(message.pack())
            return True
        except Exception as e:
            print(f"메시지 전송 실패: {e}")
            return False

    def receive_message(self):
        if not self.connected:
            return None
        try:
            data = self.sock.recv(1024)  # 메시지 전체 크기: 1(type) + 2(length) + 1021(data) = 1024
            if not data:
                return None
            return ChatMessage.unpack(data)
        except Exception as e:
            print(f"메시지 수신 실패: {e}")
            return None

class ChatUser(User):
    wait_time = between(0.01, 0.05)  # 메시지 전송 간격을 0.01~0.05초로 줄임
    
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.client = None
        self.messages_sent = 0
        self.messages_received = 0

    def on_start(self):
        # 웹 인터페이스에서 입력한 호스트/포트를 사용하기 위해 수정
        host = self.host or "localhost"
        port = 8080  # 기본 포트
        
        self.client = ChatClient(host, port)
        if not self.client.connect():
            print("서버 연결 실패")
            return

    def on_stop(self):
        if self.client and self.client.connected:
            self.client.disconnect()
            print(f"총 전송 메시지: {self.messages_sent}, 수신 메시지: {self.messages_received}")

    @task
    def send_chat(self):
        if not self.client or not self.client.connected:
            return

        # 랜덤 채팅 메시지 생성
        messages = [
            "안녕하세요!",
            "테스트 메시지입니다.",
            "채팅 서버 부하 테스트 중...",
            "성능 테스트 진행 중",
            "메시지 전송 테스트",
            "에코 테스트",
            "짧은메시지",
            "긴 메시지를 전송해보겠습니다. 이 메시지는 조금 더 긴 내용을 담고 있어서 서버의 처리 성능을 테스트하는데 도움이 될 것입니다.",
            "1234567890",
            "!@#$%^&*()"
        ]
        
        start_time = time.time()
        message = ChatMessage(MessageType.CLIENT_CHAT, random.choice(messages))
        
        try:
            if self.client.send_message(message):
                self.messages_sent += 1
                response = self.client.receive_message()
                if response and response.type == MessageType.SERVER_ECHO:
                    self.messages_received += 1
                    total_time = int((time.time() - start_time) * 1000)
                    events.request.fire(
                        request_type="CHAT",
                        name="echo_test",
                        response_time=total_time,
                        response_length=len(message.data),
                        exception=None,
                    )
                else:
                    events.request.fire(
                        request_type="CHAT",
                        name="echo_test",
                        response_time=0,
                        response_length=0,
                        exception=Exception("에코 응답 실패"),
                    )
        except Exception as e:
            events.request.fire(
                request_type="CHAT",
                name="echo_test",
                response_time=0,
                response_length=0,
                exception=e,
            ) 