// ============================================================================
//  oyobidashi / call-button
//  来場者が M5Dial の画面を押すと、ntfy.sh 経由で担当者の iPhone に通知を送る。
//  担当者が通知のボタンで返事をすると、画面に「向かっています」「12:15 ごろ戻ります」と出す。
//  画面・文言・音は docs/ux-proposal.html（2026-10-07 承認）に合わせている。
//
//  来場者の操作
//    画面に触れる（タッチ・押し込みどちらでも） : 担当者を呼ぶ
//    ダイヤルを回す                             : 待機中は外周の光がついてくる / 待っている間は製品カード
//
//  担当者の操作
//    iPhone の通知のボタン          : 向かいます / 5分で戻ります / 30分で戻ります
//    画面を押したままダイヤルを回す : 担当者メニュー（在席・離席・戻り時刻・音量・画面の向き）
//    呼び出しのあと、画面を3秒長押し : 待機に戻す
//
//  状態
//    待機 → 呼び出し中 → 届いた →（1分）QR →（2分）再通知 →（5分）お待たせ →（2分）待機
//                       └ 失敗（3回やり直してだめなとき）
//    担当者の返事 → 向かっています（5分）/ 不在・戻り時刻つき（その時刻まで）→ 待機
//    在席（担当者がいるとき。押しても通知しない。60分で待機に戻る）
// ============================================================================

#include <HTTPClient.h>
#include <M5Dial.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "certs.h"
#include "fonts_noto.h"  // tools/make_vlw.py で生成（画面の文言を変えたら作り直す）

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
constexpr const char *NTFY_HOST = "ntfy.sh";
constexpr const char *NTFY_URL = "https://ntfy.sh/";
constexpr const char *REPLY_SUFFIX = "-reply";  // 返事用のトピック = NTFY_TOPIC + これ
// 待たせているときに画面に出す QR コード。自分の SNS やサイトの URL に書き換える
constexpr const char *QR_URL = "https://example.com/";
constexpr const char *QR_CAPTION = "詳しく見てみる";  // QR の下の一言（日本語を変えたらフォントを作り直す）
constexpr const char *QR_LABEL = "example.com";      // その下の小さな文字（英数字ならフォントの作り直しは不要）

constexpr const char *CALL_TITLE = "ブースで呼ばれています";
constexpr const char *CALL_MESSAGE = "話を聞きたい人がいます！";  // 後ろに押された時刻 (10:42) を付ける
constexpr const char *REMIND_TITLE = "まだお待ちです";
constexpr const char *CUTOFF_TITLE = "来場者に連絡先を案内しました";
constexpr const char *CUTOFF_MESSAGE = "5分たったので、画面に QR を出しました";
constexpr const char *VISIT_TITLE = "来場者が来ました";  // 不在中に押されたとき（目立たない通知）
constexpr const char *VISIT_MESSAGE = "ブースで待っている人がいます";

// 通知のボタン（最大3つ）。body が返事用のトピックに届く
struct Action {
  const char *label;
  const char *body;
};
constexpr Action ACTIONS[] = {
    {"向かいます", "coming"},
    {"5分で戻ります", "back5"},
    {"30分で戻ります", "back30"},
};

// 待っている間にダイヤルで見られる製品カード
// サンプルなので、自分の製品・ショップ・SNS に書き換える（カードの数は増減してよい）
constexpr const char *STORE_URL = "https://example.com/";
struct Card {
  const char *name;
  const char *desc;
  const char *url;
};
constexpr Card CARDS[] = {
    {"Product A", "Short description", STORE_URL},
    {"Product B", "Short description", STORE_URL},
    {"Product C", "Short description", STORE_URL},
    {"Follow us", "新作や制作の様子を発信中", "https://example.com/"},
};
constexpr int CARD_COUNT = sizeof(CARDS) / sizeof(CARDS[0]);

// --- タイミング -------------------------------------------------------------
constexpr uint32_t COMMIT_MS = 1000;  // 押したまま回す操作を待つ時間（離せばすぐ送る）
constexpr uint32_t RETRY_WAIT_MS[] = {0, 5000, 15000};  // 送れなかったときのやり直し
constexpr int MAX_ATTEMPTS = 3;
constexpr uint32_t QR_AFTER_MS = 60 * 1000;          // 押されてから QR を出すまで
constexpr uint32_t REMIND_AFTER_MS = 2 * 60 * 1000;  // 再通知
constexpr uint32_t CUTOFF_AFTER_MS = 5 * 60 * 1000;  // 区切り（お待たせ）
constexpr uint32_t SORRY_HOLD_MS = 2 * 60 * 1000;
constexpr uint32_t COMING_HOLD_MS = 5 * 60 * 1000;
constexpr uint32_t FAILED_HOLD_MS = 30 * 1000;
constexpr uint32_t PRESENT_HOLD_MS = 60 * 60 * 1000;
constexpr uint32_t VISIT_INTERVAL_MS = 3 * 60 * 1000;  // 不在中の通知を送る最短の間隔
constexpr uint32_t STAFF_HOLD_START_MS = 800;  // 長押しの進み具合を出し始める
constexpr uint32_t STAFF_HOLD_MS = 3000;       // 待機に戻す長押し
constexpr uint32_t MENU_TIMEOUT_MS = 10 * 1000;
constexpr uint32_t CARD_HOLD_MS = 8 * 1000;    // 製品カードを触らないと元の画面に戻るまで
constexpr uint32_t SPIN_FADE_MS = 1500;        // 回したときの外周の光が消えるまで
constexpr int32_t ENC_PER_DETENT = 4;          // ダイヤル1段あたりのエンコーダーのカウント
constexpr uint32_t TOAST_MS = 1600;
constexpr uint32_t WIFI_WAIT_MS = 10 * 1000;
constexpr uint32_t WIFI_RETRY_MS = 60 * 1000;         // つながらないまま、これだけたったらやり直す
constexpr uint32_t WIFI_RETRY_FAILED_MS = 10 * 1000;  // はっきり失敗したとき（電波なし・認証失敗など）
constexpr uint32_t HTTP_TIMEOUT_MS = 10 * 1000;
constexpr uint32_t LISTEN_RETRY_MS = 3000;
constexpr uint32_t DRAW_INTERVAL_MS = 33;

// --- 時刻 -------------------------------------------------------------------
constexpr const char *TZ_JST = "JST-9";
constexpr const char *NTP_SERVER1 = "ntp.nict.jp";
constexpr const char *NTP_SERVER2 = "pool.ntp.org";

// --- 画面 -------------------------------------------------------------------
constexpr int CX = 120;
constexpr int CY = 120;

// RGB888。描くときに RGB565 にする
constexpr uint32_t K_BG = 0x000000;
constexpr uint32_t K_FG = 0xFFFFFF;
constexpr uint32_t K_SUB = 0x8C8C8C;
constexpr uint32_t K_TRACK = 0x262626;
constexpr uint32_t K_ACCENT = 0xFF5A1F;
constexpr uint32_t K_INK = 0x140700;  // オレンジの上の文字
// QR を出す画面は白地にして、切り替わったことが分かるようにする
constexpr uint32_t K_PAPER = 0xFFFFFF;
constexpr uint32_t K_PAPER_FG = 0x111111;
constexpr uint32_t K_PAPER_SUB = 0x707070;
constexpr uint32_t K_PAPER_TRACK = 0xE2E2E2;

// --- 担当者メニュー -----------------------------------------------------------
enum MenuId {
  M_CLOSE, M_PRESENT, M_LEAVE, M_AWAY15, M_AWAY30, M_AWAY60, M_VOLUME, M_ROTATE, M_COUNT
};
const char *const MENU_LABELS[] = {"閉じる", "在席にする", "離席にする", "15分で戻る",
                                   "30分で戻る", "60分で戻る", nullptr, nullptr};
constexpr int ROTATE_STEP_DEG = 10;  // 画面の向きを回す刻み（プレートへの取り付けの向きに合わせる）
struct Volume {
  const char *label;
  uint8_t value;
};
constexpr Volume VOLUMES[] = {{"大", 255}, {"中", 200}, {"小", 100}, {"なし", 0}};
constexpr int VOLUME_COUNT = sizeof(VOLUMES) / sizeof(VOLUMES[0]);

enum class State { Idle, Calling, Sent, Coming, Away, Sorry, Failed, Present };
enum class Reply { None, Coming, Back5, Back30 };

M5Canvas canvas(&M5Dial.Display);
Preferences prefs;

// Noto Sans JP のなめらかフォント（見出し / 小見出し / 補足 / 英語・時間）
lgfx::PointerWrapper fontData[4];
lgfx::VLWfont fonts[4];

State state = State::Idle;
uint32_t stateSinceMs = 0;
bool hasConfig = false;
char replyTopic[64] = "";

// 呼び出し
bool callCommitted = false;  // 押したあと、実際に送ると決めたか
int attempts = 0;
uint32_t nextAttemptMs = 0;
bool sendInFlight = false;
char pressedAt[8] = "";  // 押された時刻 "10:42"。時刻が合っていなければ空
uint32_t callMs = 0;     // 押された millis()
bool reminded = false;

// 不在（戻り時刻つき）
uint32_t awayUntilMs = 0;
uint32_t awayTotalMs = 1;
int awayMinutes = 0;
char awayAt[8] = "";  // 戻る時刻 "12:15"。時刻が合っていなければ空
uint32_t lastVisitMs = 0;
bool visitSent = false;

// 入力
bool wasDown = false;
uint32_t downMs = 0;
int32_t encAtDown = 0;
int32_t lastEnc = 0;
int32_t encAcc = 0;
bool gestureDone = false;
State stateAtDown = State::Idle;
bool visitPending = false;  // 不在中に触れた。短く離したら目立たない通知を送る

// ダイヤル
bool menuOpen = false;
bool menuArmed = false;  // メニューを開いた押下を離したか（離したあとの押下で決定する）
int menuSel = M_CLOSE;
uint32_t menuInputMs = 0;
int volumeIdx = 1;
int screenRotation = 0;   // 画面の向き（度、時計回り）。本体に保存する
bool menuAdjust = false;  // 「画面の向き」を調整している
int cardIdx = 0;  // 0 = 状態の画面、1〜CARD_COUNT = 製品カード
int spinPos = 0;  // 待機中に回した位置（1段 = 1）
uint32_t lastTurnMs = 0;

// 表示の演出
uint32_t fxMs = 0;
bool fxSink = false;
char toastBuf[48] = "";
uint32_t toastUntilMs = 0;

// Wi-Fi
bool wasConnected = false;
uint32_t lastWifiAttemptMs = 0;
int lastWifiStatus = -1;

uint32_t lastDrawMs = 0;

const char *stateName(State s) {
  switch (s) {
    case State::Idle: return "idle";
    case State::Calling: return "calling";
    case State::Sent: return "sent";
    case State::Coming: return "coming";
    case State::Away: return "away";
    case State::Sorry: return "sorry";
    case State::Failed: return "failed";
    case State::Present: return "present";
  }
  return "?";
}

bool localTimeValid() { return time(nullptr) > 1700000000; }

// ============================================================================
//  音（ブザー）。鳴らし終わるのを待たずに、loop() の中で順番に鳴らす
// ============================================================================

struct Note {
  uint16_t freq;
  uint16_t ms;
};
constexpr Note SND_PRESS[] = {{2093, 45}};
constexpr Note SND_SENT[] = {{1568, 70}, {2093, 120}};
constexpr Note SND_COMING[] = {{1319, 200}, {1047, 420}};
constexpr Note SND_TICK[] = {{1568, 18}};
constexpr Note SND_FAIL[] = {{523, 110}, {523, 110}};
constexpr Note SND_RESET[] = {{1047, 60}, {1568, 60}, {2093, 90}};
constexpr Note SND_MENU[] = {{1047, 40}, {1568, 40}};
constexpr uint32_t NOTE_GAP_MS = 35;

const Note *seq = nullptr;
int seqLen = 0;
int seqIdx = 0;
uint32_t noteEndMs = 0;

void startNote(uint32_t now) {
  M5Dial.Speaker.tone(seq[seqIdx].freq, seq[seqIdx].ms);
  noteEndMs = now + seq[seqIdx].ms + NOTE_GAP_MS;
}

template <size_t N>
void play(const Note (&notes)[N]) {
  seq = notes;
  seqLen = N;
  seqIdx = 0;
  startNote(millis());
}

void updateSound(uint32_t now) {
  if (seqIdx < seqLen && now >= noteEndMs) {
    if (++seqIdx < seqLen) startNote(now);
  }
}

// ============================================================================
//  通信（別タスク）。送信と返事の待ち受けを順番に行い、同時に2本つながないようにする
// ============================================================================

struct Publish {
  char title[64];
  char message[128];
  int priority;
};
Publish pubReq;
volatile bool pubPending = false;
volatile int pubResult = 0;  // 0: 送信中 / 1: 成功 / -1: 失敗

volatile bool listenWanted = false;
volatile uint32_t listenEpoch = 0;  // 増えたら、それより前の返事は読まない（呼び出しごとに増やす）
volatile Reply replyReceived = Reply::None;

// JSON の文字列に入れるのは定数と数字だけなので、エスケープはしていない
bool publish(const Publish &p) {
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_WAIT_MS) {
      Serial.println("[net] publish failed: no wifi");
      return false;
    }
    delay(100);
  }

  // 通知のボタン（http アクション）で返事用のトピックに送ってもらう。
  // 通知そのもののタップ（click）には何も割り当てない（読むだけで返事になってしまうため）
  char actions[512] = "";
  for (const auto &a : ACTIONS) {
    char one[160];
    snprintf(one, sizeof(one),
             "%s{\"action\":\"http\",\"label\":\"%s\",\"url\":\"https://%s/%s\",\"method\":\"POST\","
             "\"body\":\"%s\",\"clear\":true}",
             actions[0] ? "," : "", a.label, NTFY_HOST, replyTopic, a.body);
    strlcat(actions, one, sizeof(actions));
  }
  char body[1024];
  snprintf(body, sizeof(body),
           "{\"topic\":\"%s\",\"title\":\"%s\",\"message\":\"%s\",\"priority\":%d,%s"
           "\"actions\":[%s]}",
           NTFY_TOPIC, p.title, p.message, p.priority,
           p.priority >= 5 ? "\"tags\":[\"bell\"]," : "", actions);

  WiFiClientSecure client;
  client.setCACert(NTFY_ROOT_CA);
  client.setHandshakeTimeout(HTTP_TIMEOUT_MS / 1000);
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(client, NTFY_URL)) {
    Serial.println("[net] publish failed: http.begin");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(body);
  if (code == HTTP_CODE_OK) {
    Serial.printf("[net] published \"%s\" in %lu ms\n", p.title, (unsigned long)(millis() - t0));
  } else {
    Serial.printf("[net] publish failed: %d %s\n", code,
                  code < 0 ? http.errorToString(code).c_str() : http.getString().c_str());
  }
  http.end();
  return code == HTTP_CODE_OK;
}

// 返事用のトピックを読み続ける（HTTP/1.0 で頼むと1行ずつそのまま届く）。
// 前に読んだ返事があればその続きから、なければ押されたときから読む
bool startListen(WiFiClientSecure &c, const String &lastId) {
  c.setCACert(NTFY_ROOT_CA);
  c.setHandshakeTimeout(HTTP_TIMEOUT_MS / 1000);
  if (!c.connect(NTFY_HOST, 443)) {
    Serial.println("[net] listen connect failed");
    return false;
  }
  char since[32];
  if (lastId.length()) {
    strlcpy(since, lastId.c_str(), sizeof(since));
  } else {
    snprintf(since, sizeof(since), "%lus", (unsigned long)((millis() - callMs) / 1000 + 3));
  }
  c.printf("GET /%s/json?since=%s HTTP/1.0\r\nHost: %s\r\nUser-Agent: oyobidashi\r\n\r\n",
           replyTopic, since, NTFY_HOST);

  // ヘッダーを読み飛ばす
  const uint32_t t0 = millis();
  bool ok = false;
  while (millis() - t0 < HTTP_TIMEOUT_MS) {
    if (!c.available()) {
      if (!c.connected()) break;
      delay(10);
      continue;
    }
    const String line = c.readStringUntil('\n');
    if (line.startsWith("HTTP/")) ok = line.indexOf(" 200") > 0;
    if (line == "\r" || line.isEmpty()) {
      if (ok) Serial.printf("[net] listening (since %s)\n", since);
      if (!ok) c.stop();
      return ok;
    }
  }
  Serial.println("[net] listen failed: bad response");
  c.stop();
  return false;
}

void handleLine(const String &line, String &lastId) {
  if (line.indexOf("\"event\":\"message\"") < 0) return;
  const int i = line.indexOf("\"id\":\"");
  if (i >= 0) lastId = line.substring(i + 6, line.indexOf('"', i + 6));
  Reply r = Reply::Coming;
  if (line.indexOf("\"message\":\"back30\"") >= 0) r = Reply::Back30;
  else if (line.indexOf("\"message\":\"back5\"") >= 0) r = Reply::Back5;
  Serial.printf("[net] reply: %s (id %s)\n",
                r == Reply::Back30 ? "back30" : r == Reply::Back5 ? "back5" : "coming",
                lastId.c_str());
  replyReceived = r;
}

void netTask(void *) {
  WiFiClientSecure listen;
  bool listening = false;
  uint32_t lastListenTry = 0;
  uint32_t epoch = listenEpoch;
  String line;
  String lastId;
  for (;;) {
    if (epoch != listenEpoch) {
      epoch = listenEpoch;
      lastId = "";
      if (listening) {
        listen.stop();
        listening = false;
      }
    }
    if (pubPending) {
      if (listening) {
        listen.stop();
        listening = false;
      }
      pubResult = publish(pubReq) ? 1 : -1;
      pubPending = false;
    } else if (listenWanted && WiFi.status() == WL_CONNECTED) {
      if (!listening) {
        if (millis() - lastListenTry >= LISTEN_RETRY_MS) {
          lastListenTry = millis();
          line = "";
          listening = startListen(listen, lastId);
        }
      } else if (!listen.connected() && !listen.available()) {
        Serial.println("[net] listen closed");
        listen.stop();
        listening = false;
      } else {
        while (listen.available()) {
          const char ch = listen.read();
          if (ch == '\n') {
            handleLine(line, lastId);
            line = "";
          } else if (line.length() < 1024) {
            line += ch;
          }
        }
      }
    } else if (listening) {
      listen.stop();
      listening = false;
      Serial.println("[net] listen stopped");
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void requestPublish(const char *title, const char *message, int priority) {
  strlcpy(pubReq.title, title, sizeof(pubReq.title));
  strlcpy(pubReq.message, message, sizeof(pubReq.message));
  pubReq.priority = priority;
  pubResult = 0;
  pubPending = true;
}

// ============================================================================
//  Wi-Fi
// ============================================================================

void startWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void updateWifi(uint32_t now) {
  const int st = WiFi.status();
  if (st != lastWifiStatus) {
    Serial.printf("[wifi] status %d at %lu ms\n", st, (unsigned long)now);
    lastWifiStatus = st;
  }
  const bool connected = st == WL_CONNECTED;
  if (connected != wasConnected) {
    wasConnected = connected;
    if (connected) {
      Serial.printf("[call] wifi connected, ip %s, rssi %d\n", WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
      configTzTime(TZ_JST, NTP_SERVER1, NTP_SERVER2);
    } else {
      Serial.println("[call] wifi disconnected");
    }
    lastWifiAttemptMs = now;
  }
  // 自動再接続が効かないまま切れっぱなしのときは、接続し直す（送信中は触らない）。
  // 接続の途中でやり直すとかえって遅くなるので、はっきり失敗したとき以外は長めに待つ
  const bool failed = st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL || st == WL_CONNECTION_LOST;
  const uint32_t wait = failed ? WIFI_RETRY_FAILED_MS : WIFI_RETRY_MS;
  if (!connected && !pubPending && now - lastWifiAttemptMs >= wait) {
    lastWifiAttemptMs = now;
    Serial.println("[call] wifi retry");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// ============================================================================
//  状態
// ============================================================================

void setState(State s, uint32_t now) {
  Serial.printf("[call] state %s -> %s\n", stateName(state), stateName(s));
  state = s;
  stateSinceMs = now;
  cardIdx = 0;
  listenWanted = (s == State::Sent || s == State::Coming || s == State::Away || s == State::Sorry);
}

void toast(const char *text, uint32_t now) {
  strlcpy(toastBuf, text, sizeof(toastBuf));
  toastUntilMs = now + TOAST_MS;
}

void nowHHMM(char *buf, size_t n) {
  struct tm t;
  if (getLocalTime(&t, 0)) {
    strftime(buf, n, "%H:%M", &t);
  } else {
    buf[0] = '\0';
  }
}

void startCall(uint32_t now) {
  nowHHMM(pressedAt, sizeof(pressedAt));
  callMs = now;
  callCommitted = false;
  sendInFlight = false;
  attempts = 0;
  reminded = false;
  replyReceived = Reply::None;
  listenEpoch++;
  setState(State::Calling, now);
}

void sendCall() {
  char msg[128];
  if (pressedAt[0]) {
    snprintf(msg, sizeof(msg), "%s(%s)", CALL_MESSAGE, pressedAt);
  } else {
    snprintf(msg, sizeof(msg), "%s", CALL_MESSAGE);
  }
  attempts++;
  Serial.printf("[call] send attempt %d/%d (pressed at %s)\n", attempts, MAX_ATTEMPTS,
                pressedAt[0] ? pressedAt : "unknown");
  requestPublish(CALL_TITLE, msg, 5);
  sendInFlight = true;
}

void sendReminder(uint32_t now) {
  char msg[128];
  const unsigned long min = (now - callMs) / 60000;
  if (pressedAt[0]) {
    snprintf(msg, sizeof(msg), "%s(%sから・%lu分経過)", CALL_MESSAGE, pressedAt, min);
  } else {
    snprintf(msg, sizeof(msg), "%s(%lu分経過)", CALL_MESSAGE, min);
  }
  requestPublish(REMIND_TITLE, msg, 5);
}

// 不在にする。fresh = 担当者メニューから（それまでの返事は読まない）
void enterAway(int minutes, uint32_t now, bool fresh) {
  awayMinutes = minutes;
  awayTotalMs = minutes * 60000UL;
  awayUntilMs = now + awayTotalMs;
  if (localTimeValid()) {
    const time_t t = time(nullptr) + minutes * 60;
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(awayAt, sizeof(awayAt), "%H:%M", &tm);
  } else {
    awayAt[0] = '\0';
  }
  visitSent = false;
  if (fresh) {
    callMs = now;
    listenEpoch++;
  }
  Serial.printf("[call] away %d min (back at %s)\n", minutes, awayAt[0] ? awayAt : "unknown");
  setState(State::Away, now);
}

void awayText(char *buf, size_t n) {
  if (awayAt[0]) snprintf(buf, n, "%s ごろ戻ります", awayAt);
  else snprintf(buf, n, "%d分ほどで戻ります", awayMinutes);
}

// 不在中に押されたとき：画面で戻り時刻を返し、担当者には目立たない通知を送る
void onVisitWhileAway(uint32_t now) {
  if (visitSent && now - lastVisitMs < VISIT_INTERVAL_MS) return;
  if (pubPending) return;
  visitSent = true;
  lastVisitMs = now;
  char at[8], msg[128];
  nowHHMM(at, sizeof(at));
  if (at[0]) snprintf(msg, sizeof(msg), "%s(%s)", VISIT_MESSAGE, at);
  else snprintf(msg, sizeof(msg), "%s", VISIT_MESSAGE);
  Serial.println("[call] visitor while away: quiet notification");
  requestPublish(VISIT_TITLE, msg, 3);
}

// 押された瞬間
void onDown(uint32_t now) {
  stateAtDown = state;
  fxMs = now;
  fxSink = false;
  switch (state) {
    case State::Idle:
    case State::Failed:
    case State::Sorry:
      play(SND_PRESS);
      fxSink = state != State::Sorry;
      Serial.printf("[call] pressed in %s\n", stateName(state));
      startCall(now);
      break;
    case State::Present:
      play(SND_TICK);
      toast("目の前にいます", now);
      break;
    case State::Away: {
      // 通知は離したときに送る（戻ってきた制作者の長押しで、自分宛てに通知が飛ばないように）
      play(SND_TICK);
      char buf[48];
      awayText(buf, sizeof(buf));
      toast(buf, now);
      visitPending = true;
      break;
    }
    default:
      play(SND_TICK);
      toast(state == State::Coming ? "もうすぐ着きます" : "もう届いています", now);
      Serial.printf("[call] pressed again in %s (no new notification)\n", stateName(state));
      break;
  }
}

// --- 担当者メニュー -----------------------------------------------------------

void openMenu(uint32_t now) {
  menuOpen = true;
  menuArmed = false;
  menuSel = M_CLOSE;  // 偶然開いて押しても閉じるだけになるように
  menuAdjust = false;
  menuInputMs = now;
  encAcc = 0;
  play(SND_MENU);
  Serial.println("[call] menu open");
}

void closeMenu() {
  if (menuAdjust) {
    menuAdjust = false;
    prefs.putUShort("rot", screenRotation);
    Serial.printf("[call] screen rotation %d\n", screenRotation);
  }
  menuOpen = false;
  Serial.println("[call] menu close");
}

void menuConfirm(uint32_t now) {
  menuInputMs = now;
  Serial.printf("[call] menu select %d\n", menuSel);
  switch (menuSel) {
    case M_CLOSE:
      play(SND_TICK);
      closeMenu();
      break;
    case M_PRESENT:
      play(SND_RESET);
      closeMenu();
      setState(State::Present, now);
      break;
    case M_LEAVE:
      play(SND_RESET);
      closeMenu();
      setState(State::Idle, now);
      break;
    case M_AWAY15:
    case M_AWAY30:
    case M_AWAY60:
      play(SND_RESET);
      closeMenu();
      enterAway(menuSel == M_AWAY15 ? 15 : menuSel == M_AWAY30 ? 30 : 60, now, true);
      break;
    case M_ROTATE:
      // 1回目：調整を始める（回すとその場で画面が回る）／2回目：決める
      if (!menuAdjust) {
        menuAdjust = true;
        play(SND_PRESS);
      } else {
        play(SND_RESET);
        closeMenu();
      }
      break;
    case M_VOLUME:
      volumeIdx = (volumeIdx + 1) % VOLUME_COUNT;
      M5Dial.Speaker.setVolume(VOLUMES[volumeIdx].value);
      prefs.putUChar("vol", volumeIdx);
      play(SND_PRESS);  // 新しい音量で試しに鳴らす
      break;
  }
}

// --- 入力 -------------------------------------------------------------------

// 制作者が長押しで待機に戻せる状態か（お待たせの画面からは、触れた瞬間に呼び出し中になっている）
bool staffResettable() {
  return state == State::Sent || state == State::Coming || state == State::Away ||
         (state == State::Calling && !callCommitted && stateAtDown == State::Sorry);
}

// エンコーダーのカウントをダイヤルの段数に直す
int takeDetents(int32_t delta) {
  encAcc += delta;
  int steps = 0;
  while (encAcc >= ENC_PER_DETENT) {
    encAcc -= ENC_PER_DETENT;
    steps++;
  }
  while (encAcc <= -ENC_PER_DETENT) {
    encAcc += ENC_PER_DETENT;
    steps--;
  }
  return steps;
}

void updateInput(uint32_t now) {
  const bool down = M5Dial.BtnA.isPressed() || M5.Touch.getCount() > 0;
  const int32_t enc = M5Dial.Encoder.read();
  const int32_t delta = enc - lastEnc;
  lastEnc = enc;

  if (down && !wasDown) {
    wasDown = true;
    downMs = now;
    encAtDown = enc;
    gestureDone = false;
    if (menuOpen) {
      gestureDone = true;
      if (menuArmed) menuConfirm(now);
    } else {
      onDown(now);
    }
  } else if (!down && wasDown) {
    wasDown = false;
    if (menuOpen) menuArmed = true;
    const bool tap = !gestureDone && now - downMs < STAFF_HOLD_START_MS;
    if (visitPending && state == State::Away && tap) onVisitWhileAway(now);
    visitPending = false;
    // お待たせの画面からの呼び出しは、短く触れたときだけ送る（長押しは制作者の操作）
    if (state == State::Calling && !callCommitted && stateAtDown == State::Sorry && !gestureDone) {
      if (tap) {
        callCommitted = true;
        nextAttemptMs = now;
      } else {
        Serial.println("[call] long press from sorry: canceled");
        setState(State::Sorry, now);
      }
    }
  }

  // メニューが開いているときは、回すと選ぶ・押すと決める
  if (menuOpen) {
    const int steps = takeDetents(delta);
    if (steps) {
      if (menuAdjust) {
        screenRotation = ((screenRotation + steps * ROTATE_STEP_DEG) % 360 + 360) % 360;
      } else {
        menuSel = ((menuSel + steps) % M_COUNT + M_COUNT) % M_COUNT;
      }
      menuInputMs = now;
      play(SND_TICK);
    }
    if (now - menuInputMs >= MENU_TIMEOUT_MS) closeMenu();
    return;
  }

  // 呼び出しは、離したとき（または押したまま COMMIT_MS たったとき）に送ると決める
  if (state == State::Calling && !callCommitted && stateAtDown != State::Sorry &&
      (!down || now - downMs >= COMMIT_MS)) {
    callCommitted = true;
    nextAttemptMs = now;
  }

  if (down) {
    if (gestureDone) return;
    // 押したままダイヤルを回す：担当者メニュー（まだ送っていない呼び出しは取り消す）
    if (abs(enc - encAtDown) >= ENC_PER_DETENT) {
      gestureDone = true;
      Serial.printf("[call] rotate while pressed (%ld counts)\n", (long)(enc - encAtDown));
      if (state == State::Calling && !callCommitted) {
        Serial.println("[call] call canceled by staff gesture");
        setState(stateAtDown == State::Present ? State::Present : State::Idle, now);
      }
      openMenu(now);
      return;
    }
    // 呼び出しのあとの3秒長押し：待機に戻す
    if (staffResettable() && now - downMs >= STAFF_HOLD_MS) {
      gestureDone = true;
      Serial.println("[call] staff hold: back to idle");
      play(SND_RESET);
      setState(State::Idle, now);
    }
    return;
  }

  // 押していないときに回す：待機中は外周の光、待っている間は製品カード
  const int steps = takeDetents(delta);
  if (steps) {
    lastTurnMs = now;
    Serial.printf("[call] dial %+d (enc %ld)\n", steps, (long)enc);
    if (state == State::Idle || state == State::Present) {
      spinPos += steps;
    } else if (state == State::Sent || state == State::Coming || state == State::Away ||
               state == State::Sorry) {
      cardIdx = ((cardIdx + steps) % (CARD_COUNT + 1) + CARD_COUNT + 1) % (CARD_COUNT + 1);
      play(SND_TICK);
    }
  }
  if (cardIdx && now - lastTurnMs >= CARD_HOLD_MS) cardIdx = 0;
}

void updateState(uint32_t now) {
  const uint32_t el = now - stateSinceMs;

  // 担当者の返事
  const Reply r = replyReceived;
  if (r != Reply::None) {
    replyReceived = Reply::None;
    if (state == State::Sent || state == State::Sorry || state == State::Coming ||
        state == State::Away) {
      play(SND_COMING);
      if (r == Reply::Coming) setState(State::Coming, now);
      else enterAway(r == Reply::Back30 ? 30 : 5, now, false);
    }
  }

  switch (state) {
    case State::Calling:
      if (sendInFlight) {
        if (pubResult == 0) break;
        sendInFlight = false;
        if (pubResult > 0) {
          play(SND_SENT);
          setState(State::Sent, now);
        } else if (attempts >= MAX_ATTEMPTS) {
          play(SND_FAIL);
          setState(State::Failed, now);
        } else {
          nextAttemptMs = now + RETRY_WAIT_MS[attempts];
          Serial.printf("[call] retry in %lu ms\n", (unsigned long)RETRY_WAIT_MS[attempts]);
        }
      } else if (callCommitted && now >= nextAttemptMs && !pubPending) {
        sendCall();
      }
      break;
    case State::Sent:
      if (!reminded && now - callMs >= REMIND_AFTER_MS && !pubPending) {
        reminded = true;
        sendReminder(now);
      }
      if (now - callMs >= CUTOFF_AFTER_MS && !pubPending) {
        requestPublish(CUTOFF_TITLE, CUTOFF_MESSAGE, 3);
        setState(State::Sorry, now);
      }
      break;
    case State::Coming:
      if (el >= COMING_HOLD_MS) setState(State::Idle, now);
      break;
    case State::Away:
      if ((int32_t)(now - awayUntilMs) >= 0) setState(State::Idle, now);
      break;
    case State::Sorry:
      if (el >= SORRY_HOLD_MS) setState(State::Idle, now);
      break;
    case State::Failed:
      if (el >= FAILED_HOLD_MS) setState(State::Idle, now);
      break;
    case State::Present:
      if (el >= PRESENT_HOLD_MS) {
        Serial.println("[call] present expired");
        setState(State::Idle, now);
      }
      break;
    case State::Idle:
      break;
  }
}

// ============================================================================
//  描画（docs/ux-proposal.html のプロトタイプと同じ配置）
// ============================================================================

uint16_t c565(uint32_t rgb) { return lgfx::color565(rgb >> 16, (rgb >> 8) & 0xFF, rgb & 0xFF); }

// fg を bg の上に a の濃さで重ねた色
uint32_t mix(uint32_t fg, uint32_t bg, float a) {
  if (a < 0) a = 0;
  if (a > 1) a = 1;
  auto ch = [&](int s) {
    const int f = (fg >> s) & 0xFF, b = (bg >> s) & 0xFF;
    return (uint32_t)(b + (f - b) * a + 0.5f) << s;
  };
  return ch(16) | ch(8) | ch(0);
}

void dot(float x, float y, int r, uint32_t color) {
  canvas.fillSmoothCircle((int)(x + 0.5f), (int)(y + 0.5f), r, c565(color));
}

// 外周の60個のドット。color(i) で i 番目（12時から時計回り）の色を決める
template <typename F>
void rim(F color) {
  for (int i = 0; i < 60; i++) {
    const float a = -PI / 2 + i * TWO_PI / 60;
    dot(CX + 111 * cosf(a), CY + 111 * sinf(a), 2, color(i));
  }
}

// 回した位置のまわりを明るくする（待機中・在席中）
float spinGlow(int i, uint32_t now) {
  if (!lastTurnMs || now - lastTurnMs >= SPIN_FADE_MS) return 0;
  const int center = (((spinPos % 16) + 16) % 16) * 60 / 16;
  int d = abs(i - center);
  d = min(d, 60 - d);
  if (d > 3) return 0;
  return (1 - d / 4.0f) * (1 - (float)(now - lastTurnMs) / SPIN_FADE_MS);
}

// 5x7 のドット文字（使う文字だけ）
const uint8_t *glyph(char ch) {
  static const uint8_t A[] = {14, 17, 17, 31, 17, 17, 17}, B[] = {30, 17, 17, 30, 17, 17, 30},
                       C[] = {14, 17, 16, 16, 16, 17, 14}, E[] = {31, 16, 16, 30, 16, 16, 31},
                       G[] = {14, 17, 16, 23, 17, 17, 15}, H[] = {17, 17, 17, 31, 17, 17, 17},
                       I[] = {14, 4, 4, 4, 4, 4, 14}, K[] = {17, 18, 20, 24, 20, 18, 17},
                       L[] = {16, 16, 16, 16, 16, 16, 31}, M[] = {17, 27, 21, 21, 17, 17, 17},
                       N[] = {17, 17, 25, 21, 19, 17, 17}, O[] = {14, 17, 17, 17, 17, 17, 14},
                       P[] = {30, 17, 17, 30, 16, 16, 16}, R[] = {30, 17, 17, 30, 20, 18, 17},
                       S[] = {15, 16, 16, 14, 1, 1, 30}, T[] = {31, 4, 4, 4, 4, 4, 4},
                       U[] = {17, 17, 17, 17, 17, 17, 14}, Y[] = {17, 17, 10, 4, 4, 4, 4},
                       D0[] = {14, 17, 19, 21, 25, 17, 14}, D1[] = {4, 12, 4, 4, 4, 4, 14},
                       D2[] = {14, 17, 1, 2, 4, 8, 31}, D3[] = {31, 2, 4, 2, 1, 17, 14},
                       D4[] = {2, 6, 10, 18, 31, 2, 2}, D5[] = {31, 16, 30, 1, 1, 17, 14},
                       D6[] = {6, 8, 16, 30, 17, 17, 14}, D7[] = {31, 1, 2, 4, 8, 8, 8},
                       D8[] = {14, 17, 17, 14, 17, 17, 14}, D9[] = {14, 17, 17, 15, 1, 2, 12},
                       COLON[] = {0, 4, 4, 0, 4, 4, 0};
  switch (ch) {
    case 'A': return A; case 'B': return B; case 'C': return C; case 'E': return E;
    case 'G': return G; case 'H': return H; case 'I': return I; case 'K': return K;
    case 'L': return L; case 'M': return M; case 'N': return N; case 'O': return O;
    case 'P': return P; case 'R': return R; case 'S': return S; case 'T': return T;
    case 'U': return U; case 'Y': return Y;
    case '0': return D0; case '1': return D1; case '2': return D2; case '3': return D3;
    case '4': return D4; case '5': return D5; case '6': return D6; case '7': return D7;
    case '8': return D8; case '9': return D9; case ':': return COLON;
  }
  return nullptr;
}

void dotWord(const char *word, int cy, float maxW, uint32_t on, uint32_t off) {
  const int n = strlen(word);
  const int cols = n * 6 - 1;
  const float p = min(5.4f, maxW / cols);
  const int r = p >= 4.5f ? 2 : 1;
  const float x0 = CX - (cols - 1) * p / 2, y0 = cy - 3 * p;
  for (int i = 0; i < n; i++) {
    const uint8_t *g = glyph(word[i]);
    if (!g) continue;
    for (int row = 0; row < 7; row++) {
      for (int col = 0; col < 5; col++) {
        const float x = x0 + (i * 6 + col) * p, y = y0 + row * p;
        if ((g[row] >> (4 - col)) & 1) dot(x, y, r, on);
        else dot(x, y, 1, off);
      }
    }
  }
}

// 丸い画面のその高さで収まる幅に合わせてフォントを選び、1行描く
void loadFonts() {
  const uint8_t *src[] = {NOTO_BOLD_20, NOTO_BOLD_15, NOTO_REGULAR_13, NOTO_REGULAR_11};
  const size_t len[] = {sizeof(NOTO_BOLD_20), sizeof(NOTO_BOLD_15), sizeof(NOTO_REGULAR_13),
                        sizeof(NOTO_REGULAR_11)};
  for (int i = 0; i < 4; i++) {
    fontData[i].set(src[i], len[i]);
    if (!fonts[i].loadFont(&fontData[i])) Serial.printf("[call] font %d load failed\n", i);
  }
}

void text(const char *s, int y, int size, uint32_t color) {
  const int start = size >= 17 ? 0 : size >= 14 ? 1 : size >= 12 ? 2 : 3;
  const float dy = y - CY;
  const int maxW = (int)(2 * sqrtf(max(0.0f, 120.0f * 120 - dy * dy))) - 30;
  for (int i = start; i < 4; i++) {
    canvas.setFont(&fonts[i]);
    if (canvas.textWidth(s) <= maxW) break;
  }
  canvas.setTextColor(c565(color));
  canvas.setTextDatum(middle_center);
  canvas.drawString(s, CX, y);
}

void ring(int r0, int r1, uint32_t color) { canvas.fillArc(CX, CY, r0, r1, 0, 360, c565(color)); }

// 12時から時計回りに span 度の弧（360度をまたぐ場合は分けて描く）
void arcFromTop(int r0, int r1, float span, uint32_t color) {
  if (span <= 0) return;
  if (span > 360) span = 360;
  if (span <= 90) {
    canvas.fillArc(CX, CY, r0, r1, 270, 270 + span, c565(color));
  } else {
    canvas.fillArc(CX, CY, r0, r1, 270, 360, c565(color));
    canvas.fillArc(CX, CY, r0, r1, 0, span - 90, c565(color));
  }
}

void drawQr(const char *url, int cy, int size) {
  canvas.qrcode(url, CX - size / 2, cy - size / 2, size, 2);
}

void fmtWait(char *buf, size_t n, uint32_t ms) {
  const unsigned long s = ms / 1000;
  snprintf(buf, n, "お待ち %lu:%02lu", s / 60, s % 60);
}

// ダイヤルでページが変わることを伝える矢印。左右の縁に、時計回りの短い弧の矢印を描く。
// ときどき時計回りに少し動かして、回す向きを伝える
void drawDialHint(uint32_t now, uint32_t color) {
  const float t = fmodf(now / 1000.0f, 2.4f);
  const float nudge = t < 0.6f ? 5.0f * sinf(t / 0.6f * PI) : 0;  // 2.4秒ごとに少し動く
  const uint16_t c = c565(color);
  for (int side = 0; side < 2; side++) {
    // LovyanGFX の角度は 3時 = 0度・時計回り。右（3時）と左（9時）
    const float mid = side == 0 ? 0 : 180;
    const float a0 = mid - 14 + nudge, a1 = mid + 10 + nudge;
    if (a0 < 0) {
      canvas.fillArc(CX, CY, 101, 103, 360 + a0, 360, c);
      canvas.fillArc(CX, CY, 101, 103, 0, a1, c);
    } else {
      canvas.fillArc(CX, CY, 101, 103, a0, a1, c);
    }
    // 矢じり（弧の終わりで、時計回りの向き）
    const float rad = a1 * DEG_TO_RAD, tip = (a1 + 8) * DEG_TO_RAD;
    canvas.fillTriangle(CX + 97 * cosf(rad), CY + 97 * sinf(rad), CX + 108 * cosf(rad),
                        CY + 108 * sinf(rad), CX + 102 * cosf(tip), CY + 102 * sinf(tip), c);
  }
}

// 製品カード（待っている間にダイヤルでめくる）
void drawCard(int idx) {
  const Card &c = CARDS[idx - 1];
  canvas.fillCircle(CX, CY, 120, c565(K_PAPER));
  rim([&](int) { return K_PAPER_TRACK; });
  text(c.name, 52, 18, K_PAPER_FG);
  text(c.desc, 78, 12, K_PAPER_SUB);
  drawQr(c.url, 136, 70);
  text("QRで詳しく見る", 188, 11, K_PAPER_SUB);
  // ページの位置（0 = 元の画面）
  const int n = CARD_COUNT + 1;
  for (int i = 0; i < n; i++) {
    dot(CX + (i - (n - 1) / 2.0f) * 10, 210, 2, i == idx ? K_ACCENT : K_PAPER_TRACK);
  }
}

void drawMenu(uint32_t now) {
  char vol[24], rot[32];
  snprintf(vol, sizeof(vol), "音量: %s", VOLUMES[volumeIdx].label);
  snprintf(rot, sizeof(rot), "画面の向き: %d°", screenRotation);
  auto label = [&](int i) {
    return i == M_VOLUME ? (const char *)vol : i == M_ROTATE ? (const char *)rot : MENU_LABELS[i];
  };
  const int pos = menuSel * 60 / M_COUNT;
  rim([&](int i) { return i == pos ? K_ACCENT : K_TRACK; });
  dotWord("MENU", 50, 90, K_ACCENT, K_TRACK);
  text(label((menuSel + M_COUNT - 1) % M_COUNT), 86, 13, K_SUB);
  text(label(menuSel), 120, 19, K_FG);
  text(label((menuSel + 1) % M_COUNT), 154, 13, K_SUB);
  const char *hint = !menuArmed ? "指を離してください"
                    : menuAdjust ? "回して調整・押して決定"
                                 : "回して選ぶ・押して決定";
  text(hint, 190, 11, menuAdjust ? K_ACCENT : K_SUB);
}

// 画面の向きに合わせて回して出す（0度ならそのまま）
void present() {
  if (screenRotation == 0) {
    canvas.pushSprite(0, 0);
  } else {
    canvas.pushRotateZoom(&M5Dial.Display, CX, CY, (float)screenRotation, 1.0f, 1.0f);
  }
}

void draw(uint32_t now) {
  const float ph = now / 1000.0f;
  const bool showToast = now < toastUntilMs;
  char buf[48];

  canvas.fillScreen(c565(K_BG));

  if (!hasConfig) {
    text("設定がありません", 110, 19, K_FG);
    text("include/secrets.h", 140, 12, K_SUB);
    present();
    return;
  }

  if (menuOpen) {
    drawMenu(now);
    present();
    return;
  }

  const bool cardsAvailable = state == State::Sent || state == State::Coming ||
                              state == State::Away || state == State::Sorry;
  if (cardIdx > 0) {
    drawCard(cardIdx);
  } else {
    switch (state) {
      case State::Idle: {
        canvas.fillCircle(CX, CY, 120, c565(K_ACCENT));
        const int br = 100 + (int)(3 * sinf(ph * 2));
        ring(br - 1, br + 1, mix(K_INK, K_ACCENT, 0.18f));
        rim([&](int i) { return mix(K_INK, K_ACCENT, 0.22f + 0.78f * spinGlow(i, now)); });
        text("ただいま席を外しています", 64, 12, mix(K_INK, K_ACCENT, 0.75f));
        dotWord("TAP", 104, 150, K_INK, mix(K_INK, K_ACCENT, 0.1f));
        text("制作者と話してみたい！", 150, 19, K_INK);
        text("Talk to the maker", 174, 11, mix(K_INK, K_ACCENT, 0.7f));
        break;
      }
      case State::Calling: {
        const float head = fmodf(ph * 50, 60);
        rim([&](int i) {
          const float d = fmodf(head - i + 60, 60);
          return d < 12 ? mix(K_ACCENT, K_BG, 1 - d / 12) : K_TRACK;
        });
        dotWord("CALLING", 102, 176, K_FG, K_TRACK);
        text("呼び出しています", 148, 19, K_FG);
        if (showToast) text(toastBuf, 178, 12, K_ACCENT);
        else text(attempts > 1 ? "もう少しお待ちください" : "Calling the maker", 178, 11, K_SUB);
        break;
      }
      case State::Sent: {
        const uint32_t waited = now - callMs;
        const bool paper = waited >= QR_AFTER_MS;
        if (paper) canvas.fillCircle(CX, CY, 120, c565(K_PAPER));
        const uint32_t fg = paper ? K_PAPER_FG : K_FG;
        const uint32_t track = paper ? K_PAPER_TRACK : K_TRACK;
        const int lit = (waited / 1000) % 60;
        rim([&](int i) { return i < lit ? K_ACCENT : i == lit ? fg : track; });
        fmtWait(buf, sizeof(buf), waited);
        if (!paper) {
          dotWord("SENT", 98, 130, K_ACCENT, K_TRACK);
          text("制作者に届きました", 144, 18, K_FG);
          text("まもなく戻ります", 168, 13, K_SUB);
          text(showToast ? toastBuf : buf, 196, 12, showToast ? K_ACCENT : K_SUB);
        } else {
          text("まもなく戻ります", 54, 15, K_PAPER_FG);
          drawQr(QR_URL, 118, 78);
          text(QR_CAPTION, 176, 12, K_PAPER_SUB);
          text(showToast ? toastBuf : buf, 196, 11, showToast ? K_ACCENT : K_PAPER_SUB);
        }
        break;
      }
      case State::Coming: {
        const float a = 0.55f + 0.45f * (0.5f + 0.5f * sinf(ph * 4));
        const uint32_t c = mix(K_ACCENT, K_BG, a);
        rim([&](int) { return c; });
        dotWord("COMING", 98, 170, K_ACCENT, K_TRACK);
        text("制作者が向かっています", 144, 18, K_FG);
        text("このままお待ちください", 168, 13, K_SUB);
        if (showToast) text(toastBuf, 196, 12, K_ACCENT);
        else text("On the way", 196, 11, K_SUB);
        break;
      }
      case State::Away: {
        // 外周：戻るまでの残り時間
        const int32_t left = (int32_t)(awayUntilMs - now);
        const int lit = left > 0 ? (int)ceilf(60.0f * left / awayTotalMs) : 0;
        canvas.fillCircle(CX, CY, 120, c565(K_PAPER));
        rim([&](int i) { return i < lit ? K_ACCENT : K_PAPER_TRACK; });
        if (awayAt[0]) {
          dotWord(awayAt, 58, 140, K_ACCENT, K_PAPER_TRACK);
          text("ごろ戻ります", 92, 15, K_PAPER_FG);
        } else {
          dotWord("BACK", 58, 110, K_ACCENT, K_PAPER_TRACK);
          awayText(buf, sizeof(buf));
          text(buf, 92, 15, K_PAPER_FG);
        }
        drawQr(QR_URL, 140, 70);
        text(showToast ? toastBuf : QR_CAPTION, 190, 12,
             showToast ? K_ACCENT : K_PAPER_SUB);
        text(QR_LABEL, 208, 10, K_PAPER_SUB);
        break;
      }
      case State::Sorry:
        canvas.fillCircle(CX, CY, 120, c565(K_PAPER));
        rim([&](int) { return K_PAPER_TRACK; });
        text("お待たせしています", 54, 15, K_PAPER_FG);
        drawQr(QR_URL, 118, 78);
        text(QR_CAPTION, 176, 12, K_PAPER_SUB);
        text(QR_LABEL, 196, 11, K_PAPER_SUB);
        break;
      case State::Failed: {
        canvas.fillCircle(CX, CY, 120, c565(K_ACCENT));
        const uint32_t dim = mix(K_INK, K_ACCENT, 0.22f);
        rim([&](int) { return dim; });
        dotWord("RETRY", 98, 150, K_INK, mix(K_INK, K_ACCENT, 0.1f));
        text("うまく届きませんでした", 144, 17, K_INK);
        text("もう一度タッチしてください", 168, 13, mix(K_INK, K_ACCENT, 0.8f));
        text("Please tap again", 192, 11, mix(K_INK, K_ACCENT, 0.7f));
        break;
      }
      case State::Present: {
        rim([&](int i) { return mix(K_FG, K_BG, 0.35f + 0.65f * spinGlow(i, now)); });
        dotWord("HELLO", 98, 150, K_FG, K_TRACK);
        text("制作者がいます", 144, 19, K_FG);
        text("お気軽にお声がけください", 168, 13, K_SUB);
        if (showToast) text(toastBuf, 196, 12, K_ACCENT);
        else text("Talk to us", 196, 11, K_SUB);
        break;
      }
    }
  }

  if (cardsAvailable) drawDialHint(now, K_ACCENT);

  // Wi-Fi 未接続（担当者向けの小さな点）
  if (WiFi.status() != WL_CONNECTED) {
    const bool onAccent = cardIdx == 0 && (state == State::Idle || state == State::Failed);
    canvas.fillCircle(CX, 229, 3, c565(onAccent ? K_INK : K_SUB));
  }

  // 押したときの演出：オレンジの丸が沈み込み、波紋が広がる
  const float p = (now - fxMs) / 260.0f;
  if (fxMs && p < 1) {
    if (fxSink) canvas.fillCircle(CX, CY, (int)(120 * (1 - p)), c565(mix(K_ACCENT, K_BG, 1 - p)));
    const int r = 30 + (int)(90 * p);
    const int w = 1 + (int)(6 * (1 - p));
    ring(max(0, r - w), r, mix(K_ACCENT, K_BG, 1 - p));
  }

  // 担当者の長押しの進み具合
  if (wasDown && !gestureDone && staffResettable()) {
    const uint32_t h = now - downMs;
    if (h > STAFF_HOLD_START_MS) {
      const bool paper = cardIdx > 0 || state == State::Away ||
                         (state == State::Sent && now - callMs >= QR_AFTER_MS);
      const uint32_t c = paper ? K_PAPER_FG : K_FG;
      arcFromTop(110, 115,
                 360.0f * (h - STAFF_HOLD_START_MS) / (STAFF_HOLD_MS - STAFF_HOLD_START_MS), c);
      text("長押しで待機に戻す", 214, 10, c);
    }
  }

  present();
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;  // M5Unified 0.2.x は既定で Serial.begin() を呼ばないため明示する
  M5Dial.begin(cfg, true, false);  // encoder: on, RFID: off

  prefs.begin("callbtn", false);
  volumeIdx = prefs.getUChar("vol", 1) % VOLUME_COUNT;
  M5Dial.Speaker.setVolume(VOLUMES[volumeIdx].value);
  screenRotation = prefs.getUShort("rot", 0) % 360;

  canvas.setColorDepth(16);
  canvas.createSprite(240, 240);
  loadFonts();

  hasConfig = strlen(WIFI_SSID) > 0 && strlen(NTFY_TOPIC) > 0;
  snprintf(replyTopic, sizeof(replyTopic), "%s%s", NTFY_TOPIC, REPLY_SUFFIX);
  if (hasConfig) {
    startWifi();
    xTaskCreatePinnedToCore(netTask, "net", 12 * 1024, nullptr, 1, nullptr, 0);
  } else {
    WiFi.mode(WIFI_OFF);
  }
  lastEnc = M5Dial.Encoder.read();
  stateSinceMs = millis();
  lastWifiAttemptMs = stateSinceMs;

  Serial.printf("[call] boot, config %s, free heap %lu\n", hasConfig ? "ok" : "missing",
                (unsigned long)ESP.getFreeHeap());
}

void loop() {
  M5Dial.update();
  const uint32_t now = millis();

  if (hasConfig) {
    updateWifi(now);
    updateInput(now);
    updateState(now);
  }
  updateSound(now);

  if (now - lastDrawMs >= DRAW_INTERVAL_MS) {
    lastDrawMs = now;
    draw(now);
  }

  delay(2);
}
