#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
벤치마크 설정 파일
환경 변수와 경로 설정을 관리합니다.
"""

import os
import json
from pathlib import Path

# 서버 유형 정의
SERVER_TYPE_IOURING = "iouring"
SERVER_TYPE_EPOLL = "epoll"

# 기본 경로 설정 (서버 유형별로 구분)
DEFAULT_SERVER_PATHS = {
    SERVER_TYPE_IOURING: [     
        "./tcpchatserver/build/chat_server",       # 기본 경로 2
        "/usr/local/bin/tcpchatserver"             # 시스템 설치 경로
    ],
    SERVER_TYPE_EPOLL: [
        "./epollechoserver/build/epoll_server",    # epoll 서버 기본 경로 2
        "/usr/local/bin/epoll_server"              # epoll 서버 시스템 설치 경로
    ]
}

# 설정 파일 경로
CONFIG_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bench_config.json")

# 기본 설정
DEFAULT_CONFIG = {
    "server_paths": DEFAULT_SERVER_PATHS,
    "default_server_type": SERVER_TYPE_IOURING,     # 기본 서버 유형
    "benchmark_tool_path": "cargo run --release --bin bench --",
    "results_dir": "benchmark_results",
    "default_connections": [10, 50, 100, 500, 1000, 2000],
    "default_threads": [1, 2, 4, 8, 16],
    "default_duration": 30,
    "default_server_address": "127.0.0.1:8080"
}

# 설정 파일 로드 또는 생성
def load_config():
    """설정 파일을 로드하거나 기본 설정으로 새 파일을 생성합니다."""
    config = DEFAULT_CONFIG.copy()
    
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r', encoding='utf-8') as f:
                user_config = json.load(f)
                # 사용자 설정으로 기본 설정 업데이트
                config.update(user_config)
        except Exception as e:
            print(f"설정 파일 로드 중 오류 발생: {e}")
            print("기본 설정을 사용합니다.")
    else:
        # 설정 파일이 없으면 기본 설정으로 새 파일 생성
        save_config(config)
        print(f"기본 설정 파일이 생성되었습니다: {CONFIG_FILE}")
    
    # 환경 변수로 설정 오버라이드
    if 'SERVER_BINARY_PATH' in os.environ:
        env_path = os.environ['SERVER_BINARY_PATH']
        server_type = config['default_server_type']
        
        # 환경 변수 경로를 첫 번째 경로로 설정
        if env_path and server_type in config['server_paths']:
            paths = config['server_paths'][server_type]
            config['server_paths'][server_type] = [env_path] + [p for p in paths if p != env_path]
    
    # 환경 변수로 서버 유형 오버라이드
    if 'SERVER_TYPE' in os.environ:
        env_type = os.environ['SERVER_TYPE']
        if env_type in [SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL]:
            config['default_server_type'] = env_type
    
    return config

def save_config(config):
    """설정을 파일에 저장합니다."""
    try:
        with open(CONFIG_FILE, 'w', encoding='utf-8') as f:
            json.dump(config, f, indent=4)
        return True
    except Exception as e:
        print(f"설정 파일 저장 중 오류 발생: {e}")
        return False

def find_server_binary(server_type=None):
    """설정된 경로 목록에서 서버 바이너리를 찾습니다."""
    config = load_config()
    
    # 서버 유형이 지정되지 않으면 기본 유형 사용
    if server_type is None:
        server_type = config['default_server_type']
    
    # 서버 유형이 유효한지 확인
    if server_type not in config['server_paths']:
        print(f"오류: 유효하지 않은 서버 유형입니다: {server_type}")
        print(f"유효한 서버 유형: {', '.join(config['server_paths'].keys())}")
        return None
    
    # 지정된 유형의 서버 경로 검색
    for path in config['server_paths'][server_type]:
        if os.path.exists(path) and os.access(path, os.X_OK):
            return path
    
    return None

def add_server_path(path, server_type=None):
    """새 서버 경로를 설정에 추가합니다."""
    if not path:
        return False
    
    config = load_config()
    
    # 서버 유형이 지정되지 않으면 기본 유형 사용
    if server_type is None:
        server_type = config['default_server_type']
    
    # 서버 유형이 유효한지 확인
    if server_type not in config['server_paths']:
        print(f"오류: 유효하지 않은 서버 유형입니다: {server_type}")
        return False
    
    # 이미 있는 경로라면 제거 후 맨 앞에 추가
    if path in config['server_paths'][server_type]:
        config['server_paths'][server_type].remove(path)
    
    config['server_paths'][server_type].insert(0, path)
    return save_config(config)

def set_server_type(server_type):
    """기본 서버 유형을 설정합니다."""
    if server_type not in [SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL]:
        print(f"오류: 유효하지 않은 서버 유형입니다: {server_type}")
        print(f"유효한 서버 유형: {SERVER_TYPE_IOURING}, {SERVER_TYPE_EPOLL}")
        return False
    
    config = load_config()
    config['default_server_type'] = server_type
    return save_config(config)

# 기본 설정 로드
CONFIG = load_config() 