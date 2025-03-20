import os
import sys
import time
import psutil

# 패키지 경로 추가
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))
from bench_project.utils.command import start_server as cmd_start_server
from bench_project.config import CONFIG, SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL

def start_server(thread_count, server_type=None):
    """시스템 설정에 맞는 서버를 시작합니다."""
    if server_type is None:
        server_type = CONFIG.get('default_server_type', SERVER_TYPE_IOURING)
    
    host, port = CONFIG['default_server_address'].split(':')
    
    print(f"\n===== 서버 시작 (스레드: {thread_count}, 유형: {server_type}) =====")
    print(f"서버 주소: {host}:{port}")
    
    # 서버 시작
    server_process = cmd_start_server(thread_count, server_type=server_type)
    if not server_process:
        print("서버 시작 실패!")
        return None
    
    # 서버가 시작할 시간을 기다립니다
    print("서버가 준비될 때까지 대기 중...")
    time.sleep(1)
    
    # 서버 프로세스가 실행 중인지 확인
    if not psutil.pid_exists(server_process.pid):
        print("서버 프로세스가 예기치 않게 종료되었습니다!")
        return None
    
    print("서버가 성공적으로 시작되었습니다.")
    return server_process 