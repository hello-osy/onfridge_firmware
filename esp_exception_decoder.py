#!/usr/bin/env python3

"""ESP Exception Decoder

github:  https://github.com/janLo/EspArduinoExceptionDecoder
license: GPL v3
author:  Jan Losinski
"""

import argparse  # 명령줄 인자 파싱
import re  # 정규식을 통한 문자열 매칭
import subprocess  # 외부 프로그램 실행(여기서는 addr2line 호출)
from collections import namedtuple  # 스택 트레이스 데이터를 저장할 간단한 구조체
import sys  # 프로그램 종료와 같은 시스템 호출
import os  # 파일 경로 처리와 존재 여부 확인

# 지원하는 플랫폼
PLATFORMS = {
    "ESP8266": "lx106",
    "ESP32": "esp32"
}

# 정규식 정의
BACKTRACE_REGEX = re.compile(
    r"^Backtrace: (?P<trace>(?:0x[0-9a-fA-F]+:0x[0-9a-fA-F]+\s*)+)$"
)
ADDR_REGEX = re.compile(r"(0x[0-9a-fA-F]+):0x[0-9a-fA-F]+")

# Backtrace와 레지스터 정보를 저장할 데이터 구조
BacktraceLine = namedtuple("BacktraceLine", ["address"])

def validate_file(path, description):
    path = os.path.abspath(os.path.expanduser(path))
    if not os.path.exists(path):
        print(f"ERROR: {description} not found ({path})")
        sys.exit(1)
    return path

# Backtrace 데이터를 파싱하는 클래스
class ExceptionDataParser:
    def __init__(self):
        self.backtrace = []  # Backtrace 데이터를 저장

    def parse_file(self, file):
        for line in file:
            line = line.strip()
            if match := BACKTRACE_REGEX.search(line):
                self.backtrace = [
                    BacktraceLine(address=addr)
                    for addr in ADDR_REGEX.findall(match.group("trace"))
                ]
                break
        if not self.backtrace:
            print("ERROR: No valid backtrace found.")
            sys.exit(1)

# 주소를 디코딩하여 함수 이름 및 소스 코드 라인으로 변환하는 클래스
class AddressResolver:
    def __init__(self, tool_path, elf_path):
        self._tool = tool_path  # addr2line 도구 경로를 저장
        self._elf = elf_path  # 디코딩에 사용될 ELF 파일 경로를 저장
        self._address_map = {}  # 주소와 디코딩된 결과를 저장하는 맵

    def _lookup(self, addresses):
        cmd = [self._tool, "-aipfC", "-e", self._elf] + addresses
        output = subprocess.check_output(cmd, encoding="utf-8")
        for addr, result in zip(addresses, output.splitlines()):
            self._address_map[addr] = result

    def fill(self, parser):
        addresses = [line.address for line in parser.backtrace]
        self._lookup(addresses)

    def resolve_addr(self, addr):
        return self._address_map.get(addr, f"{addr}: ??")

# 디코딩된 Backtrace를 출력하는 함수
def print_result(parser, resolver):
    print("Decoded Backtrace:")
    for line in parser.backtrace:
        print(resolver.resolve_addr(line.address))

# 명령줄 인자를 처리하고 필요한 옵션 값을 반환하는 함수
def parse_args():
    parser = argparse.ArgumentParser(description="Decode ESP Backtrace.")
    parser.add_argument(
        "-p", "--platform", help="The platform to decode from", choices=PLATFORMS.keys(), default="ESP32"
    )
    parser.add_argument(
        "-t",
        "--tool",
        help="Path to the xtensa toolchain",
        default="/root/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32-elf-addr2line",
    )
    parser.add_argument(
        "-e",
        "--elf",
        help="Path to the ELF file",
        default="/app/.pio/build/esp32dev/firmware.elf",
    )
    parser.add_argument(
        "file", help="The file to read the exception data from ('-' for STDIN)", default="-"
    )
    return parser.parse_args()

# 프로그램의 메인 실행 부분
if __name__ == "__main__":
    args = parse_args()

    addr2line = validate_file(args.tool, "addr2line")
    elf_file = validate_file(args.elf, "ELF file")

    if args.file == "-":
        file = sys.stdin
    else:
        file = validate_file(args.file, "Input file")
        file = open(args.file, "r")

    parser = ExceptionDataParser()
    resolver = AddressResolver(addr2line, elf_file)

    parser.parse_file(file)
    resolver.fill(parser)
    print_result(parser, resolver)
