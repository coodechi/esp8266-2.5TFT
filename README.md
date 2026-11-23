# 开发流程

命令都在这个目录里执行。工具装在 `.venv`，不需要 Arduino IDE。

屏幕是 2.4 寸 ST7789V，竖放，排针在上沿，丝印 1 是 GND。开发板是 NodeMCU 一类 ESP8266。屏幕 VCC 和 BL 接开发板 **3V3**。VIN 是 5 V，接到屏幕上会损坏模块。

| 屏幕 | 信号 | NodeMCU | GPIO |
|---|---|---|---|
| 1 GND | 地 | GND | — |
| 2 VCC | 3 V | 3V3 | — |
| 3 SCL | SPI 时钟 | D5 | 14 |
| 4 SDI | SPI 数据 | D7 | 13 |
| 5 RST | 复位 | D0 | 16 |
| 6 DC | 数据/命令 | D1 | 5 |
| 7 CS | 片选 | D2 | 4 |
| 8 BL | 背光，常亮 | 3V3 | — |

这块模组的排针只有上面 8 个脚，没有触摸信号。时钟、天气和打印进度画在同一屏上。上电时按住 FLASH 会进入下载模式；壳装上之后按键在里面，进下载模式要先打开尾盖。

## 1. 安装工具

只需做一次。会创建 `.venv`，并安装 PlatformIO 和 esptool。

```bash
make setup
```

## 2. 识别主板

用能传数据的 Micro-B 线把开发板插上电脑，然后：

```bash
make detect
```

脚本会跳过蓝牙和调试口，对剩下的串口做芯片应答。成功时能看到 USB 芯片（常见是 CH340 或 CP2102）、`ESP8266EX`、26 MHz 晶振、MAC 和闪存大小。4MB 闪存会写明与 `platformio.ini` 里的 `nodemcuv2` 一致，并把端口写入 `.board-port`。

没有串口：换一根数据线。有串口但芯片不应答：按住板上的 FLASH，点一下 RST，松开 FLASH，再运行一次 `make detect`。同时插着多块 ESP8266 时，只留一块再识别。

## 3. 填写配置

编译和烧录之前，先把模板复制成 `.env`，并按下面的表把每一项补全。没有 `.env` 时，`make build` 和 `make upload` 会停下来，不会开始编译。

```bash
cp .env.example .env
```

补全后，`make build` 和 `make upload` 只从 `.env` 读取这些值并编进固件。项目里不再放第二份配置。`.env` 不提交。以后改配置也是改 `.env`，改完重新烧录。屏幕引脚在 `platformio.ini` 的 `TFT_*`。

| 配置 | 作用 |
|---|---|
| `WIFI_SSID` / `WIFI_PASS` | 2.4 GHz 路由器 |
| `CITY_NAME` / `WEATHER_CITY` | 屏幕上的城市名，以及中国天气网城市代码 |
| `BAMBU_HOST` | 打印机的局域网 IP |
| `BAMBU_SERIAL` | 打印机序列号 |
| `BAMBU_CODE` | 局域网访问码 |

在打印机的网络设置里打开局域网模式，就能看到 IP、序列号和访问码。开发板和打印机要在同一个局域网。拓竹这一栏留空时，进度环停在起点，时间下面是一个带点的圆。时钟和天气仍会工作。

先确认能编译：

```bash
make build
```

## 4. 烧录

```bash
make upload
```

使用 `.board-port` 里上次识别到的端口，编译后写入闪存。开发板会自动进入下载模式。

停在 `Invalid head of packet` 或 `Timed out waiting for packet header` 时，确认 `platformio.ini` 里的 `upload_speed` 是 `115200`，再烧一次。仍失败就按住 FLASH，点一下 RST，松开 FLASH，立刻再执行 `make upload`。

换了一块板或重新插拔后端口变了，先再跑 `make detect`。

## 5. 调试

烧录并打开串口：

```bash
make debug
```

已经烧过、只想看输出：

```bash
make monitor
```

退出监视器用 `Ctrl-C`。

上电后串口会出现 `SD2 boot`、`wifi ...`、`time ...`、`weather ...`。拓竹连上后还有 `mqtt connected` 和 `print RUNNING 42%` 这样的进度。监视器里输入 `info` 可以看剩余内存、信号和当前进度。

画面旋转了 180 度。奶油纸上，小时是红色，分钟是蓝色。天气来自国内接口。第一行是图标、温度和城市，下一行单独放湿度和风力。进度环绕着整屏，从正上方顺时针走。环里是文件名、剩余时间、喷头和热床。最下面一行左边是状态图标和进度百分比，右边是层数。这五个图标依次是黄、绿、青、蓝、紫。电脑上打开 `preview.html`，看的是同一屏。

天气大约 15 分钟更新一次。打印机状态有变化就刷新；另外每 5 分钟向打印机要一次完整状态。P1 系列不适合把这个间隔再缩短。

进度环不走、环里是一个划掉的圆时，核对访问码、序列号和 IP，并确认打印机与开发板在同一个 2.4 GHz 局域网。串口里 `mqtt failed rc 5` 表示访问码被拒绝。

屏幕不亮：把 BL 改接到 GND 试一次，有的模块背光是低电平有效。颜色发反时，在 `build_flags` 增加 `-D TFT_INVERSION_ON=1`。红蓝对调时增加 `-D TFT_RGB_ORDER=TFT_BGR`。想把画面再转回来时，把 `src/main.cpp` 里的 `tft.setRotation(2)` 改成 `0`。改完重新 `make upload`。

程序崩溃时，监视器会用 `esp8266_exception_decoder` 把异常栈解码到函数名。

## 常用命令

开始编译或烧录前，先执行 `cp .env.example .env`，把模板里的 WiFi、城市和拓竹信息补全。

```bash
make            # 列出步骤
make setup      # 安装工具
make detect     # 识别主板
make build      # 只编译
make upload     # 烧录
make monitor    # 串口调试
make debug      # 烧录后马上调试
make clean      # 清掉编译结果
```
# esp8266-2.5TFT
