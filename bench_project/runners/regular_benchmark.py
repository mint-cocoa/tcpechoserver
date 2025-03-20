#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import subprocess
import datetime
import time

# 패키지 경로 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# 절대 경로로 임포트
from bench_project.utils.command import run_cmd
from bench_project.utils.parsers import parse_bench_output, write_to_csv
from bench_project.runners.start_server import start_server

def run_regular_benchmark(conn_count, thread_count, server_address, 
                         duration=30, csv_file=None, results_dir="benchmark_results",
                         timestamp=None, server_type=None):
    """Run a benchmark without strace instrumentation."""
    if timestamp is None:
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    
    print("\n===== Starting regular benchmark =====")
    print(f"Connections: {conn_count}, Threads: {thread_count}, Duration: {duration}s")
    
    # Create output directory if it doesn't exist
    os.makedirs(results_dir, exist_ok=True)
    
    # If no CSV file is specified, create one in the results directory
    if csv_file is None:
        csv_file = os.path.join(results_dir, f"regular_results_{timestamp}.csv")
    
    # Start the server
    server_process = start_server(thread_count, server_type=server_type)
    if not server_process:
        print("Failed to start server. Aborting benchmark.")
        return None
    
    # 연결 수가 리스트인 경우 각 항목마다 개별적으로 벤치마크 실행
    if isinstance(conn_count, list):
        combined_results = {}
        for single_conn in conn_count:
            print(f"\n----- Running individual benchmark for {single_conn} connections -----")
            single_cmd = [
                "cargo", "run", "--release", "--bin", "bench", "--",
                "-a", server_address,
                "-c", str(single_conn),
                "-t", str(duration),
                "-l", str(1024)
            ]
            
            print(f"Running: {' '.join(single_cmd)}")
            single_result = subprocess.run(single_cmd, capture_output=True, text=True)
            
            if single_result.returncode == 0:
                print(single_result.stdout)
            else:
                print(f"Benchmark for {single_conn} connections failed with exit code {single_result.returncode}")
            
            time.sleep(1)  # 각 벤치마크 사이에 약간의 간격
        
        # 리스트인 경우 대표 값으로 첫 번째 연결 수 사용
        cmd = [
            "cargo", "run", "--release", "--bin", "bench", "--",
            "-a", server_address,
            "-c", str(conn_count[0]),
            "-t", str(duration),
            "-l", str(1024)
        ]
    else:
        # 단일 연결 수인 경우 기존 방식대로 실행
        cmd = [
            "cargo", "run", "--release", "--bin", "bench", "--",
            "-a", server_address,
            "-c", str(conn_count),
            "-t", str(duration),
            "-l", str(1024)
        ]
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        error_msg = f"Exit code: {result.returncode}\n"
        if result.stderr:
            error_msg += f"Error message: {result.stderr}\n"
        print(f"Benchmark failed: {error_msg}")
        
        # Save error info
        error_file = os.path.join(results_dir, f"error_conn_{conn_count}_{timestamp}.txt")
        with open(error_file, 'w', encoding='utf-8') as f:
            f.write(f"Benchmark failed (connections: {conn_count})\n")
            f.write(f"Command: {' '.join(cmd)}\n")
            f.write(error_msg)
            if result.stdout:
                f.write(f"\nStandard output:\n{result.stdout}\n")
        print(f"Error info saved to {error_file}")
        
        return None
    
    output = result.stdout
    print(output)
    
    # Parse benchmark results
    tps_request, tps_response, success_rate = parse_bench_output(output, duration)
    
    # Save raw output
    result_file = os.path.join(results_dir, f"regular_benchmark_conn_{conn_count}_{timestamp}.txt")
    with open(result_file, 'w', encoding='utf-8') as f:
        f.write(output)
    print(f"Benchmark results saved to {result_file}")
    
    # Create result dictionary
    result_dict = {
        "connection_count": conn_count,
        "thread_count": thread_count,  # Include thread count in results
        "tps_request": tps_request,
        "tps_response": tps_response,
        "success_rate": success_rate,
        "raw_output": output
    }
    
    # Save results to CSV
    write_to_csv(csv_file, result_dict, timestamp=timestamp)
    
    return result_dict 