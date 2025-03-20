#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
서버 설정 관리 도구 실행 스크립트
"""

import os
import sys

# 현재 디렉토리를 시스템 경로에 추가
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# 설정 도구 모듈 가져오기
from bench_project.server_config import main

if __name__ == "__main__":
    sys.exit(main()) 