// ============================================================================
//  oyobidashi / call-button
//  来場者がボタンを押すと、ntfy.sh 経由で担当者の iPhone に通知を送る。
//
//  入力
//    BIG スイッチ : PORT.B の G1 と GND の間につなぐ（INPUT_PULLUP、押すと LOW）
//    画面のボタン : BIG スイッチと同じ扱い（配線前の動作確認用）
//
//  状態
//    待機 → 送信中 → 呼び出し済み（60秒は再送しない）→ 待機
//                   → 送信失敗（もう一度押すと再送）
//
//  ※ 画面のレイアウトと文言は仮。デザインが決まったら draw() を作り直す。
// ============================================================================

#include <HTTPClient.h>
#include <M5Dial.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "certs.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#define WIFI_PASS ""
#endif
#ifndef NTFY_TOPIC
#define NTFY_TOPIC ""
#endif

namespace {

// --- 通知の内容 -------------------------------------------------------------
constexpr const char *NTFY_URL = "https://ntfy.sh/";
constexpr const char *NOTIFY_TITLE = "ブースで呼ばれています";
constexpr const char *NOTIFY_MESSAGE = "話を聞きたい人がいます！";
constexpr int NOTIFY_PRIORITY = 5;      // 1〜5。5 が最も目立つ
constexpr const char *NOTIFY_TAG = "bell";  // 🔔

// --- タイミング -------------------------------------------------------------
constexpr uint32_t CALLED_HOLD_MS = 60 * 1000;    // 呼び出し後、再送しない時間
constexpr uint32_t FAILED_HOLD_MS = 15 * 1000;    // 失敗表示を出しておく時間
constexpr uint32_t WIFI_WAIT_MS = 10 * 1000;      // 押されたとき Wi-Fi を待つ最大時間
constexpr uint32_t WIFI_RETRY_MS = 30 * 1000;     // 切れたままのとき接続し直す間隔
constexpr uint32_t HTTP_TIMEOUT_MS = 10 * 1000;
constexpr uint32_t DEBOUNCE_MS = 30;              // BIG スイッチのチャタリング対策
constexpr uint32_t DRAW_INTERVAL_MS = 50;

// --- ピン -------------------------------------------------------------------
constexpr int PIN_BIG_SWITCH = 1;  // PORT.B の G1

// --- 画面 -------------------------------------------------------------------
constexpr int W = 240;
constexpr int H = 240;
constexpr int CX = W / 2;
constexpr int CY = H / 2;

constexpr uint16_t C_BG = 0x0000;
constexpr uint16_t C_FG = 0xFFFF;
constexpr uint16_t C_SUB = 0x8C71;     // #8C8C8C
constexpr uint16_t C_TRACK = 0x2124;   // #262626
constexpr uint16_t C_ACCENT = 0xFAC3;  // #FF5A1F

enum class State { Idle, Sending, Called, Failed };

M5Canvas canvas(&M5Dial.Display);

State state = State::Idle;
uint32_t stateSinceMs = 0;
volatile int sendResult = 0;  // 送信タスクの結果。0: 送信中 / 1: 成功 / -1: 失敗

bool hasConfig = false;
bool wasConnected = false;
uint32_t lastWifiAttemptMs = 0;

bool swStable = HIGH;  // BIG スイッチの確定した状態（HIGH = 離している）
bool swLastRead = HIGH;
uint32_t swChangedMs = 0;

uint32_t lastDrawMs = 0;

const char *stateName(State s) {
  switch (s) {
    case State::Idle: return "idle";
    case State::Sending: return "sending";
    case State::Called: return "called";
    case State::Failed: return "failed";
  }
  return "?";
}

void setState(State s, uint32_t now) {
  Serial.printf("[call] state %s -> %s\n", stateName(state), stateName(s));
  state = s;
  stateSinceMs = now;
}

// --- Wi-Fi ------------------------------------------------------------------

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void updateWifi(uint32_t now) {
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      Serial.printf("[call] wifi connected, ip %s, rssi %d\n", WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
    } else {
      Serial.println("[call] wifi disconnected");
    }
    lastWifiAttemptMs = now;
  }
  // 自動再接続が効かないまま切れっぱなしのときは、接続し直す
  // （送信中は通信を邪魔しないよう触らない）
  if (!connected && state != State::Sending && now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
    lastWifiAttemptMs = now;
    Serial.println("[call] wifi retry");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// --- ntfy への送信（別タスクで動かし、その間も画面を描き続ける） --------------

bool publish() {
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_WAIT_MS) {
      Serial.println("[call] publish failed: no wifi");
      return false;
    }
    delay(100);
  }

  char body[256];
  snprintf(body, sizeof(body),
           "{\"topic\":\"%s\",\"title\":\"%s\",\"message\":\"%s\",\"priority\":%d,"
           "\"tags\":[\"%s\"]}",
           NTFY_TOPIC, NOTIFY_TITLE, NOTIFY_MESSAGE, NOTIFY_PRIORITY, NOTIFY_TAG);

  WiFiClientSecure client;
  client.setCACert(NTFY_ROOT_CA);
  client.setHandshakeTimeout(HTTP_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, NTFY_URL)) {
    Serial.println("[call] publish failed: http.begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  if (code == HTTP_CODE_OK) {
    Serial.printf("[call] published in %lu ms\n", (unsigned long)(millis() - t0));
  } else {
    Serial.printf("[call] publish failed: %d %s\n", code,
                  code < 0 ? http.errorToString(code).c_str() : http.getString().c_str());
  }
  http.end();
  return code == HTTP_CODE_OK;
}

void sendTask(void *) {
  sendResult = publish() ? 1 : -1;
  vTaskDelete(nullptr);
}

void onPress(uint32_t now, const char *source) {
  Serial.printf("[call] pressed (%s) in %s\n", source, stateName(state));
  if (state == State::Sending || state == State::Called) return;  // 連打対策
  if (!hasConfig) return;
  sendResult = 0;
  setState(State::Sending, now);
  xTaskCreatePinnedToCore(sendTask, "ntfy", 12 * 1024, nullptr, 1, nullptr, 0);
}

// --- 入力 -------------------------------------------------------------------

// BIG スイッチが押された瞬間に true を返す
bool bigSwitchPressed(uint32_t now) {
  const bool v = digitalRead(PIN_BIG_SWITCH);
  if (v != swLastRead) {
    swLastRead = v;
    swChangedMs = now;
  }
  if (v != swStable && now - swChangedMs >= DEBOUNCE_MS) {
    swStable = v;
    return swStable == LOW;
  }
  return false;
}

// --- 描画（仮） -------------------------------------------------------------

// 外周の弧を描く。start は 3時 = 0度・時計回り（LovyanGFX の角度）。360度をまたぐ場合は分けて描く
void drawRingArc(float start, float span, uint16_t color) {
  if (span <= 0) return;
  if (span > 360) span = 360;
  start = fmodf(start, 360.0f);
  const float end = start + span;
  if (end <= 360) {
    canvas.fillArc(CX, CY, 119, 114, start, end, color);
  } else {
    canvas.fillArc(CX, CY, 119, 114, start, 360, color);
    canvas.fillArc(CX, CY, 119, 114, 0, end - 360, color);
  }
}

// 幅に収まる最も大きいフォントで1行描く
void drawFit(const char *text, int y, uint16_t color, int maxWidth) {
  static const lgfx::IFont *fontsBySize[] = {
      &fonts::lgfxJapanGothicP_32,
      &fonts::lgfxJapanGothicP_24,
      &fonts::lgfxJapanGothicP_16,
  };
  for (auto f : fontsBySize) {
    canvas.setFont(f);
    if (canvas.textWidth(text) <= maxWidth) break;
  }
  canvas.setTextColor(color);
  canvas.drawString(text, CX, y);
}

void drawSub(const char *text, int y) {
  canvas.setFont(&fonts::lgfxJapanGothicP_16);
  canvas.setTextColor(C_SUB);
  canvas.drawString(text, CX, y);
}

void draw(uint32_t now) {
  const uint32_t elapsed = now - stateSinceMs;
  canvas.fillSprite(C_BG);
  canvas.setTextDatum(middle_center);

  canvas.fillArc(CX, CY, 119, 114, 0, 360, C_TRACK);

  if (!hasConfig) {
    drawFit("設定がありません", CY - 10, C_FG, 200);
    drawSub("include/secrets.h", CY + 24);
  } else {
    switch (state) {
      case State::Idle:
        drawFit("押してください", CY - 10, C_FG, 200);
        drawSub("担当者を呼び出します", CY + 24);
        break;
      case State::Sending: {
        // 回転する弧
        drawRingArc((now / 3) % 360, 60, C_ACCENT);
        drawFit("呼び出し中…", CY, C_FG, 200);
        break;
      }
      case State::Called: {
        // 再送できるようになるまでの残り時間を外周で表す（12時から時計回り）
        drawRingArc(270, 360.0f * (1.0f - (float)elapsed / CALLED_HOLD_MS), C_ACCENT);
        drawFit("呼び出しました", CY - 10, C_FG, 200);
        drawSub("少々お待ちください", CY + 24);
        break;
      }
      case State::Failed:
        canvas.fillArc(CX, CY, 119, 114, 0, 360, C_ACCENT);
        drawFit("通信できませんでした", CY - 10, C_FG, 200);
        drawSub("もう一度押してください", CY + 24);
        break;
    }

    if (WiFi.status() != WL_CONNECTED) {
      canvas.setFont(&fonts::lgfxJapanGothicP_12);
      canvas.setTextColor(C_SUB);
      canvas.drawString("Wi-Fi 未接続", CX, 200);
    }
  }

  canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // M5Unified 0.2.x は既定で Serial.begin() を呼ばないため明示する
  M5Dial.begin(cfg, true, false);  // encoder: on, RFID: off

  pinMode(PIN_BIG_SWITCH, INPUT_PULLUP);

  canvas.setColorDepth(16);
  canvas.createSprite(W, H);

  hasConfig = strlen(WIFI_SSID) > 0 && strlen(NTFY_TOPIC) > 0;
  if (hasConfig) {
    startWifi();
  } else {
    WiFi.mode(WIFI_OFF);
  }
  stateSinceMs = millis();
  lastWifiAttemptMs = stateSinceMs;

  Serial.printf("[call] boot, config %s, free heap %lu\n", hasConfig ? "ok" : "missing",
                (unsigned long)ESP.getFreeHeap());
}

void loop() {
  M5Dial.update();
  const uint32_t now = millis();

  if (hasConfig) updateWifi(now);

  if (bigSwitchPressed(now)) onPress(now, "big switch");
  if (M5Dial.BtnA.wasPressed()) onPress(now, "screen");

  switch (state) {
    case State::Sending:
      if (sendResult != 0) setState(sendResult > 0 ? State::Called : State::Failed, now);
      break;
    case State::Called:
      if (now - stateSinceMs >= CALLED_HOLD_MS) setState(State::Idle, now);
      break;
    case State::Failed:
      if (now - stateSinceMs >= FAILED_HOLD_MS) setState(State::Idle, now);
      break;
    case State::Idle:
      break;
  }

  if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = now;
    draw(now);
  }

  delay(5);
}
