#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
TCP 서버 벤치마크 실행 스크립트
명령줄 인수만으로 벤치마크를 실행할 수 있는 간소화 버전
"""

import os
import sys
import argparse
import datetime
import time
import signal
import subprocess
import socket
import psutil
import re
import csv

# 현재 실행을 위한 타임스탬프 생성
TIMESTAMP = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

# 기본 결과 디렉토리
DEFAULT_RESULTS_DIR = "benchmark_results"

def run_cmd(cmd, shell=False, capture_output=True):
    """명령어를 실행하고 결과를 반환합니다."""
    print(f"실행: {' '.join(cmd) if isinstance(cmd, list) else cmd}")
    result = subprocess.run(cmd, shell=shell, capture_output=capture_output, text=True)
    if result.returncode != 0 and capture_output:
        print(f"오류: {result.stderr}")
    return result

def compile_bench():
    """벤치마크 도구를 컴파일합니다."""
    print("벤치마크 도구 컴파일 중...")
    result = run_cmd(["cargo", "build", "--release", "--bin", "bench"])
    if result.returncode == 0:
        print("벤치마크 도구 컴파일 성공!")
        return True
    else:
        print("벤치마크 도구 컴파일 실패!")
        return False

def find_server_pid(host, port):
    """서버 프로세스 ID를 찾습니다."""
    port = int(port)
    
    print(f"서버 PID 검색 중 (주소: {host}:{port})...")
    
    # 프로세스 이름으로 검색
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            # 서버 프로세스 확인
            if proc.info['name'] and any(name in proc.info['name'] for name in ['server', 'echo']):
                # 연결 확인
                conns = proc.connections()
                for conn in conns:
                    if conn.status == 'LISTEN' and conn.laddr.port == port:
                        print(f"서버 프로세스 발견: PID={proc.pid}, 이름={proc.info['name']}")
                        return proc.pid
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    
    # 포트로만 검색
    for proc in psutil.process_iter(['pid', 'name']):
        try:
            conns = proc.connections()
            for conn in conns:
                if conn.status == 'LISTEN' and conn.laddr.port == port:
                    print(f"포트 {port}를 사용하는 프로세스 발견: PID={proc.pid}, 이름={proc.info['name']}")
                    return proc.pid
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
            continue
    
    print(f"경고: 포트 {port}를 사용하는 서버 프로세스를 찾을 수 없습니다.")
    return None

def start_server(server_path, host, port, thread_count=4):
    """지정된 스레드 수로 서버를 시작합니다."""
    print(f"서버 시작 중 (스레드 수: {thread_count})...")
    
    if not os.path.exists(server_path):
        print(f"오류: 서버 경로가 존재하지 않습니다 - {server_path}")
        return None
    
    if not os.access(server_path, os.X_OK):
        print(f"오류: 서버 파일에 실행 권한이 없습니다 - {server_path}")
        return None
    
    # 서버 실행 명령 - <server_path> <host> <port> [num_threads] 형태
    server_cmd = [
        server_path,
        host,
        port,
        str(thread_count)
    ]
    
    server_process = subprocess.Popen(server_cmd)
    
    # 서버 시작 대기
    print("서버 시작 대기 중...")
    for i in range(10):
        time.sleep(1)
        print(f"서버 시작 대기: {i+1}초...")
        
        # 서버 프로세스 실행 확인
        if server_process.poll() is not None:
            print(f"서버가 시작 중에 종료되었습니다. 종료 코드: {server_process.poll()}")
            return None
        
        # 포트 확인
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
                s.settimeout(1)
                if s.connect_ex((host, int(port))) == 0:
                    print("서버가 성공적으로 시작되었습니다!")
                    return server_process
        except Exception as e:
            print(f"포트 확인 중 오류 발생: {e}")
    
    print("서버 시작 시간 초과")
    if server_process.poll() is None:
        server_process.terminate()
    return None

def parse_bench_output(output, duration):
    """벤치마크 출력을 분석하여 TPS와 성공률을 추출합니다."""
    tps_request = 0
    tps_response = 0
    success_rate = 0
    
    # "Speed: {} request/sec, {} response/sec" 구문 파싱
    speed_match = re.search(r'Speed: (\d+) request/sec, (\d+) response/sec', output)
    if speed_match:
        tps_request = int(speed_match.group(1))
        tps_response = int(speed_match.group(2))
        print(f"TPS: 요청={tps_request}/초, 응답={tps_response}/초")
    
    # "Requests: {}" 구문 파싱
    req_match = re.search(r'Requests: (\d+)', output)
    if req_match and tps_request == 0:  # Speed에서 이미 파싱되지 않은 경우에만 사용
        tps_request = int(int(req_match.group(1)) / duration)
        print(f"요청 TPS (계산): {tps_request}/초")
    
    # "Responses: {}" 구문 파싱
    resp_match = re.search(r'Responses: (\d+)', output)
    if resp_match and tps_response == 0:  # Speed에서 이미 파싱되지 않은 경우에만 사용
        tps_response = int(int(resp_match.group(1)) / duration)
        print(f"응답 TPS (계산): {tps_response}/초")
    
    # "Success rate: {:.2}%" 구문 파싱
    success_match = re.search(r'Success rate: ([\d.]+)%', output)
    if success_match:
        success_rate = float(success_match.group(1))
        print(f"성공률: {success_rate}%")
    
    return tps_request, tps_response, success_rate

def parse_strace_output(output):
    """strace 출력을 분석하여 시스템 콜 통계를 추출합니다."""
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
    
    print(f"시스템 콜 통계: {len(syscalls)} 종류, 총 {total_syscalls}회 호출")
    return syscalls, total_syscalls

def write_to_csv(csv_file, result, include_syscalls=False):
    """벤치마크 결과를 CSV 파일에 저장합니다."""
    # 파일 존재 여부에 따라 헤더 작성 여부 결정
    file_exists = os.path.isfile(csv_file)
    
    # 기본 필드
    fields = ["connection_count", "thread_count", "tps_request", "tps_response", "success_rate"]
    
    if include_syscalls:
        fields.append("total_syscalls")
        # 상위 시스템 콜 추가
        if "syscalls" in result and result["syscalls"]:
            # 시스템 콜을 개수 기준으로 정렬
            top_syscalls = sorted(result["syscalls"].items(), key=lambda x: x[1], reverse=True)
            # 상위 10개 또는 전체 (10개 미만인 경우)
            top_syscalls = top_syscalls[:min(10, len(top_syscalls))]
            for syscall, _ in top_syscalls:
                fields.append(f"syscall_{syscall}")
    
    # 타임스탬프 필드 추가
    fields.append("timestamp")
    
    with open(csv_file, 'a', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        
        # 새 파일이면 헤더 작성
        if not file_exists:
            writer.writeheader()
        
        # 행 데이터 준비
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
            # 상위 시스템 콜 추가
            top_syscalls = sorted(result["syscalls"].items(), key=lambda x: x[1], reverse=True)
            top_syscalls = top_syscalls[:min(10, len(top_syscalls))]
            for syscall, count in top_syscalls:
                row_data[f"syscall_{syscall}"] = count
        
        writer.writerow(row_data)
    
    print(f"결과가 CSV 파일에 추가되었습니다: {csv_file}")

def run_regular_benchmark(conn_count, thread_count, host, port, 
                         duration=30, csv_file=None, results_dir=DEFAULT_RESULTS_DIR,
                         timestamp=TIMESTAMP):
    """strace 없이 일반 벤치마크를 실행합니다."""
    print(f"\n===== {conn_count} 연결로 일반 벤치마크 시작 =====")
    
    # 결과 디렉토리 생성
    os.makedirs(results_dir, exist_ok=True)
    
    # CSV 파일이 지정되지 않은 경우 결과 디렉토리에 생성
    if csv_file is None:
        csv_file = os.path.join(results_dir, f"regular_results_{timestamp}.csv")
    
    # 벤치마크 실행
    server_address = f"{host}:{port}"
    bench_cmd = [
        "cargo", "run", "--release", "--bin", "bench", "--",
        "-a", server_address,
        "-c", str(conn_count),
        "-t", str(duration),
        "-l", str(1024)
    ]
    
    print(f"실행: {' '.join(bench_cmd)}")
    result = subprocess.run(bench_cmd, capture_output=True, text=True)
    
    if result.returncode != 0:
        error_msg = f"종료 코드: {result.returncode}\n"
        if result.stderr:
            error_msg += f"오류 메시지: {result.stderr}\n"
        print(f"벤치마크 실패: {error_msg}")
        
        # 오류 정보 저장
        error_file = os.path.join(results_dir, f"error_conn_{conn_count}_{timestamp}.txt")
        with open(error_file, 'w', encoding='utf-8') as f:
            f.write(f"벤치마크 실패 (연결 수: {conn_count})\n")
            f.write(f"명령어: {' '.join(bench_cmd)}\n")
            f.write(error_msg)
            if result.stdout:
                f.write(f"\n표준 출력:\n{result.stdout}\n")
        print(f"오류 정보 저장 위치: {error_file}")
        
        return None
    
    output = result.stdout
    print(output)
    
    # 벤치마크 결과 분석
    tps_request, tps_response, success_rate = parse_bench_output(output, duration)
    
    # 원시 출력 저장
    result_file = os.path.join(results_dir, f"regular_benchmark_conn_{conn_count}_{timestamp}.txt")
    with open(result_file, 'w', encoding='utf-8') as f:
        f.write(output)
    print(f"벤치마크 결과 저장 위치: {result_file}")
    
    # 결과 딕셔너리 생성
    result_dict = {
        "connection_count": conn_count,
        "thread_count": thread_count,
        "tps_request": tps_request,
        "tps_response": tps_response,
        "success_rate": success_rate,
        "raw_output": output
    }
    
    # 결과를 CSV에 저장
    write_to_csv(csv_file, result_dict)
    
    return result_dict

def run_strace_benchmark(conn_count, thread_count, host, port, 
                       duration=30, csv_file=None, results_dir=DEFAULT_RESULTS_DIR,
                       timestamp=TIMESTAMP):
    """strace를 사용한 벤치마크를 실행합니다."""
    print(f"\n===== {conn_count} 연결로 strace 벤치마크 시작 =====")
    
    # 결과 디렉토리 생성
    os.makedirs(results_dir, exist_ok=True)
    
    # CSV 파일이 지정되지 않은 경우 결과 디렉토리에 생성
    if csv_file is None:
        csv_file = os.path.join(results_dir, f"strace_results_{timestamp}.csv")
    
    # strace 출력 파일 정의
    strace_output = os.path.join(results_dir, f"strace_output_conn_{conn_count}_threads_{thread_count}_{timestamp}.txt")
    
    # 메시지 길이 설정
    message_length = 1024
    
    # 서버 PID 찾기
    server_address = f"{host}:{port}"
    server_pid = find_server_pid(host, port)
    if not server_pid:
        print("서버 PID를 찾을 수 없습니다. strace 벤치마크를 실행할 수 없습니다.")
        return None
    
    strace_log_file = os.path.join(results_dir, f"strace_server_pid_{server_pid}_conn_{conn_count}_{timestamp}.log")
    strace_cmd = [
        "sudo", "strace", "-c", "-p", str(server_pid), "-o", strace_log_file
    ]
    
    print(f"서버 프로세스에 대한 시스템 콜 추적 시작 (PID: {server_pid})...")
    print(f"Strace 명령: {' '.join(strace_cmd)}")
    print("프롬프트가 나타나면 sudo 비밀번호를 입력하세요...")
    
    try:
        # strace를 백그라운드로 실행
        strace_process = subprocess.Popen(strace_cmd)
        
        # strace 시작 대기
        print("strace 시작 대기 중... (5초)")
        time.sleep(5)
        
        # strace 실행 확인
        if strace_process.poll() is not None:
            print(f"strace 시작 실패 (종료 코드: {strace_process.returncode})")
            return None
        
        print(f"strace 시작됨 (PID: {strace_process.pid})")
        
        # 추가 대기
        time.sleep(2)
        
        # 벤치마크 클라이언트 실행
        print(f"{conn_count} 연결로 벤치마크 실행 중...")
        bench_cmd = [
            "cargo", "run", "--release", "--bin", "bench", "--",
            "-a", server_address,
            "-c", str(conn_count),
            "-t", str(duration),
            "-l", str(message_length)
        ]
        
        bench_result = run_cmd(bench_cmd)
        bench_output = bench_result.stdout
        
        print("벤치마크 완료. strace 종료 중...")
        
        # strace 중지
        strace_process.send_signal(signal.SIGINT)
        time.sleep(1)
        
        if strace_process.poll() is None:
            print("strace 프로세스가 응답하지 않습니다. 강제 종료 중...")
            strace_process.terminate()
            time.sleep(1)
            if strace_process.poll() is None:
                strace_process.kill()
        
        print(f"strace 종료됨 (종료 코드: {strace_process.returncode})")
        
        # strace 로그 읽기
        with open(strace_log_file, 'r', encoding='utf-8') as f:
            strace_output_text = f.read()
        
        print(f"strace 로그 파일 읽기 완료: {strace_log_file}")
        
        # 벤치마크 결과 분석
        tps_request, tps_response, success_rate = parse_bench_output(bench_output, duration)
        
        # 시스템 콜 통계 분석
        syscalls, total_syscalls = parse_strace_output(strace_output_text)
        
        # 결과 딕셔너리 생성
        result = {
            "connection_count": conn_count,
            "thread_count": thread_count,
            "tps_request": tps_request,
            "tps_response": tps_response,
            "success_rate": success_rate,
            "total_syscalls": total_syscalls,
            "syscalls": syscalls,
            "raw_benchmark_output": bench_output,
            "raw_strace_output": strace_output_text,
            "server_pid": server_pid
        }
        
        # 결과를 CSV에 저장
        write_to_csv(csv_file, result, include_syscalls=True)
        
        return result
        
    except Exception as e:
        print(f"strace 벤치마크 중 오류 발생: {str(e)}")
        try:
            strace_process.terminate()
        except:
            pass
        return None

def run_recursive_benchmark(connections, thread_counts, host, port, server_path,
                           duration=30, run_regular=True, run_strace=True, 
                           results_dir=DEFAULT_RESULTS_DIR, timestamp=TIMESTAMP, 
                           thread_idx=0):
    """재귀적으로 벤치마크를 실행합니다."""
    # 모든 스레드 수를 처리했으면 종료
    if thread_idx >= len(thread_counts):
        return True
    
    thread_count = thread_counts[thread_idx]
    print(f"\n{'='*50}")
    print(f"===== {thread_count} 스레드로 테스트 시작 =====")
    print(f"{'='*50}")
    
    # 로그 파일 설정
    combined_dir = os.path.join(results_dir, f"combined_benchmark_{timestamp}")
    os.makedirs(combined_dir, exist_ok=True)
    log_file = os.path.join(combined_dir, "benchmark_log.txt")
    
    # 스레드 수 로그 기록
    with open(log_file, 'a', encoding='utf-8') as f:
        f.write(f"\n{'='*50}\n")
        f.write(f"{thread_count} 스레드로 테스트 시작 ({datetime.datetime.now().strftime('%H:%M:%S')})\n")
        f.write(f"{'='*50}\n\n")
    
    # 서버 시작
    server_process = start_server(server_path, host, port, thread_count)
    if not server_process:
        print(f"{thread_count} 스레드로 서버 시작 실패. 다음 스레드 수로 진행합니다.")
        with open(log_file, 'a', encoding='utf-8') as f:
            f.write(f"{thread_count} 스레드로 서버 시작 실패\n\n")
        
        # 다음 스레드 수로 재귀 호출
        return run_recursive_benchmark(connections, thread_counts, host, port, server_path,
                                     duration, run_regular, run_strace, results_dir,
                                     timestamp, thread_idx + 1)
    
    try:
        # 서버가 완전히 시작할 때까지 대기
        time.sleep(3)
        
        # 모든 연결 수에 대해 벤치마크 실행
        for conn_count in connections:
            print(f"\n----- {conn_count} 연결로 테스트 시작 -----")
            
            # 연결 수 로그 기록
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"{conn_count} 연결로 테스트 시작 ({datetime.datetime.now().strftime('%H:%M:%S')})\n")
            
            # 일반 벤치마크 실행
            if run_regular:
                print(f"\n----- 일반 벤치마크 실행 (연결: {conn_count}, 스레드: {thread_count}) -----")
                csv_file = os.path.join(results_dir, f"regular_results_{timestamp}.csv")
                
                result = run_regular_benchmark(
                    conn_count=conn_count,
                    thread_count=thread_count,
                    host=host,
                    port=port,
                    duration=duration,
                    csv_file=csv_file,
                    results_dir=combined_dir,
                    timestamp=timestamp
                )
                
                if result:
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"  일반 벤치마크 완료 - TPS: {result.get('tps_request', 'N/A')}\n")
                else:
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"  일반 벤치마크 실패\n")
            
            # strace 벤치마크 실행
            if run_strace:
                print(f"\n----- strace 벤치마크 실행 (연결: {conn_count}, 스레드: {thread_count}) -----")
                csv_file = os.path.join(results_dir, f"strace_results_{timestamp}.csv")
                
                result = run_strace_benchmark(
                    conn_count=conn_count,
                    thread_count=thread_count,
                    host=host,
                    port=port,
                    duration=duration,
                    csv_file=csv_file,
                    results_dir=combined_dir,
                    timestamp=timestamp
                )
                
                if result:
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"  strace 벤치마크 완료 - TPS: {result.get('tps_request', 'N/A')}\n")
                else:
                    with open(log_file, 'a', encoding='utf-8') as f:
                        f.write(f"  strace 벤치마크 실패\n")
            
            # 이 연결 수에 대한 테스트 완료 로그
            with open(log_file, 'a', encoding='utf-8') as f:
                f.write(f"{conn_count} 연결 테스트 완료 ({datetime.datetime.now().strftime('%H:%M:%S')})\n\n")
        
        # 다음 스레드 수로 재귀 호출
        return run_recursive_benchmark(connections, thread_counts, host, port, server_path,
                                     duration, run_regular, run_strace, results_dir,
                                     timestamp, thread_idx + 1)
        
    finally:
        # 서버 프로세스 종료 확인
        if server_process and server_process.poll() is None:
            try:
                print(f"서버 프로세스 종료 중 (PID: {server_process.pid})...")
                server_process.terminate()
                server_process.wait(5)
            except:
                print(f"서버 프로세스 강제 종료 (PID: {server_process.pid})...")
                server_process.kill()

def main():
    """벤치마크 실행 메인 함수"""
    # 결과 디렉토리 생성
    os.makedirs(DEFAULT_RESULTS_DIR, exist_ok=True)
    
    # 명령행 인자 파싱
    parser = argparse.ArgumentParser(description="TCP 에코 서버 성능 벤치마크")
    parser.add_argument("--connections", "-c", type=str, default="10,50,100,500,1000,2000",
                      help="테스트할 연결 수 (쉼표로 구분, 기본값: 10,50,100,500,1000,2000)")
    parser.add_argument("--threads", "-t", type=str, default="1,2,4,8,16",
                      help="테스트할 스레드 수 (쉼표로 구분, 기본값: 1,2,4,8,16)")
    parser.add_argument("--duration", "-d", type=int, default=30,
                      help="각 벤치마크 지속 시간(초) (기본값: 30)")
    parser.add_argument("--server-host", "-H", default="127.0.0.1",
                      help="서버 호스트 (기본값: 127.0.0.1)")
    parser.add_argument("--server-port", "-p", default="8080",
                      help="서버 포트 (기본값: 8080)")
    parser.add_argument("--server-path", "-s", required=True,
                      help="서버 바이너리 경로 (필수)")
    parser.add_argument("--results-dir", "-r", default=DEFAULT_RESULTS_DIR,
                      help=f"결과 저장 디렉토리 (기본값: {DEFAULT_RESULTS_DIR})")
    
    # 벤치마크 유형 설정
    parser.add_argument("--no-regular", action="store_true", 
                      help="일반 벤치마크 건너뛰기")
    parser.add_argument("--no-strace", action="store_true",
                      help="strace 벤치마크 건너뛰기")
    parser.add_argument("--skip-compile", action="store_true",
                      help="벤치마크 도구 컴파일 건너뛰기")
    
    args = parser.parse_args()
    
    # 연결 수 파싱
    try:
        connections = [int(x.strip()) for x in args.connections.split(',')]
    except ValueError:
        print("오류: 연결 수는 쉼표로 구분된 정수여야 합니다.")
        return 1
    
    # 스레드 수 파싱
    try:
        thread_counts = [int(x.strip()) for x in args.threads.split(',')]
    except ValueError:
        print("오류: 스레드 수는 쉼표로 구분된 정수여야 합니다.")
        return 1
    
    # 서버 경로 확인
    server_path = os.path.abspath(args.server_path)
    if not os.path.exists(server_path):
        print(f"오류: 지정한 서버 경로가 존재하지 않습니다: {server_path}")
        return 1
    
    if not os.access(server_path, os.X_OK):
        print(f"오류: 지정한 서버 파일에 실행 권한이 없습니다: {server_path}")
        return 1
    
    # 벤치마크 도구 컴파일
    if not args.skip_compile:
        if not compile_bench():
            print("벤치마크 도구 컴파일 실패. 종료합니다.")
            return 1
    
    # 벤치마크 설정 출력
    print("\n===== 벤치마크 설정 =====")
    print(f"타임스탬프: {TIMESTAMP}")
    print(f"서버 호스트: {args.server_host}")
    print(f"서버 포트: {args.server_port}")
    print(f"서버 경로: {server_path}")
    print(f"연결 수: {connections}")
    print(f"스레드 수: {thread_counts}")
    print(f"지속 시간: {args.duration}초")
    print(f"결과 디렉토리: {args.results_dir}")
    print(f"벤치마크 유형: {'일반' if not args.no_regular else ''} {'strace' if not args.no_strace else ''}")
    
    try:
        # 재귀적 벤치마크 실행
        result = run_recursive_benchmark(
            connections=connections,
            thread_counts=thread_counts,
            host=args.server_host,
            port=args.server_port,
            server_path=server_path,
            duration=args.duration,
            run_regular=(not args.no_regular),
            run_strace=(not args.no_strace),
            results_dir=args.results_dir,
            timestamp=TIMESTAMP
        )
        
        if result:
            print("\n===== 벤치마크 완료 =====")
            print(f"결과는 '{args.results_dir}' 디렉토리에 저장되었습니다.")
            return 0
        else:
            print("\n===== 벤치마크 실패 =====")
            return 1
            
    except KeyboardInterrupt:
        print("\n사용자에 의해 벤치마크가 중단되었습니다.")
        return 1
    except Exception as e:
        print(f"\n벤치마크 중 오류가 발생했습니다: {str(e)}")
        return 1

if __name__ == "__main__":
    sys.exit(main()) 