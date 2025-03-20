#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import time
import signal
import subprocess
import datetime

# 패키지 경로 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# 절대 경로로 임포트
from bench_project.utils.command import find_server_pid, run_cmd
from bench_project.utils.parsers import parse_bench_output, parse_strace_output, write_to_csv

def start_server_with_strace(thread_count, output_file, server_type=None):
    """strace를 사용하여 서버를 시작합니다."""
    from bench_project.config import CONFIG, SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL
    from bench_project.utils.command import find_server_binary
    
    if server_type is None:
        server_type = CONFIG.get('default_server_type', SERVER_TYPE_IOURING)
    
    server_path = find_server_binary(server_type)
    if not server_path:
        print(f"서버 바이너리를 찾을 수 없습니다 (유형: {server_type}).")
        return None
    
    host, port = CONFIG['default_server_address'].split(':')
    
    print(f"\n===== strace로 서버 시작 (스레드: {thread_count}, 유형: {server_type}) =====")
    print(f"서버 주소: {host}:{port}")
    print(f"strace 출력 파일: {output_file}")
    
    # strace 명령 구성
    cmd = [
        "sudo", "strace", "-ff", "-o", output_file, 
        server_path, "-t", str(thread_count), "-h", host, "-p", port
    ]
    
    try:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        print("서버가 strace로 시작 중입니다...")
        time.sleep(2)  # 서버 시작을 위한 시간 지연
        return process
    except Exception as e:
        print(f"strace로 서버를 시작하는 중 오류 발생: {e}")
        return None

def run_strace_benchmark(conn_count, thread_count, server_address, 
                       duration=30, csv_file=None, results_dir="benchmark_results",
                       timestamp=None, server_type=None):
    """Run a benchmark with strace instrumentation."""
    if timestamp is None:
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    
    print("\n===== Starting strace benchmark =====")
    print(f"Connections: {conn_count}, Threads: {thread_count}, Duration: {duration}s")
    
    # Create output directory if it doesn't exist
    os.makedirs(results_dir, exist_ok=True)
    
    # If no CSV file is specified, create one in the results directory
    if csv_file is None:
        csv_file = os.path.join(results_dir, f"strace_results_{timestamp}.csv")
    
    # Define the strace output file
    strace_output = os.path.join(results_dir, f"strace_output_conn_{conn_count}_threads_{thread_count}_{timestamp}.txt")
    
    # 메시지 길이 설정
    message_length = 1024
    
    # Start the server with strace
    server_process = start_server_with_strace(thread_count, strace_output, server_type=server_type)
    if not server_process:
        print("Failed to start server with strace. Aborting benchmark.")
        return None
    
    server_pid = find_server_pid(server_address)
    if not server_pid:
        print("Could not find server PID. Cannot run strace benchmark.")
        return None
    
    strace_log_file = os.path.join(results_dir, f"strace_server_pid_{server_pid}_conn_{conn_count}_{timestamp}.log")
    strace_cmd = [
        "sudo", "strace", "-c", "-p", str(server_pid), "-o", strace_log_file
    ]
    
    print(f"Starting system call tracing for server process (PID: {server_pid})...")
    print(f"Strace command: {' '.join(strace_cmd)}")
    print("Enter sudo password if prompted...")
    
    try:
        # Run strace in background
        strace_process = subprocess.Popen(strace_cmd)
        
        # Wait for strace to start
        print("Waiting for strace to start... (5 seconds)")
        time.sleep(5)
        
        # Check if strace is running
        if strace_process.poll() is not None:
            print(f"Strace failed to start (Exit code: {strace_process.returncode})")
            return None
        
        print(f"Strace started (PID: {strace_process.pid})")
        
        # Additional wait
        time.sleep(2)
        
        # Run benchmark client
        print(f"Running benchmark with {conn_count} connections...")
        
        # 연결 수가 리스트인 경우 각 항목마다 개별적으로 벤치마크 실행
        if isinstance(conn_count, list):
            combined_results = {}
            for single_conn in conn_count:
                print(f"\n----- Running individual benchmark for {single_conn} connections -----")
                bench_cmd = [
                    "cargo", "run", "--release", "--bin", "bench", "--",
                    "-a", server_address,
                    "-c", str(single_conn),
                    "-t", str(duration),
                    "-l", str(message_length)
                ]
                
                bench_result = run_cmd(bench_cmd)
                # 개별 결과 처리 로직 추가 가능
                time.sleep(1)  # 각 벤치마크 사이에 약간의 간격
            
            # 리스트인 경우 대표 값으로 첫 번째 연결 수 사용
            bench_cmd = [
                "cargo", "run", "--release", "--bin", "bench", "--",
                "-a", server_address,
                "-c", str(conn_count[0]),
                "-t", str(duration),
                "-l", str(message_length)
            ]
        else:
            # 단일 연결 수인 경우 기존 방식대로 실행
            bench_cmd = [
                "cargo", "run", "--release", "--bin", "bench", "--",
                "-a", server_address,
                "-c", str(conn_count),
                "-t", str(duration),
                "-l", str(message_length)
            ]
        
        bench_result = run_cmd(bench_cmd)
        bench_output = bench_result.stdout
        
        print("Benchmark completed. Stopping strace...")
        
        # Stop strace
        strace_process.send_signal(signal.SIGINT)
        time.sleep(1)
        
        if strace_process.poll() is None:
            print("Strace process not responding. Terminating...")
            strace_process.terminate()
            time.sleep(1)
            if strace_process.poll() is None:
                strace_process.kill()
        
        print(f"Strace stopped (Exit code: {strace_process.returncode})")
        
        # Read strace log
        with open(strace_log_file, 'r', encoding='utf-8') as f:
            strace_output = f.read()
        
        print(f"Strace log file read: {strace_log_file}")
        
        # Parse benchmark results
        tps_request, tps_response, success_rate = parse_bench_output(bench_output, duration)
        
        # Parse syscall statistics
        syscalls, total_syscalls = parse_strace_output(strace_output)
        
        # Create result dictionary
        result = {
            "connection_count": conn_count,
            "thread_count": thread_count,  # Include thread count in results
            "tps_request": tps_request,
            "tps_response": tps_response,
            "success_rate": success_rate,
            "total_syscalls": total_syscalls,
            "syscalls": syscalls,
            "raw_benchmark_output": bench_output,
            "raw_strace_output": strace_output,
            "server_pid": server_pid
        }
        
        # Save results to CSV
        write_to_csv(csv_file, result, include_syscalls=True, timestamp=timestamp)
        
        return result
        
    except Exception as e:
        print(f"Error during strace benchmark: {str(e)}")
        try:
            strace_process.terminate()
        except:
            pass
        return None 