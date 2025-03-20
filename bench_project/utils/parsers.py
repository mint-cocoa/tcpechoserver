#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import re
import csv
import os

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

def write_to_csv(csv_file, result, include_syscalls=False, timestamp=None):
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
            "timestamp": timestamp
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