#pragma once

#include <stdint.h>

// 奶油纸、深褐字、陶土天气、青蓝机器。RGB565。
constexpr uint16_t COL_PAPER = 0xF75A;
constexpr uint16_t COL_INK = 0x3923;
constexpr uint16_t COL_MUTED = 0xA42C;
constexpr uint16_t COL_TRACK = 0xE697;
constexpr uint16_t COL_SKY = 0xD329;
constexpr uint16_t COL_ICON = 0x2412;
constexpr uint16_t COL_HOUR = 0xE0E4;
constexpr uint16_t COL_MIN = 0x1B7A;
constexpr uint16_t COL_YEL = 0xFE20;
constexpr uint16_t COL_GRN = 0x262B;
constexpr uint16_t COL_CYN = 0x065C;
constexpr uint16_t COL_BLU = 0x2B5F;
constexpr uint16_t COL_PUR = 0x8ADE;

// 16 像素高。ASCII 用 TFT 字库 2，汉字用 font16 点阵。超出 max_x 的部分不画。
int measureText(const char *text);
void drawText(int x, int y, const char *text, uint16_t color, int max_x);
void drawTextLine(int y, const char *text, uint16_t color, bool center);
