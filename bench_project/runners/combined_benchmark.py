#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import time
import datetime

# 패키지 경로 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

# 절대 경로로 임포트
from bench_project.utils.command import start_server
from bench_project.runners.regular_benchmark import run_regular_benchmark
from bench_project.runners.strace_benchmark import run_strace_benchmark

def run_combined_benchmark(connections, thread_counts, server_address, duration=30, 
                           run_regular=True, run_strace=True, results_dir="benchmark_results",
                           timestamp=None, server_type=None):
    """Run comprehensive benchmarks for all combinations of connections and thread counts."""
    print("\n===== Starting comprehensive benchmark =====")
    
    if timestamp is None:
        timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    
    # Create CSV files for each type of benchmark
    regular_csv = os.path.join(results_dir, f"regular_results_{timestamp}.csv")
    strace_csv = os.path.join(results_dir, f"strace_results_{timestamp}.csv")
    
    # Initialize empty lists for results
    regular_results = []
    strace_results = []
    
    # Create combined results directory
    combined_dir = os.path.join(results_dir, f"combined_benchmark_{timestamp}")
    os.makedirs(combined_dir, exist_ok=True)
    
    # Log file for this run
    log_file = os.path.join(combined_dir, "benchmark_log.txt")
    with open(log_file, 'w', encoding='utf-8') as f:
        f.write(f"===== Comprehensive Benchmark Log =====\n")
        f.write(f"Started: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Server address: {server_address}\n")
        if server_type:
            f.write(f"Server type: {server_type}\n")
        f.write(f"Duration per test: {duration} seconds\n")
        f.write(f"Connection counts: {connections}\n")
        f.write(f"Thread counts: {thread_counts}\n")
        f.write(f"Benchmark types: " + 
                f"{'Regular ' if run_regular else ''}" +
                f"{'Strace' if run_strace else ''}\n\n")
    
    # 재귀적으로 벤치마크 실행
    def run_benchmark_recursive(thread_idx=0):
        if thread_idx >= len(thread_counts):
            return True
        
        thread_count = thread_counts[thread_idx]
        print(f"\n{'='*50}")
        print(f"===== Testing with {thread_count} threads =====")
        print(f"{'='*50}")
        
        # Log thread count
        with open(log_file, 'a', encoding='utf-8') as f:
            f.write(f"\n{'='*50}\n")
            f.write(f"Starting tests with {thread_count} threads at {datetime.datetime.now().strftime('%H:%M:%S')}\n")
            f.write(f"{'='*50}\n\n")
        
        # Start server with this thread count
        server_process = start_server(thread_count, server_type=server_type)
        if not server_process:
            print(f"Failed to start server with {thread_count} threads. Skipping.")
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"Failed to start server with {thread_count} threads. Skipping.\n\n")
            # 다음 스레드 카운트로 재귀 호출
            return run_benchmark_recursive(thread_idx + 1)
        
        try:
            # 연결 수 테스트 시작
            run_connection_tests(thread_count, server_process)
            
            # 다음 스레드 카운트로 재귀 호출
            return run_benchmark_recursive(thread_idx + 1)
        finally:
            # Ensure server process is terminated
            if server_process.poll() is None:
                try:
                    print(f"Terminating server process (PID: {server_process.pid})...")
                    server_process.terminate()
                    server_process.wait(5)
                except:
                    print(f"Killing server process (PID: {server_process.pid})...")
                    server_process.kill()
    
    # 각 연결 수에 대한 테스트 실행
    def run_connection_tests(thread_count, server_process):
        for conn_count in connections:
            print(f"\n----- Testing with {conn_count} connections -----")
            
            # Log connection count
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"Testing with {conn_count} connections at {datetime.datetime.now().strftime('%H:%M:%S')}\n")
            
            # Run regular benchmark if requested
            if run_regular:
                print(f"\n----- Running regular benchmark (conn: {conn_count}, threads: {thread_count}) -----")
                
                result = run_regular_benchmark(
                    conn_count=conn_count,
                    thread_count=thread_count,
                    server_address=server_address,
                    duration=duration,
                    csv_file=regular_csv,
                    results_dir=combined_dir,
                    timestamp=timestamp,
                    server_type=server_type
                )
                
                if result:
                    regular_results.append(result)
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"Regular benchmark completed - TPS: {result.get('tps_request', 'N/A')}\n")
                else:
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"Regular benchmark failed\n")
            
            # Run strace benchmark if requested
            if run_strace:
                print(f"\n----- Running strace benchmark (conn: {conn_count}, threads: {thread_count}) -----")
                
                # Kill previous server process first
                if server_process.poll() is None:
                    try:
                        server_process.terminate()
                        server_process.wait(5)
                    except:
                        server_process.kill()
                
                result = run_strace_benchmark(
                    conn_count=conn_count,
                    thread_count=thread_count,
                    server_address=server_address,
                    duration=duration,
                    csv_file=strace_csv,
                    results_dir=combined_dir,
                    timestamp=timestamp,
                    server_type=server_type
                )
                
                if result:
                    strace_results.append(result)
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"Strace benchmark completed - TPS: {result.get('tps_request', 'N/A')}\n")
                else:
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"Strace benchmark failed\n")
            
            # Log completion for this connection count
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"Completed tests with {conn_count} connections at {datetime.datetime.now().strftime('%H:%M:%S')}\n\n")
    
    # 재귀적 벤치마크 실행 시작
    run_benchmark_recursive()
    
    # Log completion of all tests
    with open(log_file, 'a', encoding='utf-8') as f:
        f.write(f"\n{'='*50}\n")
        f.write(f"All benchmarks completed at {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"{'='*50}\n")
    
    print("\n===== Comprehensive benchmark completed =====")
    print(f"Results saved to {combined_dir}")
    
    return True

if __name__ == "__main__":
    # Example usage
    connections = [10, 20, 30]
    thread_counts = [1, 2, 4]
    server_address = "localhost:8080"
    run_combined_benchmark(connections, thread_counts, server_address) 