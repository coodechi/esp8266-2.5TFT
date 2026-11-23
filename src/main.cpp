#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "text.h"

TFT_eSPI tft;

// 一屏：上半是时钟和天气，进度环绕整屏，下半是打印状态。
static const int INSET = 24;
static const int CLOCK_Y = 30;
static const int DATE_Y = 94;
static const int WX_Y = 122;
static const int META_Y = 154;
static const int RULE_Y = 180;
static const int NAME_Y = 186;
static const int STAT_Y = 224;
static const int FOOT_Y = 264;

static WiFiClientSecure net;
static PubSubClient mqtt(net);

static char topicReport[64];
static char topicRequest[64];

static bool timeReady = false;
static int shownMinute = -1;
static int shownDay = -1;

static bool weatherOk = false;
static bool weatherFailed = false;
static int weatherTemp = 0;
static int weatherHumidity = 0;
static char weatherWind[12] = "";
static char weatherType[24] = "";
static uint32_t lastWeather = 0;

static char httpBuf[4600];
static WiFiClient httpNet;

struct Printer {
  bool seen = false;
  char state[16] = "IDLE";
  int percent = 0;
  int remainMin = 0;
  char task[96] = "";
  char file[96] = "";
  int nozzle = 0;
  int bed = 0;
  int layer = 0;
  int layers = 0;
};

static Printer printer;
static bool printerDirty = true;
static bool mqttUp = false;
static uint32_t lastPush = 0;
static uint32_t lastMqttTry = 0;
static uint32_t lastWifiTry = 0;

static const char *weekName(int wday) {
  static const char *names[] = {"日", "一", "二", "三", "四", "五", "六"};
  if (wday < 0 || wday > 6) {
    return "";
  }
  return names[wday];
}

enum WxKind { WX_SUN, WX_CLOUD, WX_OVER, WX_FOG, WX_RAIN, WX_SNOW, WX_STORM };

static WxKind weatherKind(const char *type) {
  if (!type || !type[0]) return WX_CLOUD;
  if (strstr(type, "雷")) return WX_STORM;
  if (strstr(type, "雪")) return WX_SNOW;
  if (strstr(type, "雨")) return WX_RAIN;
  if (strstr(type, "雾") || strstr(type, "霾")) return WX_FOG;
  if (strstr(type, "阴")) return WX_OVER;
  if (strstr(type, "云")) return WX_CLOUD;
  if (strstr(type, "晴")) return WX_SUN;
  return WX_CLOUD;
}

static bool sameText(const char *a, const char *b) { return strcmp(a, b) == 0; }

static const char *findKey(const char *json, size_t len, const char *key) {
  const size_t keyLen = strlen(key);
  for (size_t i = 0; i < len; i++) {
    if (json[i] != '"') {
      continue;
    }
    if (i + keyLen + 1 < len && memcmp(json + i + 1, key, keyLen) == 0 &&
        json[i + 1 + keyLen] == '"') {
      size_t pos = i + keyLen + 2;
      while (pos < len && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
      }
      if (pos < len && json[pos] == ':') {
        pos++;
        while (pos < len && (json[pos] == ' ' || json[pos] == '\t')) {
          pos++;
        }
        return json + pos;
      }
    }
    i++;
    while (i < len) {
      if (json[i] == '\\' && i + 1 < len) {
        i += 2;
        continue;
      }
      if (json[i] == '"') {
        break;
      }
      i++;
    }
  }
  return nullptr;
}

static int parseInt(const char *value) {
  return static_cast<int>(lroundf(strtof(value, nullptr)));
}

static int parseIntBounded(const char *value, const char *end) {
  char tmp[24];
  size_t n = 0;
  while (value < end && n + 1 < sizeof(tmp) &&
         (*value == '-' || *value == '+' || *value == '.' || (*value >= '0' && *value <= '9'))) {
    tmp[n++] = *value++;
  }
  tmp[n] = 0;
  if (n == 0) {
    return 0;
  }
  return static_cast<int>(lroundf(strtof(tmp, nullptr)));
}

static void parseString(const char *value, const char *end, char *out, size_t outLen) {
  if (value >= end || *value != '"') {
    out[0] = 0;
    return;
  }
  value++;
  size_t n = 0;
  while (value < end && *value != '"') {
    if (*value == '\\' && value + 1 < end) {
      value++;
    }
    if (n + 1 < outLen) {
      out[n++] = *value;
    }
    value++;
  }
  out[n] = 0;
}

static void copyIfPresent(const char *json, size_t len, const char *key, char *dest, size_t destLen,
                          bool &changed) {
  const char *value = findKey(json, len, key);
  if (!value) {
    return;
  }
  char next[96];
  parseString(value, json + len, next, sizeof(next));
  if (strncmp(dest, next, destLen) != 0) {
    strncpy(dest, next, destLen - 1);
    dest[destLen - 1] = 0;
    changed = true;
  }
}

static void intIfPresent(const char *json, size_t len, const char *key, int &dest, bool &changed) {
  const char *value = findKey(json, len, key);
  if (!value || !(*value == '-' || *value == '+' || (*value >= '0' && *value <= '9'))) {
    return;
  }
  const int next = parseIntBounded(value, json + len);
  if (next != dest) {
    dest = next;
    changed = true;
  }
}

static void onMqtt(char *topic, byte *payload, unsigned int length) {
  (void)topic;
  const char *json = reinterpret_cast<char *>(payload);
  bool changed = false;
  copyIfPresent(json, length, "gcode_state", printer.state, sizeof(printer.state), changed);
  copyIfPresent(json, length, "subtask_name", printer.task, sizeof(printer.task), changed);
  copyIfPresent(json, length, "gcode_file", printer.file, sizeof(printer.file), changed);
  intIfPresent(json, length, "mc_percent", printer.percent, changed);
  intIfPresent(json, length, "mc_remaining_time", printer.remainMin, changed);
  intIfPresent(json, length, "nozzle_temper", printer.nozzle, changed);
  intIfPresent(json, length, "bed_temper", printer.bed, changed);
  intIfPresent(json, length, "layer_num", printer.layer, changed);
  intIfPresent(json, length, "total_layer_num", printer.layers, changed);
  if (changed || !printer.seen) {
    printer.seen = true;
    printerDirty = true;
    Serial.printf("print %s %d%% layer %d/%d\n", printer.state, printer.percent, printer.layer,
                  printer.layers);
  }
}

static void fileBase(const char *path, char *out, size_t outLen) {
  const char *slash = strrchr(path, '/');
  const char *base = slash ? slash + 1 : path;
  strncpy(out, base, outLen - 1);
  out[outLen - 1] = 0;
}

static const char *jobName() {
  static char name[96];
  if (printer.task[0]) {
    return printer.task;
  }
  if (printer.file[0]) {
    fileBase(printer.file, name, sizeof(name));
    return name;
  }
  return "";
}

static void stroke(int x0, int y0, int x1, int y1, uint16_t color) {
  tft.drawWideLine(x0, y0, x1, y1, 1.5f, color, COL_PAPER);
}

static void iconSun(int cx, int cy) {
  tft.drawCircle(cx, cy, 5, COL_SKY);
  tft.drawCircle(cx, cy, 4, COL_SKY);
  for (int i = 0; i < 8; i++) {
    const float a = i * 0.78539816f;
    stroke(cx + (int)lroundf(cosf(a) * 8), cy + (int)lroundf(sinf(a) * 8),
           cx + (int)lroundf(cosf(a) * 12), cy + (int)lroundf(sinf(a) * 12), COL_SKY);
  }
}

static void iconCloud(int cx, int cy) {
  tft.drawCircle(cx - 5, cy + 2, 5, COL_SKY);
  tft.drawCircle(cx + 4, cy + 2, 6, COL_SKY);
  tft.drawCircle(cx, cy - 3, 5, COL_SKY);
}

static void iconOver(int cx, int cy) {
  iconCloud(cx, cy);
  stroke(cx - 8, cy + 9, cx + 9, cy + 9, COL_SKY);
}

static void iconFog(int cx, int cy) {
  stroke(cx - 9, cy - 4, cx + 9, cy - 4, COL_SKY);
  stroke(cx - 7, cy, cx + 7, cy, COL_SKY);
  stroke(cx - 9, cy + 4, cx + 9, cy + 4, COL_SKY);
}

static void iconRain(int cx, int cy) {
  iconCloud(cx, cy - 2);
  stroke(cx - 6, cy + 8, cx - 8, cy + 13, COL_SKY);
  stroke(cx, cy + 8, cx - 2, cy + 13, COL_SKY);
  stroke(cx + 6, cy + 8, cx + 4, cy + 13, COL_SKY);
}

static void iconSnow(int cx, int cy) {
  iconCloud(cx, cy - 3);
  stroke(cx - 5, cy + 7, cx - 5, cy + 13, COL_SKY);
  stroke(cx - 8, cy + 10, cx - 2, cy + 10, COL_SKY);
  stroke(cx + 4, cy + 7, cx + 4, cy + 13, COL_SKY);
  stroke(cx + 1, cy + 10, cx + 7, cy + 10, COL_SKY);
}

static void iconStorm(int cx, int cy) {
  iconCloud(cx, cy - 3);
  stroke(cx + 2, cy + 6, cx - 3, cy + 11, COL_SKY);
  stroke(cx - 3, cy + 11, cx + 2, cy + 11, COL_SKY);
  stroke(cx + 2, cy + 11, cx - 2, cy + 16, COL_SKY);
}

static void drawWeatherIcon(int cx, int cy, WxKind kind) {
  if (kind == WX_SUN) iconSun(cx, cy);
  else if (kind == WX_OVER) iconOver(cx, cy);
  else if (kind == WX_FOG) iconFog(cx, cy);
  else if (kind == WX_RAIN) iconRain(cx, cy);
  else if (kind == WX_SNOW) iconSnow(cx, cy);
  else if (kind == WX_STORM) iconStorm(cx, cy);
  else iconCloud(cx, cy);
}

static void iconPlay(int cx, int cy, uint16_t color) {
  tft.fillTriangle(cx - 5, cy - 7, cx - 5, cy + 7, cx + 7, cy, color);
}

static void iconPause(int cx, int cy, uint16_t color) {
  tft.fillRect(cx - 5, cy - 6, 3, 13, color);
  tft.fillRect(cx + 2, cy - 6, 3, 13, color);
}

static void iconCheck(int cx, int cy, uint16_t color) {
  stroke(cx - 7, cy, cx - 2, cy + 5, color);
  stroke(cx - 2, cy + 5, cx + 7, cy - 6, color);
}

static void iconCross(int cx, int cy, uint16_t color) {
  stroke(cx - 5, cy - 5, cx + 5, cy + 5, color);
  stroke(cx + 5, cy - 5, cx - 5, cy + 5, color);
}

static void iconIdle(int cx, int cy, uint16_t color) { tft.drawCircle(cx, cy, 6, color); }

static void iconOffline(int cx, int cy, uint16_t color) {
  tft.drawCircle(cx, cy, 6, color);
  stroke(cx - 4, cy + 4, cx + 4, cy - 4, color);
}

static void iconPrepare(int cx, int cy, uint16_t color) {
  tft.fillCircle(cx - 7, cy, 2, color);
  tft.fillCircle(cx, cy, 2, color);
  tft.drawCircle(cx + 7, cy, 2, color);
}

static void iconClock(int cx, int cy, uint16_t color) {
  tft.drawCircle(cx, cy, 6, color);
  stroke(cx, cy, cx, cy - 4, color);
  stroke(cx, cy, cx + 3, cy + 1, color);
}

static void iconNozzle(int cx, int cy, uint16_t color) {
  tft.drawRect(cx - 4, cy - 7, 8, 6, color);
  stroke(cx - 4, cy - 1, cx, cy + 6, color);
  stroke(cx + 4, cy - 1, cx, cy + 6, color);
}

static void iconBed(int cx, int cy, uint16_t color) {
  stroke(cx - 8, cy - 2, cx + 8, cy - 2, color);
  stroke(cx - 8, cy + 2, cx + 8, cy + 2, color);
  stroke(cx - 5, cy + 2, cx - 5, cy + 7, color);
  stroke(cx + 5, cy + 2, cx + 5, cy + 7, color);
}

static void iconLayers(int cx, int cy, uint16_t color) {
  stroke(cx, cy - 6, cx + 8, cy - 2, color);
  stroke(cx, cy - 6, cx - 8, cy - 2, color);
  stroke(cx - 8, cy - 2, cx, cy + 2, color);
  stroke(cx + 8, cy - 2, cx, cy + 2, color);
  stroke(cx - 8, cy + 1, cx, cy + 5, color);
  stroke(cx + 8, cy + 1, cx, cy + 5, color);
  stroke(cx - 8, cy + 1, cx + 8, cy + 1, color);
}

static void ringPoint(float s, int &ox, int &oy) {
  const float rad = 20.f;
  const float arc = 31.415927f;
  const float top = 82.f;
  const float side = 244.f;
  const float bottom = 164.f;
  if (s < top) {
    ox = 120 + (int)lroundf(s);
    oy = 18;
    return;
  }
  s -= top;
  if (s < arc) {
    const float a = (-90.f + 90.f * s / arc) * 0.0174532925f;
    ox = (int)lroundf(202.f + cosf(a) * rad);
    oy = (int)lroundf(38.f + sinf(a) * rad);
    return;
  }
  s -= arc;
  if (s < side) {
    ox = 222;
    oy = 38 + (int)lroundf(s);
    return;
  }
  s -= side;
  if (s < arc) {
    const float a = (90.f * s / arc) * 0.0174532925f;
    ox = (int)lroundf(202.f + cosf(a) * rad);
    oy = (int)lroundf(282.f + sinf(a) * rad);
    return;
  }
  s -= arc;
  if (s < bottom) {
    ox = 202 - (int)lroundf(s);
    oy = 302;
    return;
  }
  s -= bottom;
  if (s < arc) {
    const float a = (90.f + 90.f * s / arc) * 0.0174532925f;
    ox = (int)lroundf(38.f + cosf(a) * rad);
    oy = (int)lroundf(282.f + sinf(a) * rad);
    return;
  }
  s -= arc;
  if (s < side) {
    ox = 18;
    oy = 282 - (int)lroundf(s);
    return;
  }
  s -= side;
  if (s < arc) {
    const float a = (180.f + 90.f * s / arc) * 0.0174532925f;
    ox = (int)lroundf(38.f + cosf(a) * rad);
    oy = (int)lroundf(38.f + sinf(a) * rad);
    return;
  }
  s -= arc;
  ox = 38 + (int)lroundf(s);
  oy = 18;
}

static void drawRing(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  const int steps = 300;
  const int inkSteps = steps * percent / 100;
  const float length = 82.f * 2 + 244.f * 2 + 164.f + 31.415927f * 4;
  for (int i = 0; i < steps; i++) {
    int x = 0;
    int y = 0;
    ringPoint(length * i / steps, x, y);
    tft.fillCircle(x, y, 3, i < inkSteps ? COL_INK : COL_TRACK);
  }
}

static void drawClock(const tm &now, bool force) {
  const int minute = timeReady ? now.tm_min : -2;
  if (!force && minute == shownMinute) {
    return;
  }
  shownMinute = minute;
  char hourText[4];
  char minuteText[4];
  if (timeReady) {
    snprintf(hourText, sizeof(hourText), "%02d", now.tm_hour);
    snprintf(minuteText, sizeof(minuteText), "%02d", now.tm_min);
  } else {
    snprintf(hourText, sizeof(hourText), "--");
    snprintf(minuteText, sizeof(minuteText), "--");
  }
  const int hourWidth = tft.textWidth(hourText, 7);
  const int minuteWidth = tft.textWidth(minuteText, 7);
  const int gap = 16;
  const int total = hourWidth + gap + minuteWidth;
  const int x = (tft.width() - total) / 2;
  tft.fillRect(x, CLOCK_Y, total, 48, COL_PAPER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
  tft.setTextColor(COL_HOUR, COL_PAPER);
  tft.drawString(hourText, x, CLOCK_Y, 7);
  const int colonX = x + hourWidth + gap / 2;
  tft.fillCircle(colonX, CLOCK_Y + 16, 3, COL_INK);
  tft.fillCircle(colonX, CLOCK_Y + 32, 3, COL_INK);
  tft.setTextColor(COL_MIN, COL_PAPER);
  tft.drawString(minuteText, x + hourWidth + gap, CLOCK_Y, 7);
}

// 1900–2100 农历。低 4 位是闰月，bit4 起每个月 1 为 30 天，bit16 是闰月大小。
static const uint32_t LUNAR_INFO[] PROGMEM = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2, 0x04ae0,
    0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977, 0x04970, 0x0a4b0,
    0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, 0x06566, 0x0d4a0, 0x0ea50,
    0x06e95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950, 0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0,
    0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, 0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0,
    0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, 0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260,
    0x0f263, 0x0d950, 0x05b57, 0x056a0, 0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558,
    0x0b540, 0x0b6a0, 0x195a6, 0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46,
    0x0ab60, 0x09570, 0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x055c0, 0x0ab60, 0x096d5,
    0x092e0, 0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5,
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, 0x07954,
    0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, 0x05aa0, 0x076a3,
    0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, 0x0b5a0, 0x056d0, 0x055b2,
    0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, 0x14b63, 0x09370, 0x049f8, 0x04970,
    0x064b0, 0x168a6, 0x0ea50, 0x06b20, 0x1a6c4, 0x0aae0, 0x0a2e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0,
    0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4, 0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50,
    0x055a0, 0x0aba4, 0x0a5b0, 0x052b0, 0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60,
    0x0a570, 0x054e4, 0x0d160, 0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0,
    0x0d150, 0x0f252, 0x0d520};

static uint32_t lunarInfo(int year) { return pgm_read_dword(&LUNAR_INFO[year - 1900]); }

static bool solarLeap(int year) { return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0); }

static int daysAfter1900Jan31(int year, int month, int day) {
  static const int kDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = 0;
  for (int y = 1900; y < year; y++) {
    days += solarLeap(y) ? 366 : 365;
  }
  for (int m = 1; m < month; m++) {
    days += kDays[m] + (m == 2 && solarLeap(year) ? 1 : 0);
  }
  return days + day - 31;
}

static int lunarLeapMonth(int year) { return lunarInfo(year) & 0xf; }

static int lunarLeapDays(int year) {
  if (!lunarLeapMonth(year)) {
    return 0;
  }
  return (lunarInfo(year) & 0x10000) ? 30 : 29;
}

static int lunarYearDays(int year) {
  int days = 348;
  for (uint32_t bit = 0x8000; bit > 0x8; bit >>= 1) {
    if (lunarInfo(year) & bit) {
      days++;
    }
  }
  return days + lunarLeapDays(year);
}

static int lunarMonthDays(int year, int month) { return (lunarInfo(year) & (0x10000 >> month)) ? 30 : 29; }

static void lunarDayName(int day, char *out, size_t outLen) {
  static const char *kNum[] = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
  if (day <= 10) {
    snprintf(out, outLen, "初%s", kNum[day]);
  } else if (day < 20) {
    snprintf(out, outLen, "十%s", kNum[day - 10]);
  } else if (day == 20) {
    snprintf(out, outLen, "二十");
  } else if (day < 30) {
    snprintf(out, outLen, "廿%s", kNum[day - 20]);
  } else {
    snprintf(out, outLen, "三十");
  }
}

static bool lunarDate(int year, int month, int day, char *out, size_t outLen) {
  if (year < 1900 || year > 2100) {
    return false;
  }
  int offset = daysAfter1900Jan31(year, month, day);
  if (offset < 0) {
    return false;
  }
  int lunarYear = 1900;
  while (lunarYear <= 2100) {
    const int yearDays = lunarYearDays(lunarYear);
    if (offset < yearDays) {
      break;
    }
    offset -= yearDays;
    lunarYear++;
  }
  if (lunarYear > 2100) {
    return false;
  }
  const int leap = lunarLeapMonth(lunarYear);
  bool inLeap = false;
  int lunarMonth = 1;
  while (lunarMonth <= 12) {
    const int monthDays = lunarMonthDays(lunarYear, lunarMonth);
    if (offset < monthDays) {
      break;
    }
    offset -= monthDays;
    if (leap == lunarMonth) {
      const int leapDays = lunarLeapDays(lunarYear);
      if (offset < leapDays) {
        inLeap = true;
        break;
      }
      offset -= leapDays;
    }
    lunarMonth++;
  }
  if (lunarMonth < 1 || lunarMonth > 12) {
    return false;
  }
  static const char *kMonth[] = {"", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊"};
  char dayName[12];
  lunarDayName(offset + 1, dayName, sizeof(dayName));
  snprintf(out, outLen, "%s%s月%s", inLeap ? "闰" : "", kMonth[lunarMonth], dayName);
  return true;
}

static void drawDate(const tm &now) {
  char text[48];
  if (timeReady) {
    char lunar[24];
    const int year = now.tm_year + 1900;
    const int month = now.tm_mon + 1;
    if (lunarDate(year, month, now.tm_mday, lunar, sizeof(lunar))) {
      snprintf(text, sizeof(text), "%d/%d  %s  %s", month, now.tm_mday, weekName(now.tm_wday), lunar);
    } else {
      snprintf(text, sizeof(text), "%d/%d  %s", month, now.tm_mday, weekName(now.tm_wday));
    }
  } else {
    text[0] = 0;
  }
  drawTextLine(DATE_Y, text, COL_MUTED, true);
  shownDay = timeReady ? now.tm_yday : -1;
}

static void drawRule() {
  tft.drawFastHLine(72, RULE_Y, 96, COL_TRACK);
}

static void drawWeather() {
  tft.fillRect(26, WX_Y - 4, tft.width() - 52, META_Y + 18 - (WX_Y - 4), COL_PAPER);
  const int cityWidth = measureText(CITY_NAME);
  if (!weatherOk) {
    if (cityWidth > 0) {
      drawText((tft.width() - cityWidth) / 2, WX_Y + 8, CITY_NAME, COL_INK, tft.width() - 12);
    }
    return;
  }

  char temp[8];
  char humid[8];
  snprintf(temp, sizeof(temp), "%d", weatherTemp);
  snprintf(humid, sizeof(humid), "%d%%", weatherHumidity);
  const int iconSlot = 20;
  const int gap = 6;
  const int tempWidth = tft.textWidth(temp, 4);
  const int degreeWidth = measureText("°");
  int total = iconSlot + gap + tempWidth + 2 + degreeWidth;
  const bool showCity = cityWidth > 0 && total + gap + cityWidth <= 216;
  if (showCity) {
    total += gap + cityWidth;
  }
  int x = (tft.width() - total) / 2;
  if (x < 8) {
    x = 8;
  }
  drawWeatherIcon(x + iconSlot / 2, WX_Y + 13, weatherKind(weatherType));
  x += iconSlot + gap;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_INK, COL_PAPER);
  tft.setTextPadding(0);
  tft.drawString(temp, x, WX_Y, 4);
  x += tempWidth + 2;
  drawText(x, WX_Y + 8, "°", COL_MUTED, tft.width() - 8);
  if (showCity) {
    drawText(x + degreeWidth + gap, WX_Y + 8, CITY_NAME, COL_MUTED, tft.width() - 8);
  }

  const int humidWidth = measureText(humid);
  const int windWidth = weatherWind[0] ? measureText(weatherWind) : 0;
  int meta = humidWidth;
  if (windWidth > 0) {
    meta += 16 + windWidth;
  }
  int mx = (tft.width() - meta) / 2;
  if (mx < 8) {
    mx = 8;
  }
  drawText(mx, META_Y, humid, COL_INK, tft.width() - 8);
  if (windWidth > 0) {
    drawText(mx + humidWidth + 16, META_Y, weatherWind, COL_MUTED, tft.width() - 8);
  }
}

static void drawStateIcon(int cx, int cy) {
  if (BAMBU_HOST[0] == 0 || BAMBU_SERIAL[0] == 0) {
    iconIdle(cx, cy, COL_YEL);
    tft.fillCircle(cx, cy, 2, COL_YEL);
    return;
  }
  if (!mqttUp || !printer.seen) {
    iconOffline(cx, cy, COL_YEL);
    return;
  }
  if (sameText(printer.state, "RUNNING")) {
    iconPlay(cx, cy, COL_YEL);
  } else if (sameText(printer.state, "PAUSE")) {
    iconPause(cx, cy, COL_YEL);
  } else if (sameText(printer.state, "FINISH")) {
    iconCheck(cx, cy, COL_YEL);
  } else if (sameText(printer.state, "FAILED")) {
    iconCross(cx, cy, COL_YEL);
  } else if (sameText(printer.state, "PREPARE") || sameText(printer.state, "PREPARING")) {
    iconPrepare(cx, cy, COL_YEL);
  } else {
    iconIdle(cx, cy, COL_YEL);
  }
}

static void drawStat(int cx, int y, void (*icon)(int, int, uint16_t), uint16_t color, const char *num) {
  icon(cx, y, color);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(COL_INK, COL_PAPER);
  tft.setTextPadding(52);
  tft.drawString(num, cx, y + 14, 2);
  tft.setTextPadding(0);
}

static void drawFoot(int fill, const char *layers) {
  char num[8];
  snprintf(num, sizeof(num), "%d", fill);
  const int numberWidth = tft.textWidth(num, 4);
  const int unitWidth = tft.textWidth("%", 2);
  const int leftWidth = 28 + numberWidth + 3 + unitWidth;
  const int textWidth = measureText(layers);
  const int rightWidth = 24 + textWidth;
  const int gap = 12;
  int left = (tft.width() - (leftWidth + gap + rightWidth)) / 2;
  if (left < 28) {
    left = 28;
  }
  int x = left;
  drawStateIcon(x + 11, FOOT_Y + 13);
  x += 28;
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_INK, COL_PAPER);
  tft.setTextPadding(0);
  tft.drawString(num, x, FOOT_Y, 4);
  tft.setTextColor(COL_MUTED, COL_PAPER);
  tft.drawString("%", x + numberWidth + 3, FOOT_Y + 10, 2);

  const int rx = left + leftWidth + gap;
  iconLayers(rx + 9, FOOT_Y + 13, COL_PUR);
  drawText(rx + 24, FOOT_Y + 5, layers, COL_INK, tft.width() - 26);
}

static void drawPrinter() {
  tft.fillRect(INSET, RULE_Y + 4, tft.width() - INSET * 2, 292 - (RULE_Y + 4), COL_PAPER);

  int fill = printer.seen ? printer.percent : 0;
  if (fill < 0) fill = 0;
  if (fill > 100) fill = 100;
  const char *name = jobName();
  drawTextLine(NAME_Y, name, COL_MUTED, true);

  char remain[16];
  char nozzle[8];
  char bed[8];
  char layers[16];
  const bool active = mqttUp && printer.seen &&
                      (sameText(printer.state, "RUNNING") || sameText(printer.state, "PAUSE") ||
                       sameText(printer.state, "PREPARE") || sameText(printer.state, "PREPARING"));
  if (active) {
    snprintf(remain, sizeof(remain), "%d:%02d", printer.remainMin / 60, printer.remainMin % 60);
  } else {
    snprintf(remain, sizeof(remain), "--");
  }
  if (printer.seen) {
    snprintf(nozzle, sizeof(nozzle), "%d", printer.nozzle);
    snprintf(bed, sizeof(bed), "%d", printer.bed);
  } else {
    snprintf(nozzle, sizeof(nozzle), "--");
    snprintf(bed, sizeof(bed), "--");
  }
  if (printer.seen && printer.layers > 0) {
    snprintf(layers, sizeof(layers), "%d/%d", printer.layer, printer.layers);
  } else {
    snprintf(layers, sizeof(layers), "--");
  }
  drawStat(60, STAT_Y, iconClock, COL_GRN, remain);
  drawStat(120, STAT_Y, iconNozzle, COL_CYN, nozzle);
  drawStat(180, STAT_Y, iconBed, COL_BLU, bed);
  drawFoot(fill, layers);
  drawRing(fill);
  printerDirty = false;
}

static void drawFace(const tm &now) {
  tft.fillScreen(COL_PAPER);
  shownMinute = -1;
  shownDay = -1;
  drawClock(now, true);
  drawDate(now);
  drawWeather();
  drawRule();
  drawPrinter();
}

static bool waitWifi(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (!WiFi.isConnected() && millis() - start < timeoutMs) {
    delay(200);
    yield();
  }
  return WiFi.isConnected();
}

static bool syncTime() {
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp.tencent.com", "pool.ntp.org");
  for (int i = 0; i < 40; i++) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      timeReady = true;
      Serial.printf("time %ld\n", static_cast<long>(now));
      return true;
    }
    delay(250);
    yield();
  }
  Serial.println("time sync failed");
  return false;
}

static bool copyJsonString(const char *json, size_t len, const char *key, char *out, size_t outLen) {
  const char *value = findKey(json, len, key);
  if (!value || (size_t)(value - json) >= len || *value != '"') {
    return false;
  }
  value++;
  size_t n = 0;
  while ((size_t)(value - json) < len && *value != '"' && n + 1 < outLen) {
    out[n++] = *value++;
  }
  out[n] = 0;
  return n > 0;
}

static bool fetchWeather() {
  if (WEATHER_CITY[0] == 0) {
    return false;
  }
  if (mqtt.connected()) {
    mqtt.disconnect();
  }
  net.stop();
  httpNet.stop();
  Serial.printf("weather heap %u\n", ESP.getFreeHeap());

  char url[96];
  snprintf(url, sizeof(url), "http://t.weather.itboy.net/api/weather/city/%s", WEATHER_CITY);

  HTTPClient http;
  http.setTimeout(12000);
  http.setReuse(false);
  if (!http.begin(httpNet, url)) {
    Serial.println("weather begin failed");
    weatherFailed = true;
    httpNet.stop();
    return false;
  }
  const int code = http.GET();
  if (code != 200) {
    Serial.printf("weather http %d\n", code);
    http.end();
    httpNet.stop();
    weatherFailed = true;
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t n = 0;
  const uint32_t start = millis();
  while (n + 1 < sizeof(httpBuf) && millis() - start < 12000) {
    if (!stream->available()) {
      if (!http.connected()) {
        break;
      }
      delay(1);
      yield();
      continue;
    }
    httpBuf[n++] = stream->read();
  }
  httpBuf[n] = 0;
  http.end();
  httpNet.stop();

  char temp[12];
  char humid[12];
  if (!copyJsonString(httpBuf, n, "wendu", temp, sizeof(temp)) ||
      !copyJsonString(httpBuf, n, "shidu", humid, sizeof(humid))) {
    Serial.println("weather parse failed");
    weatherFailed = true;
    return false;
  }
  weatherTemp = parseInt(temp);
  weatherHumidity = parseInt(humid);
  if (!copyJsonString(httpBuf, n, "fl", weatherWind, sizeof(weatherWind))) {
    weatherWind[0] = 0;
  }
  if (!copyJsonString(httpBuf, n, "type", weatherType, sizeof(weatherType))) {
    weatherType[0] = 0;
  }
  weatherFailed = false;
  weatherOk = true;
  Serial.printf("weather %dC %d%% %s %s\n", weatherTemp, weatherHumidity, weatherWind, weatherType);
  return true;
}

static void requestPush() {
  char body[128];
  snprintf(body, sizeof(body),
           "{\"pushing\":{\"sequence_id\":\"%lu\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}",
           millis());
  mqtt.publish(topicRequest, body);
}

static void connectMqtt() {
  if (BAMBU_HOST[0] == 0 || BAMBU_SERIAL[0] == 0) {
    return;
  }
  if (!mqtt.setBufferSize(8192)) {
    Serial.printf("mqtt buffer alloc failed heap %u\n", ESP.getFreeHeap());
  }
  net.stop();
  net.setInsecure();
  net.setBufferSizes(6144, 1024);
  net.setTimeout(5000);
  mqtt.setServer(BAMBU_HOST, 8883);
  mqtt.setCallback(onMqtt);
  mqtt.setSocketTimeout(3);

  char clientId[24];
  snprintf(clientId, sizeof(clientId), "sd2-%06x", ESP.getChipId());
  Serial.printf("mqtt connect %s heap %u\n", BAMBU_HOST, ESP.getFreeHeap());
  if (!mqtt.connect(clientId, "bblp", BAMBU_CODE)) {
    mqttUp = false;
    printerDirty = true;
    Serial.printf("mqtt failed rc %d\n", mqtt.state());
    return;
  }
  mqtt.subscribe(topicReport);
  requestPush();
  lastPush = millis();
  mqttUp = true;
  printerDirty = true;
  Serial.println("mqtt connected");
}

static void serviceWifi() {
  if (WIFI_SSID[0] == 0 || WiFi.isConnected()) {
    return;
  }
  if (millis() - lastWifiTry < 10000) {
    return;
  }
  lastWifiTry = millis();
  Serial.println("wifi reconnect");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  waitWifi(8000);
  if (WiFi.isConnected()) {
    Serial.printf("wifi %s\n", WiFi.localIP().toString().c_str());
    if (!timeReady) {
      syncTime();
    }
    printerDirty = true;
  }
}

static void serviceMqtt() {
  if (BAMBU_HOST[0] == 0 || BAMBU_SERIAL[0] == 0 || !WiFi.isConnected()) {
    if (mqttUp) {
      mqttUp = false;
      printerDirty = true;
    }
    return;
  }
  if (!mqtt.connected()) {
    mqttUp = false;
    if (millis() - lastMqttTry < 8000) {
      return;
    }
    lastMqttTry = millis();
    connectMqtt();
    return;
  }
  mqtt.loop();
  if (millis() - lastPush > 300000UL) {
    requestPush();
    lastPush = millis();
  }
}

static void pollSerial() {
  static char line[24];
  static size_t n = 0;
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == '\n' || c == '\r') {
      line[n] = 0;
      if (strcmp(line, "info") == 0) {
        Serial.printf("heap %u wifi %d rssi %d mqtt %d state %s %d%%\n", ESP.getFreeHeap(),
                      WiFi.isConnected() ? 1 : 0, WiFi.RSSI(), mqtt.connected() ? 1 : 0, printer.state,
                      printer.percent);
      }
      n = 0;
    } else if (n + 1 < sizeof(line)) {
      line[n++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("SD2 boot");
  Serial.printf("heap %u\n", ESP.getFreeHeap());

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(COL_PAPER);
  tft.setTextWrap(false);

  snprintf(topicReport, sizeof(topicReport), "device/%s/report", BAMBU_SERIAL);
  snprintf(topicRequest, sizeof(topicRequest), "device/%s/request", BAMBU_SERIAL);

  tm now = {};
  drawFace(now);

  if (WIFI_SSID[0] == 0) {
    Serial.println("WIFI_SSID is empty");
    return;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("wifi connecting");
  if (!waitWifi(20000)) {
    Serial.println("wifi timeout");
    return;
  }
  Serial.printf("wifi %s rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  syncTime();
  time_t synced = time(nullptr);
  localtime_r(&synced, &now);
  drawClock(now, true);
  drawDate(now);

  fetchWeather();
  lastWeather = millis();
  drawWeather();
  connectMqtt();
}

void loop() {
  pollSerial();
  serviceWifi();

  tm now = {};
  time_t seconds = time(nullptr);
  localtime_r(&seconds, &now);
  if (seconds > 1700000000) {
    timeReady = true;
  }

  drawClock(now, false);
  if (timeReady && now.tm_yday != shownDay) {
    drawDate(now);
  }

  const uint32_t weatherWait = weatherOk ? 900000UL : 30000UL;
  const bool weatherDue = WiFi.isConnected() && WEATHER_CITY[0] != 0 &&
                          (lastWeather == 0 || millis() - lastWeather > weatherWait);
  if (weatherDue) {
    const bool mqttWasUp = mqtt.connected();
    if (mqttWasUp) {
      mqtt.disconnect();
      net.stop();
    }
    fetchWeather();
    lastWeather = millis();
    drawWeather();
    if (mqttWasUp) {
      connectMqtt();
    }
  }

  serviceMqtt();
  if (printerDirty) {
    drawPrinter();
  }
  delay(20);
}
