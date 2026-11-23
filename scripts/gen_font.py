#!/usr/bin/env python3
"""把 GB2312 一级汉字渲成 16×16 点阵，写到 src/font16.cpp。"""

from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src" / "font16.cpp"
FONT_PATH = "/System/Library/Fonts/STHeiti Light.ttc"
CELL = 16

REQUIRED = (
    "月日周一二三四五六天晴少云阴雾毛雨中大小雪阵雷未知"
    "拓竹打印空闲暂停完成失败准备连接剩余时分喷头热床层"
    "正在获取天气不可用未配置网络填写请的同步"
)


def gb2312_chars() -> list[str]:
    chars: list[str] = []
    for hi in range(0xB0, 0xD8):
        last = 0xF9 if hi == 0xD7 else 0xFE
        for lo in range(0xA1, last + 1):
            try:
                chars.append(bytes([hi, lo]).decode("gb2312"))
            except UnicodeDecodeError:
                continue
    return chars


def render(font: ImageFont.FreeTypeFont, ch: str) -> bytes:
    image = Image.new("L", (CELL, CELL), 0)
    draw = ImageDraw.Draw(image)
    bbox = font.getbbox(ch)
    width = bbox[2] - bbox[0]
    height = bbox[3] - bbox[1]
    x = (CELL - width) // 2 - bbox[0]
    y = (CELL - height) // 2 - bbox[1] + 1
    draw.text((x, y), ch, font=font, fill=255)
    packed = bytearray()
    for row in range(CELL):
        word = 0
        for col in range(CELL):
            if image.getpixel((col, row)) > 96:
                word |= 1 << (15 - col)
        packed.append((word >> 8) & 0xFF)
        packed.append(word & 0xFF)
    return bytes(packed)


def main() -> None:
    font = ImageFont.truetype(FONT_PATH, 15)
    glyphs: dict[int, bytes] = {}
    for ch in gb2312_chars():
        glyphs[ord(ch)] = render(font, ch)
    glyphs[0x00B0] = render(font, "°")

    missing = [ch for ch in REQUIRED if ord(ch) not in glyphs]
    if missing:
        raise SystemExit("字库缺少: " + "".join(missing))

    items = sorted(glyphs.items())
    lines = [
        "// 由 scripts/gen_font.py 生成。16×16 点阵，行优先，高位在左。",
        "#include <Arduino.h>",
        "",
        f"extern const uint16_t ZH_COUNT = {len(items)};",
        "extern const uint16_t ZH_CP[] PROGMEM = {",
    ]
    codes = [f"0x{cp:04X}" for cp, _ in items]
    for i in range(0, len(codes), 16):
        lines.append("  " + ", ".join(codes[i : i + 16]) + ",")
    lines.append("};")
    lines.append("extern const uint8_t ZH_BMP[] PROGMEM = {")
    blob = b"".join(bmp for _, bmp in items)
    for i in range(0, len(blob), 24):
        chunk = blob[i : i + 24]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    lines.append("")
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"写入 {OUT}  字形 {len(items)}  点阵 {len(blob)} 字节")


if __name__ == "__main__":
    main()
