// ============================================================================
//  oyobidashi / battery-test
//  M5Dial をモバイルバッテリーで動かし続け、途中で給電が切れないかを確かめる
//  ためのテスト用ファームウェア。
//
//  画面
//    中央   : 起動してからの経過時間 (UPTIME)
//    外周   : 秒の進み。動いていれば止まらずに回り続ける
//    LAST   : 前回の起動で動いていた時間（1分ごとに本体へ保存）
//    下段   : 画面の明るさ / Wi-Fi の状態 / 起動回数
//
//  操作
//    ダイヤルを回す    : 画面の明るさを変える（0% にすると最も低電流）
//    画面を短く押す    : Wi-Fi のオン/オフ（include/secrets.h がある場合）
//    画面を2秒長押し   : LAST の記録と起動回数を消す
// ============================================================================

#include <M5Dial.h>
#include <Preferences.h>
#include <WiFi.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif

namespace {

constexpr int W = 240;
constexpr int H = 240;
constexpr int CX = W / 2;
constexpr int CY = H / 2;

constexpr uint32_t SAVE_INTERVAL_MS = 60 * 1000;  // 経過時間を保存する間隔
constexpr uint32_t DRAW_INTERVAL_MS = 200;
constexpr uint32_t LOG_INTERVAL_MS = 60 * 1000;
constexpr uint32_t BRI_SAVE_DELAY_MS = 2000;      // 明るさ変更から保存までの待ち
constexpr int BRI_STEP = 4;                       // ダイヤル1カウントあたり (0-255)

// --- colors (RGB565) -------------------------------------------------------
constexpr uint16_t C_BG = 0x0000;
constexpr uint16_t C_FG = 0xFFFF;
constexpr uint16_t C_SUB = 0x8C71;     // #8C8C8C
constexpr uint16_t C_TRACK = 0x2124;   // #262626
constexpr uint16_t C_ACCENT = 0xFAC3;  // #FF5A1F

M5Canvas canvas(&M5Dial.Display);
Preferences prefs;

uint32_t lastRunSec = 0;  // 前回の起動で動いていた秒数
uint32_t bootCount = 0;
int brightness = 128;
bool wifiWanted = false;
bool hasWifiConfig = false;

long lastEnc = 0;
uint32_t lastSaveMs = 0;
uint32_t lastDrawMs = 0;
uint32_t lastLogMs = 0;
uint32_t briChangedMs = 0;
bool briDirty = false;
uint32_t clearedUntilMs = 0;

String fmtHMS(uint32_t sec) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)(sec / 3600),
           (unsigned long)((sec / 60) % 60), (unsigned long)(sec % 60));
  return String(buf);
}

String fmtHM(uint32_t sec) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(sec / 3600),
           (unsigned long)((sec / 60) % 60));
  return String(buf);
}

void startWifi() {
  if (!hasWifiConfig) return;
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiWanted = true;
}

void stopWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  wifiWanted = false;
}

String wifiLabel() {
  if (!hasWifiConfig) return "WiFi --";
  if (!wifiWanted) return "WiFi OFF";
  if (WiFi.status() == WL_CONNECTED) return "WiFi " + String(WiFi.RSSI());
  return "WiFi ...";
}

// 12時の位置から時計回りに span 度ぶんの弧を描く
void drawProgressArc(int r0, int r1, float span, uint16_t color) {
  if (span <= 0) return;
  if (span > 360) span = 360;
  // LovyanGFX の角度は 3時 = 0度・時計回り。12時 = 270度。
  if (span <= 90) {
    canvas.fillArc(CX, CY, r0, r1, 270, 270 + span, color);
  } else {
    canvas.fillArc(CX, CY, r0, r1, 270, 360, color);
    canvas.fillArc(CX, CY, r0, r1, 0, span - 90, color);
  }
}

void draw(uint32_t now) {
  const uint32_t up = now / 1000;

  canvas.fillSprite(C_BG);

  // 外周リング：秒の進み
  canvas.fillArc(CX, CY, 119, 114, 0, 360, C_TRACK);
  drawProgressArc(119, 114, (up % 60) * 6.0f + 6.0f, C_ACCENT);

  canvas.setTextDatum(middle_center);

  // タイトル
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  canvas.setTextColor(C_ACCENT);
  canvas.drawString("BATTERY TEST", CX, 56);

  // 経過時間
  canvas.setFont(&fonts::FreeSansBold24pt7b);
  canvas.setTextColor(C_FG);
  canvas.drawString(fmtHMS(up), CX, 106);

  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(C_SUB);
  canvas.drawString("UPTIME", CX, 140);

  canvas.drawFastHLine(CX - 50, 158, 100, C_TRACK);

  // 前回の動作時間
  canvas.setTextDatum(middle_right);
  canvas.setTextColor(C_SUB);
  canvas.drawString("LAST", CX - 6, 176);
  canvas.setTextDatum(middle_left);
  canvas.setFont(&fonts::FreeSansBold9pt7b);
  if (now < clearedUntilMs) {
    canvas.setTextColor(C_ACCENT);
    canvas.drawString("CLEARED", CX + 2, 176);
  } else {
    canvas.setTextColor(C_FG);
    canvas.drawString(lastRunSec ? fmtHM(lastRunSec) : String("--"), CX + 2, 176);
  }

  // 負荷の状態
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextColor(C_SUB);
  String load = "LCD " + String(brightness * 100 / 255) + "%   " + wifiLabel();
  canvas.drawString(load, CX, 198);
  canvas.drawString("BOOT #" + String(bootCount), CX, 218);

  canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // M5Unified 0.2.x は既定で Serial.begin() を呼ばないため明示する
  M5Dial.begin(cfg, true, false);  // encoder: on, RFID: off

  prefs.begin("battest", false);
  // 前回の起動で記録された時間を LAST に移す。
  // すぐに再起動を繰り返しても記録が消えないよう、run が 0 のときは上書きしない。
  uint32_t run = prefs.getUInt("run", 0);
  if (run > 0) {
    prefs.putUInt("last", run);
    prefs.putUInt("run", 0);
  }
  lastRunSec = prefs.getUInt("last", 0);
  bootCount = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", bootCount);
  brightness = prefs.getUChar("bri", 128);

  M5Dial.Display.setBrightness(brightness);
  M5Dial.BtnA.setHoldThresh(2000);

  canvas.setColorDepth(16);
  canvas.createSprite(W, H);

  hasWifiConfig = strlen(WIFI_SSID) > 0;
  if (hasWifiConfig) {
    startWifi();
  } else {
    WiFi.mode(WIFI_OFF);
  }

  lastEnc = M5Dial.Encoder.read();

  Serial.printf("[battest] boot #%lu, last run %lu sec\n", (unsigned long)bootCount,
                (unsigned long)lastRunSec);
}

void loop() {
  M5Dial.update();
  const uint32_t now = millis();

  // ダイヤル：明るさ
  long enc = M5Dial.Encoder.read();
  if (enc != lastEnc) {
    brightness = constrain(brightness + (int)(enc - lastEnc) * BRI_STEP, 0, 255);
    lastEnc = enc;
    M5Dial.Display.setBrightness(brightness);
    briDirty = true;
    briChangedMs = now;
  }
  if (briDirty && now - briChangedMs > BRI_SAVE_DELAY_MS) {
    prefs.putUChar("bri", brightness);
    briDirty = false;
  }

  // 短押し：Wi-Fi 切り替え / 長押し：記録をリセット
  if (M5Dial.BtnA.wasClicked()) {
    if (wifiWanted) stopWifi();
    else startWifi();
  }
  if (M5Dial.BtnA.wasHold()) {
    lastRunSec = 0;
    bootCount = 1;
    prefs.putUInt("last", 0);
    prefs.putUInt("boots", 1);
    clearedUntilMs = now + 2000;
  }

  // 経過時間を定期保存（給電が切れたあと、次の起動で LAST として表示する）
  if (now - lastSaveMs >= SAVE_INTERVAL_MS) {
    lastSaveMs = now;
    prefs.putUInt("run", now / 1000);
  }

  if (now - lastLogMs >= LOG_INTERVAL_MS) {
    lastLogMs = now;
    Serial.printf("[battest] uptime %s, lcd %d, %s\n", fmtHMS(now / 1000).c_str(), brightness,
                  wifiLabel().c_str());
  }

  if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = now;
    draw(now);
  }

  delay(10);
}
