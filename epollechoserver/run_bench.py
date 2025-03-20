#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import subprocess
import time
import os
import sys
import argparse
import csv
import re
import matplotlib.pyplot as plt
import pandas as pd
import glob
import signal
import psutil
from matplotlib.backends.backend_pdf import PdfPages
import numpy as np
import seaborn as sns
import socket
import json
import datetime

# Create directory to store benchmark results
RESULTS_DIR = "benchmark_results"
os.makedirs(RESULTS_DIR, exist_ok=True)

# Create timestamp for current run that will be used across all files
TIMESTAMP = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

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
            # Check if process name is epoll_server
            if proc.info['name'] and 'epoll_server' in proc.info['name']:
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

def start_server(thread_count=4):
    """Start the server with the specified number of threads."""
    print(f"Starting server (thread count: {thread_count})...")
    
    # Set thread count as an environment variable
    env = os.environ.copy()
    env["SERVER_THREAD_COUNT"] = str(thread_count)
    
    # Run server (from build directory)
    server_cmd = [
        "./build/epoll_server",
        "127.0.0.1",
        "8080"
    ]
    
    server_process = subprocess.Popen(server_cmd, env=env)
    
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
                if s.connect_ex(('127.0.0.1', 8080)) == 0:
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

# Perf benchmark function removed as requested

def run_strace_benchmark(connection_count, duration=10, server_address="127.0.0.1:8080", message_length=1024, thread_count=None):
    """Run a benchmark with strace to collect system call statistics."""
    print(f"\n===== Running strace benchmark with {connection_count} connections =====")
    
    server_pid = find_server_pid(server_address)
    if not server_pid:
        print("Could not find server PID. Cannot run strace benchmark.")
        return None
    
    strace_log_file = os.path.join(RESULTS_DIR, f"strace_server_pid_{server_pid}_conn_{connection_count}_{TIMESTAMP}.log")
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
        print(f"Running benchmark with {connection_count} connections...")
        bench_cmd = [
            "cargo", "run", "--release", "--bin", "bench", "--",
            "-a", server_address,
            "-c", str(connection_count),
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
            "connection_count": connection_count,
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
        strace_csv_file = os.path.join(RESULTS_DIR, f"strace_results_{TIMESTAMP}.csv")
        write_to_csv(strace_csv_file, result, include_syscalls=True)
        
        return result
        
    except Exception as e:
        print(f"Error during strace benchmark: {str(e)}")
        try:
            strace_process.terminate()
        except:
            pass
        return None

def run_benchmark(connection_count, duration=10, server_address="127.0.0.1:8080", message_length=1024, thread_count=None):
    """Run a regular benchmark without system call tracing."""
    print(f"\n===== Running benchmark with {connection_count} connections =====")
    
    cmd = [
        "cargo", "run", "--release", "--bin", "bench", "--",
        "-a", server_address,
        "-c", str(connection_count),
        "-t", str(duration),
        "-l", str(message_length)
    ]
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        error_msg = f"Exit code: {result.returncode}\n"
        if result.stderr:
            error_msg += f"Error message: {result.stderr}\n"
        print(f"Benchmark failed: {error_msg}")
        
        # Save error info
        error_file = os.path.join(RESULTS_DIR, f"error_conn_{connection_count}_{TIMESTAMP}.txt")
        with open(error_file, 'w', encoding='utf-8') as f:
            f.write(f"Benchmark failed (connections: {connection_count})\n")
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
    result_file = os.path.join(RESULTS_DIR, f"regular_benchmark_conn_{connection_count}_{TIMESTAMP}.txt")
    with open(result_file, 'w', encoding='utf-8') as f:
        f.write(output)
    print(f"Benchmark results saved to {result_file}")
    
    # Create result dictionary
    result_dict = {
        "connection_count": connection_count,
        "thread_count": thread_count,  # Include thread count in results
        "tps_request": tps_request,
        "tps_response": tps_response,
        "success_rate": success_rate,
        "raw_output": output
    }
    
    # Save results to CSV
    regular_csv_file = os.path.join(RESULTS_DIR, f"regular_results_{TIMESTAMP}.csv")
    write_to_csv(regular_csv_file, result_dict)
    
    return result_dict

def parse_bench_output(output, duration):
    """Parse benchmark output to extract TPS and success rate."""
    tps_request = 0
    tps_response = 0
    success_rate = 0
    
    # Parse "Speed: {} request/sec, {} response/sec"
    speed_match = re.search(r'Speed: (\d+) request/sec, (\d+) response/sec', output)
    if speed_match:
        tps_request = int(speed_match.group(1))
        tps_response = int(speed_match.group(2))
        print(f"TPS: Requests={tps_request}/sec, Responses={tps_response}/sec")
    
    # Parse "Requests: {}"
    req_match = re.search(r'Requests: (\d+)', output)
    if req_match and tps_request == 0:  # Only use if not already parsed from Speed
        tps_request = int(int(req_match.group(1)) / duration)
        print(f"Request TPS (calculated): {tps_request}/sec")
    
    # Parse "Responses: {}"
    resp_match = re.search(r'Responses: (\d+)', output)
    if resp_match and tps_response == 0:  # Only use if not already parsed from Speed
        tps_response = int(int(resp_match.group(1)) / duration)
        print(f"Response TPS (calculated): {tps_response}/sec")
    
    # Parse "Success rate: {:.2}%"
    success_match = re.search(r'Success rate: ([\d.]+)%', output)
    if success_match:
        success_rate = float(success_match.group(1))
        print(f"Success rate: {success_rate}%")
    
    return tps_request, tps_response, success_rate

def parse_strace_output(output):
    """Parse strace output to extract system call statistics."""
    syscalls = {}
    total_syscalls = 0
    
    for line in output.split('\n'):
        if '%' in line and 'total' not in line:
            parts = line.strip().split()
            if len(parts) >= 5:
                syscall_name = parts[-1]
                calls = int(parts[0])
                syscalls[syscall_name] = calls
                total_syscalls += calls
    
    print(f"System call statistics: {len(syscalls)} types, {total_syscalls} total calls")
    return syscalls, total_syscalls

# Perf output parsing function removed as requested

def write_to_csv(csv_file, result, include_syscalls=False):
    """Write benchmark results to a CSV file."""
    # Check if file exists to determine if we need to write headers
    file_exists = os.path.isfile(csv_file)
    
    # Basic fields to always include
    fields = ["connection_count", "thread_count", "tps_request", "tps_response", "success_rate"]
    
    if include_syscalls:
        fields.append("total_syscalls")
        # Add top syscalls
        if "syscalls" in result and result["syscalls"]:
            # Sort syscalls by count
            top_syscalls = sorted(result["syscalls"].items(), key=lambda x: x[1], reverse=True)
            # Take top 10 or all if less than 10
            top_syscalls = top_syscalls[:min(10, len(top_syscalls))]
            for syscall, _ in top_syscalls:
                fields.append(f"syscall_{syscall}")
    
    # Add timestamp field
    fields.append("timestamp")
    
    with open(csv_file, 'a', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        
        # Write header if file is new
        if not file_exists:
            writer.writeheader()
        
        # Prepare row data
        row_data = {
            "connection_count": result.get("connection_count", ""),
            "thread_count": result.get("thread_count", ""),
            "tps_request": result.get("tps_request", 0),
            "tps_response": result.get("tps_response", 0),
            "success_rate": result.get("success_rate", 0),
            "timestamp": TIMESTAMP
        }
        
        if include_syscalls and "syscalls" in result and "total_syscalls" in result:
            row_data["total_syscalls"] = result["total_syscalls"]
            # Add top syscalls
            top_syscalls = sorted(result["syscalls"].items(), key=lambda x: x[1], reverse=True)
            top_syscalls = top_syscalls[:min(10, len(top_syscalls))]
            for syscall, count in top_syscalls:
                row_data[f"syscall_{syscall}"] = count
        
        writer.writerow(row_data)
    
    print(f"Results appended to CSV file: {csv_file}")

def run_combined_benchmark(connections, thread_counts, server_address, duration=30, 
                           run_regular=True, run_strace=True):
    """Run comprehensive benchmarks for all combinations of connections and thread counts."""
    print("\n===== Starting comprehensive benchmark =====")
    
    # Create CSV files for each type of benchmark
    regular_csv = os.path.join(RESULTS_DIR, f"regular_results_{TIMESTAMP}.csv")
    strace_csv = os.path.join(RESULTS_DIR, f"strace_results_{TIMESTAMP}.csv")
    
    # Initialize empty dataframes for results
    regular_results = []
    strace_results = []
    
    # Create combined results directory
    combined_dir = os.path.join(RESULTS_DIR, f"combined_benchmark_{TIMESTAMP}")
    os.makedirs(combined_dir, exist_ok=True)
    
    # Log file for this run
    log_file = os.path.join(combined_dir, "benchmark_log.txt")
    with open(log_file, 'w', encoding='utf-8') as f:
        f.write(f"===== Comprehensive Benchmark Log =====\n")
        f.write(f"Started: {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write(f"Server address: {server_address}\n")
        f.write(f"Duration per test: {duration} seconds\n")
        f.write(f"Connection counts: {connections}\n")
        f.write(f"Thread counts: {thread_counts}\n")
        f.write(f"Benchmark types: " + 
                f"{'Regular ' if run_regular else ''}" +
                f"{'Strace' if run_strace else ''}\n\n")
    
    # For each thread count
    for thread_count in thread_counts:
        print(f"\n{'='*50}")
        print(f"===== Testing with {thread_count} threads =====")
        print(f"{'='*50}")
        
        # Log thread count
        with open(log_file, 'a', encoding='utf-8') as f:
            f.write(f"\n{'='*50}\n")
            f.write(f"Starting tests with {thread_count} threads at {datetime.datetime.now().strftime('%H:%M:%S')}\n")
            f.write(f"{'='*50}\n\n")
        
        # Start server with this thread count
        server_process = start_server(thread_count)
        if not server_process:
            print(f"Failed to start server with {thread_count} threads. Skipping.")
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"Failed to start server with {thread_count} threads. Skipping.\n\n")
            continue
        
        try:
            # Give server time to initialize
            time.sleep(5)
            
            # For each connection count
            for conn_count in connections:
                print(f"\n{'-'*40}")
                print(f"Testing with {conn_count} connections")
                print(f"{'-'*40}")
                
                # Log connection count
                with open(log_file, 'a', encoding='utf-8') as f:
                    f.write(f"\nTesting with {conn_count} connections at {datetime.datetime.now().strftime('%H:%M:%S')}\n")
                
                # Run regular benchmark
                if run_regular:
                    print("\nRunning regular benchmark...")
                    regular_result = run_benchmark(conn_count, duration, server_address, thread_count=thread_count)
                    if regular_result:
                        regular_results.append(regular_result)
                        with open(log_file, 'a', encoding='utf-8') as f:
                            f.write(f"  Regular benchmark: {regular_result['tps_response']} TPS, {regular_result['success_rate']}% success\n")
                    else:
                        with open(log_file, 'a', encoding='utf-8') as f:
                            f.write(f"  Regular benchmark failed\n")
                
                # Run strace benchmark
                if run_strace:
                    print("\nRunning strace benchmark...")
                    strace_result = run_strace_benchmark(conn_count, duration, server_address, thread_count=thread_count)
                    if strace_result:
                        strace_results.append(strace_result)
                        with open(log_file, 'a', encoding='utf-8') as f:
                            f.write(f"  Strace benchmark: {strace_result['tps_response']} TPS, {strace_result['success_rate']}% success, {strace_result['total_syscalls']} syscalls\n")
                    else:
                        with open(log_file, 'a', encoding='utf-8') as f:
                            f.write(f"  Strace benchmark failed\n")
                
                with open(log_file, 'a', encoding='utf-8') as f:
                    f.write(f"Completed tests with {conn_count} connections\n")
                
        finally:
            # Stop server
            print(f"Stopping server with {thread_count} threads...")
            stop_server(server_process)
            time.sleep(2)  # Give time for server to fully shut down
            
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"\nCompleted all tests with {thread_count} threads at {datetime.datetime.now().strftime('%H:%M:%S')}\n")
    
    # Log completion
    with open(log_file, 'a', encoding='utf-8') as f:
        f.write(f"\n{'='*50}\n")
        f.write(f"All benchmarks completed at {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        
        # Summary statistics
        f.write("\n===== Summary =====\n")
        if regular_results:
            max_regular = max(regular_results, key=lambda x: x['tps_response'])
            f.write(f"Best regular performance: {max_regular['tps_response']} TPS with {max_regular['thread_count']} threads and {max_regular['connection_count']} connections\n")
        
        if strace_results:
            max_strace = max(strace_results, key=lambda x: x['tps_response'])
            f.write(f"Best strace performance: {max_strace['tps_response']} TPS with {max_strace['thread_count']} threads and {max_strace['connection_count']} connections\n")
    
    print(f"\nAll benchmarks completed! Results saved to: {combined_dir}")
    print(f"CSV files: {regular_csv}, {strace_csv}")
    
    return regular_results, strace_results


def main():
    parser = argparse.ArgumentParser(description="TCP Server Performance Benchmarking")
    
    # Simplified arguments
    parser.add_argument("--connections", type=str, default="10,50,100,500,1000,2000",
                      help="Connection counts to test (comma-separated, default: 10,50,100,500,1000,2000)")
    parser.add_argument("--threads", type=str, default="1,2,4,8,16",
                      help="Thread counts to test (comma-separated, default: 1,2,4,8,16)")
    parser.add_argument("--duration", type=int, default=30,
                      help="Duration in seconds for each benchmark (default: 30)")
    parser.add_argument("--server-address", default="127.0.0.1:8080",
                      help="Server address (default: 127.0.0.1:8080)")
    parser.add_argument("--skip-compile", action="store_true",
                      help="Skip compilation of benchmark tool")
    
    # Benchmark types
    parser.add_argument("--no-regular", action="store_true",
                      help="Skip regular benchmarks")
    parser.add_argument("--no-strace", action="store_true",
                      help="Skip strace benchmarks")
    
    args = parser.parse_args()
    
    # Parse connection counts
    try:
        connections = [int(x.strip()) for x in args.connections.split(',')]
    except ValueError:
        print("Error: Connection counts must be comma-separated integers.")
        return 1
    
    # Parse thread counts
    try:
        thread_counts = [int(x.strip()) for x in args.threads.split(',')]
    except ValueError:
        print("Error: Thread counts must be comma-separated integers.")
        return 1
    
    # Compile benchmark tool if needed
    if not args.skip_compile:
        if not compile_bench():
            print("Failed to compile benchmark tool. Exiting.")
            return 1
    
    # Run combined benchmark
    try:
        print(f"\n===== Starting comprehensive benchmark with {len(connections)} connection counts and {len(thread_counts)} thread counts =====")
        print(f"Server address: {args.server_address}")
        print(f"Duration per test: {args.duration} seconds")
        print(f"Connection counts: {connections}")
        print(f"Thread counts: {thread_counts}")
        print(f"Benchmark types: " + 
              f"{'Regular ' if not args.no_regular else ''}" +
              f"{'Strace' if not args.no_strace else ''}")
        
        regular_results, strace_results = run_combined_benchmark(
            connections=connections,
            thread_counts=thread_counts,
            server_address=args.server_address,
            duration=args.duration,
            run_regular=(not args.no_regular),
            run_strace=(not args.no_strace)
        )
        
        print("\n====== All benchmarks completed! ======")
        print(f"Results are saved in the 'benchmark_results' directory.")
        print(f"CSV files with timestamp {TIMESTAMP} contain raw data.")
        
        return 0
    
    except KeyboardInterrupt:
        print("\nBenchmark interrupted by user.")
        return 1
    except Exception as e:
        print(f"\nAn error occurred during benchmarking: {str(e)}")
        return 1

if __name__ == "__main__":
    sys.exit(main())