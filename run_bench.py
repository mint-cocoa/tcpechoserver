#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
TCP Server 벤치마크 실행 스크립트
이 스크립트는 bench_project 패키지를 사용하여 벤치마크를 실행합니다.
"""

import os
import sys

# 현재 디렉토리를 시스템 경로에 추가
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# bench_project 패키지에서 main 모듈 가져오기
from bench_project.main import main

if __name__ == "__main__":
    sys.exit(main())