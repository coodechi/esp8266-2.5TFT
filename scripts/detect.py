#!/usr/bin/env python3
"""识别当前 USB 串口上的 ESP8266，并把端口写到 .board-port。"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PORT_FILE = ROOT / ".board-port"
PIO = ROOT / ".venv" / "bin" / "pio"

SKIP_PARTS = ("Bluetooth", "debug-console", "EDIFIER", "wlan-debug")

# 常见 USB 转串口。NodeMCU 开发板大多是 CH340 或 CP2102。
USB_CHIPS = {
    "1A86:7523": "CH340",
    "1A86:5523": "CH343",
    "10C4:EA60": "CP2102",
    "0403:6001": "FTDI",
}


def fail(message: str) -> None:
    print(message)
    raise SystemExit(1)


def list_ports() -> list[dict]:
    if not PIO.is_file():
        fail("还没有安装工具。在当前目录运行 make setup")
    result = subprocess.run(
        [str(PIO), "device", "list", "--json-output"],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        fail(result.stderr.strip() or "无法列出串口")
    try:
        devices = json.loads(result.stdout or "[]")
    except json.JSONDecodeError:
        fail("串口列表不是合法的 JSON")
    ports = []
    for device in devices:
        port = device.get("port") or ""
        if not port.startswith("/dev/cu."):
            continue
        if any(part in port for part in SKIP_PARTS):
            continue
        ports.append(device)
    return ports


def esptool_commands() -> tuple[str, str]:
    result = subprocess.run(
        [sys.executable, "-m", "esptool", "--help"],
        text=True,
        capture_output=True,
        check=False,
    )
    help_text = (result.stdout or "") + (result.stderr or "")
    chip = "chip-id" if "chip-id" in help_text else "chip_id"
    flash = "flash-id" if "flash-id" in help_text else "flash_id"
    return chip, flash


def run_esptool(port: str, command: str) -> tuple[bool, str]:
    result = subprocess.run(
        [
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            "esp8266",
            "--port",
            port,
            "--baud",
            "115200",
            command,
        ],
        text=True,
        capture_output=True,
        check=False,
        timeout=25,
    )
    output = ((result.stdout or "") + (result.stderr or "")).strip()
    return result.returncode == 0, output


def line_value(output: str, *needles: str) -> str:
    for line in output.splitlines():
        lowered = line.lower()
        if any(needle in lowered for needle in needles):
            if ":" in line:
                return line.split(":", 1)[1].strip()
            return line.strip()
    return ""


def usb_chip(hwid: str) -> str:
    upper = hwid.upper()
    for vid_pid, name in USB_CHIPS.items():
        if vid_pid in upper:
            return name
    return "未知 USB 串口芯片"


def main() -> None:
    ports = list_ports()
    if not ports:
        fail("没有发现开发板串口。用能传数据的 Micro-B 线插上 NodeMCU，然后再运行 make detect")

    chip_cmd, flash_cmd = esptool_commands()
    matched: list[tuple[dict, str, str]] = []

    for device in ports:
        port = device["port"]
        hwid = device.get("hwid") or ""
        print(f"端口 {port}")
        print(f"  USB: {usb_chip(hwid)}  {hwid}")
        ok, chip_out = run_esptool(port, chip_cmd)
        if not ok or "ESP8266" not in chip_out.upper():
            print("  这块口没有应答成 ESP8266")
            if chip_out:
                print("  " + chip_out.splitlines()[-1])
            continue
        _, flash_out = run_esptool(port, flash_cmd)
        chip = line_value(chip_out, "chip is", "chip type")
        crystal = line_value(chip_out, "crystal")
        mac = line_value(chip_out, "mac")
        flash = line_value(flash_out, "detected flash size")
        print(f"  芯片: {chip or 'ESP8266'}")
        if crystal:
            print(f"  晶振: {crystal}")
        if mac:
            print(f"  MAC: {mac}")
        if flash:
            print(f"  闪存: {flash}")
        matched.append((device, flash, mac))

    if not matched:
        fail("串口在，但没有 ESP8266 应答。按住 FLASH，点一下 RST，松开 FLASH，再运行 make detect")
    if len(matched) > 1:
        fail("识别到多块 ESP8266。只留一块插在电脑上，再运行 make detect")

    _device, flash, _mac = matched[0]
    port = _device["port"]
    PORT_FILE.write_text(port + "\n", encoding="utf-8")
    print(f"已记下串口 {port}")
    if flash and "4MB" not in flash.upper().replace(" ", ""):
        print(f"当前工程按 4MB 闪存配置（nodemcuv2）。这块板报告的闪存是 {flash}")
        return
    print("结论: ESP8266，4MB 闪存，与 platformio.ini 里的 nodemcuv2 一致")


if __name__ == "__main__":
    try:
        main()
    except subprocess.TimeoutExpired:
        fail("开发板在超时内没有完成复位应答。检查数据线，或按住 FLASH 再点 RST 后重试")
