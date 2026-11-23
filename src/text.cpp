#include "text.h"

#include <TFT_eSPI.h>
#include <pgmspace.h>

extern TFT_eSPI tft;

extern const uint16_t ZH_COUNT;
extern const uint16_t ZH_CP[] PROGMEM;
extern const uint8_t ZH_BMP[] PROGMEM;

static uint16_t nextCode(const char *&cursor) {
  const uint8_t c = static_cast<uint8_t>(*cursor);
  if (c == 0) {
    return 0;
  }
  if (c < 0x80) {
    cursor++;
    return c;
  }
  if ((c & 0xE0) == 0xC0 && cursor[1]) {
    uint16_t cp = (c & 0x1F) << 6;
    cp |= static_cast<uint8_t>(cursor[1]) & 0x3F;
    cursor += 2;
    return cp;
  }
  if ((c & 0xF0) == 0xE0 && cursor[1] && cursor[2]) {
    uint16_t cp = (c & 0x0F) << 12;
    cp |= (static_cast<uint8_t>(cursor[1]) & 0x3F) << 6;
    cp |= static_cast<uint8_t>(cursor[2]) & 0x3F;
    cursor += 3;
    return cp;
  }
  cursor++;
  return '?';
}

static int findGlyph(uint16_t code) {
  int lo = 0;
  int hi = static_cast<int>(ZH_COUNT) - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) >> 1;
    const uint16_t value = pgm_read_word(&ZH_CP[mid]);
    if (value == code) {
      return mid;
    }
    if (value < code) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return -1;
}

static int asciiWidth(char ascii) {
  char text[2] = {ascii, 0};
  return tft.textWidth(text, 2);
}

static void blitGlyph(int x, int y, int index, uint16_t color) {
  const uint8_t *base = ZH_BMP + index * 32;
  tft.startWrite();
  for (int row = 0; row < 16; row++) {
    const uint16_t bits = (pgm_read_byte(base + row * 2) << 8) | pgm_read_byte(base + row * 2 + 1);
    if (!bits) {
      continue;
    }
    for (int col = 0; col < 16; col++) {
      if (bits & (0x8000 >> col)) {
        tft.drawPixel(x + col, y + row, color);
      }
    }
  }
  tft.endWrite();
}

int measureText(const char *text) {
  int width = 0;
  const char *cursor = text;
  while (uint16_t code = nextCode(cursor)) {
    if (code < 128) {
      width += asciiWidth(static_cast<char>(code));
    } else if (findGlyph(code) >= 0) {
      width += 16;
    } else {
      width += 8;
    }
  }
  return width;
}

void drawText(int x, int y, const char *text, uint16_t color, int max_x) {
  const char *cursor = text;
  tft.setTextColor(color);
  while (uint16_t code = nextCode(cursor)) {
    if (code < 128) {
      const int width = asciiWidth(static_cast<char>(code));
      if (x + width > max_x) {
        return;
      }
      tft.drawChar(code, x, y, 2);
      x += width;
      continue;
    }
    const int index = findGlyph(code);
    const int width = index >= 0 ? 16 : 8;
    if (x + width > max_x) {
      return;
    }
    if (index >= 0) {
      blitGlyph(x, y, index, color);
    } else {
      tft.drawRect(x + 1, y + 1, 6, 14, color);
    }
    x += width;
  }
}

void drawTextLine(int y, const char *text, uint16_t color, bool center) {
  const int x0 = 24;
  const int x1 = tft.width() - 24;
  tft.fillRect(x0, y, x1 - x0, 16, COL_PAPER);
  int x = x0;
  if (center) {
    x = (tft.width() - measureText(text)) / 2;
    if (x < x0) {
      x = x0;
    }
  }
  drawText(x, y, text, color, x1);
}
