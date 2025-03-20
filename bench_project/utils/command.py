#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import subprocess
import time
import os
import signal
import psutil
import socket
import sys

# 패키지 경로 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# 설정 파일 임포트
from bench_project.config import find_server_binary, CONFIG, SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL

def run_cmd(cmd, shell=False, capture_output=True):
    """Execute a command and return its result."""
    print(f"Running: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    result = subprocess.run(cmd, shell=shell, capture_output=capture_output, text=True)
    if result.returncode != 0 and capture_output:
        print(f"Error: {result.stderr}")
    return result

def compile_bench():
    """Compile the benchmark tool."""
    print("Compiling benchmark tool...")
    result = run_cmd(["cargo", "build", "--release", "--bin", "bench"])
    if result.returncode == 0:
        print("Benchmark tool compiled successfully!")
        return True
    else:
        print("Benchmark tool compilation failed!")
        return False

def find_server_pid(server_address):
    """Find the process ID of the running server on the given address."""
    host, port = server_address.split(':')
    port = int(port)
    
    print(f"Looking for server PID (address: {server_address})...")
    
    # First try to find by process name
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            # Check for server process name (either main or specific binary name)
            if proc.info['name'] and ('server' in proc.info['name'] or 'main' in proc.info['name']):
                # Check connections of this process
                conns = proc.connections()
                for conn in conns:
                    if conn.status == 'LISTEN' and conn.laddr.port == port:
                        print(f"Found server process: PID={proc.pid}, name={proc.info['name']}")
                        return proc.pid
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    
    # If process name search fails, search by port only
    for proc in psutil.process_iter(['pid', 'name']):
        try:
            conns = proc.connections()
            for conn in conns:
                if conn.status == 'LISTEN' and conn.laddr.port == port:
                    print(f"Found process using port {port}: PID={proc.pid}, name={proc.info['name']}")
                    return proc.pid
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    
    print(f"Warning: Could not find server process using port {port}")
    return None

def start_server(thread_count=4, server_type=None):
    """Start the server with the specified number of threads and server type."""
    # 서버 유형이 지정되지 않으면 기본 유형 사용
    if server_type is None:
        server_type = CONFIG['default_server_type']
    
    print(f"Starting {server_type} server (thread count: {thread_count})...")
    
    # 설정 파일에서 서버 경로 가져오기
    server_path = find_server_binary(server_type)
    if not server_path:
        print(f"서버 실행 파일을 찾을 수 없습니다 (유형: {server_type}).")
        print(f"설정된 경로: {CONFIG['server_paths'][server_type]}")
        print("SERVER_BINARY_PATH 환경 변수를 설정하거나 bench_config.json 파일에서 경로를 수정하세요.")
        return None
    
    print(f"서버 실행 파일을 찾았습니다: {server_path}")
    
    # 서버 주소 설정
    server_address = CONFIG['default_server_address']
    host, port = server_address.split(':')
    
    # 쓰레드 수를 명령줄 인수로 전달
    server_cmd = [
        server_path,
        host,
        port
    ]
    
    
    server_process = subprocess.Popen(server_cmd)
    
    # Wait for server to start
    print("Waiting for server to start...")
    for i in range(10):
        time.sleep(1)
        print(f"Waiting for server start: {i+1} seconds...")
        
        # Check if server process is still running
        if server_process.poll() is not None:
            print(f"Server exited during startup. Exit code: {server_process.poll()}")
            return None
        
        # Check if port is open
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(1)
                if s.connect_ex((host, int(port))) == 0:
                    print("Server started successfully!")
                    return server_process
        except Exception as e:
            print(f"Error checking port: {e}")
    
    print("Server startup timed out")
    if server_process.poll() is None:
        server_process.terminate()
    return None

def stop_server(server_process):
    """Stop the server."""
    if server_process:
        print("Stopping server...")
        server_process.terminate()
        server_process.wait()
        print("Server stopped.") 