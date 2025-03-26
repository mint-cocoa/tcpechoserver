#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import argparse
import datetime

# 패키지 경로 추가
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# 절대 경로로 임포트
from bench_project.utils.command import compile_bench
from bench_project.runners.combined_benchmark import run_combined_benchmark
from bench_project.config import CONFIG, add_server_path, set_server_type
from bench_project.config import SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL

# 결과 디렉토리 생성
RESULTS_DIR = CONFIG['results_dir']
os.makedirs(RESULTS_DIR, exist_ok=True)

# 현재 실행을 위한 타임스탬프 생성
TIMESTAMP = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")

def main():
    """벤치마크 실행의 메인 함수"""
    from bench_project.config import CONFIG, add_server_path, SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL
    
    # 결과 디렉토리 생성
    os.makedirs(CONFIG['results_dir'], exist_ok=True)
    
    # 현재 시간을 기록
    timestamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    
    # 명령행 인자 파싱
    parser = argparse.ArgumentParser(description="TCP 에코 서버 성능 벤치마크")
    parser.add_argument("--connections", "-c", type=int, nargs="+", default=[CONFIG.get('default_connections', 100)],
                        help=f"동시 연결 수 (기본값: {CONFIG.get('default_connections', 100)})")
    parser.add_argument("--threads", "-t", type=int, nargs="+", default=[CONFIG.get('default_threads', 4)],
                        help=f"서버 스레드 수 (기본값: {CONFIG.get('default_threads', 4)})")
    parser.add_argument("--duration", "-d", type=int, default=CONFIG.get('default_duration', 30),
                        help=f"각 벤치마크 실행 지속 시간(초) (기본값: {CONFIG.get('default_duration', 30)}초)")
    parser.add_argument("--server-address", "-a", default=CONFIG.get('default_server_address', "127.0.0.1:8080"),
                        help=f"서버 주소 (기본값: {CONFIG.get('default_server_address', '127.0.0.1:8080')})")
    parser.add_argument("--server-path", "-p", help="서버 바이너리 경로 (옵션)")
    parser.add_argument("--server-type", "-s", choices=[SERVER_TYPE_IOURING, SERVER_TYPE_EPOLL],
                        default=CONFIG.get('default_server_type', SERVER_TYPE_IOURING),
                        help=f"사용할 서버 유형 (기본값: {CONFIG.get('default_server_type', SERVER_TYPE_IOURING)})")
    
    # 벤치마크 유형 설정
    benchmark_group = parser.add_argument_group("벤치마크 유형")
    benchmark_group.add_argument("--regular", action="store_true", default=True,
                                 help="일반 벤치마크 실행 (기본값: 사용)")
    benchmark_group.add_argument("--strace", action="store_true", default=False,
                                 help="strace를 사용한 벤치마크 실행 (기본값: 사용 안 함)")
    
    args = parser.parse_args()
    
    
    
    # 서버 경로가 지정된 경우 추가
    if args.server_path:
        server_path = os.path.abspath(args.server_path)
        if not os.path.exists(server_path):
            print(f"오류: 지정한 서버 경로가 존재하지 않습니다: {server_path}")
            return 1
        
        if not os.access(server_path, os.X_OK):
            print(f"오류: 지정한 서버 파일에 실행 권한이 없습니다: {server_path}")
            return 1
        
        add_server_path(server_path, args.server_type)
    
    # 벤치마크 실행 방식 결정
    is_combined = len(args.threads) > 1 or len(args.connections) > 1
    
    print("\n===== 벤치마크 설정 =====")
    print(f"타임스탬프: {timestamp}")
    print(f"서버 주소: {args.server_address}")
    print(f"서버 유형: {args.server_type}")
    print(f"연결 수: {', '.join(str(c) for c in args.connections)}")
    print(f"스레드 수: {', '.join(str(t) for t in args.threads)}")
    print(f"지속 시간: {args.duration}초")
    print(f"벤치마크 유형: {'일반' if args.regular else ''} {'strace' if args.strace else ''}")
    print(f"벤치마크 모드: {'종합' if is_combined else '단일'}")
    
    # 단일 스레드 수와 단일 연결 수로 실행 (일반 벤치마크)
    if not is_combined:
        thread_count = args.threads[0]
        conn_count = args.connections[0]
        
        if args.regular:
            from bench_project.runners.regular_benchmark import run_regular_benchmark
            print("\n===== 일반 벤치마크 시작 =====")
            result = run_regular_benchmark(
                conn_count,
                thread_count,
                args.server_address,
                duration=args.duration,
                results_dir=CONFIG['results_dir'],
                timestamp=timestamp,
                server_type=args.server_type
            )
            if not result:
                print("일반 벤치마크 실패")
                return 1
        
        if args.strace:
            from bench_project.runners.strace_benchmark import run_strace_benchmark
            print("\n===== strace 벤치마크 시작 =====")
            result = run_strace_benchmark(
                conn_count,
                thread_count,
                args.server_address,
                duration=args.duration,
                results_dir=CONFIG['results_dir'],
                timestamp=timestamp,
                server_type=args.server_type
            )
            if not result:
                print("strace 벤치마크 실패")
                return 1
    
    # 여러 스레드 수 또는 여러 연결 수로 실행 (종합 벤치마크)
    else:
        from bench_project.runners.combined_benchmark import run_combined_benchmark
        
        print("\n===== 종합 벤치마크 시작 =====")
        result = run_combined_benchmark(
            args.connections,
            args.threads,
            args.server_address,
            duration=args.duration,
            run_regular=args.regular,
            run_strace=args.strace,
            results_dir=CONFIG['results_dir'],
            timestamp=timestamp,
            server_type=args.server_type
        )
        if not result:
            print("종합 벤치마크 실패")
            return 1
    
    print("\n===== 벤치마크 완료 =====")
    print(f"결과는 '{CONFIG['results_dir']}' 디렉토리에 저장되었습니다.")
    return 0

if __name__ == "__main__":
    sys.exit(main()) 