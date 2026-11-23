VENV := .venv
PY := $(VENV)/bin/python
PIO := $(VENV)/bin/pio
PORT_FILE := .board-port

export PYTHONWARNINGS := ignore::NotOpenSSLWarning

.DEFAULT_GOAL := help

.PHONY: help setup detect build upload monitor debug clean

help:
	@echo "1. make setup     安装命令行工具（只需一次）"
	@echo "2. make detect    识别插上的 ESP8266，记下串口"
	@echo "3. 复制 .env.example 为 .env，填写 WiFi、城市、拓竹"
	@echo "4. make build     只编译"
	@echo "5. make upload    按上次识别的串口烧录"
	@echo "6. make monitor   打开串口调试"
	@echo "   make debug     烧录并马上进入串口调试"

setup:
	python3 -m venv $(VENV)
	$(PY) -m pip install -U pip
	$(PY) -m pip install -r requirements.txt

detect:
	$(PY) scripts/detect.py

build:
	$(PIO) run

upload:
	@test -s $(PORT_FILE) || { echo "还没有识别到主板。先插上开发板，运行 make detect"; exit 1; }
	$(PIO) run -t upload --upload-port "$$(cat $(PORT_FILE))"

monitor:
	@test -s $(PORT_FILE) || { echo "还没有识别到主板。先运行 make detect"; exit 1; }
	$(PIO) device monitor --port "$$(cat $(PORT_FILE))" --baud 115200

debug: upload
	$(PIO) device monitor --port "$$(cat $(PORT_FILE))" --baud 115200

clean:
	$(PIO) run -t clean
