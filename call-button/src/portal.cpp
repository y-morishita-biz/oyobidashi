// 設定モード（portal.h を参照）
#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>  // esp_random()

namespace {

const IPAddress AP_IP(192, 168, 4, 1);
constexpr const char *AP_PREFIX = "oyobidashi-";
constexpr const char *TOPIC_PREFIX = "oyobidashi-";
constexpr int MAX_NETWORKS = 20;

DNSServer dns;
WebServer server(80);

Config cur;
bool hasCur = false;
const char *fwVersion = "";
Config saved;
bool savedReady = false;
String networks;  // <option> を並べたもの（近くのテザリングの候補）

char apSsid[24] = "";
char apPass[12] = "";
char wifiQr[64] = "";
char url[24] = "";

// 紛らわしい文字（0 / o / 1 / l など）を除いた、画面から読み写しやすい文字
void randomChars(char *buf, int n, const char *set) {
  const int len = strlen(set);
  for (int i = 0; i < n; i++) buf[i] = set[esp_random() % len];
  buf[n] = '\0';
}

void randomTopic(char *buf, size_t size) {
  char tail[17];
  randomChars(tail, 16, "0123456789abcdef");
  snprintf(buf, size, "%s%s", TOPIC_PREFIX, tail);
}

String esc(const char *s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += *s;
    }
  }
  return o;
}

// --- Web ページ --------------------------------------------------------------

const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>oyobidashi の設定</title>
<style>
:root{--accent:#FF5A1F;--ink:#111;--sub:#707070;--line:#e2e2e2;--bg:#fff}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.6 -apple-system,BlinkMacSystemFont,"Hiragino Sans","Noto Sans JP",sans-serif}
main{max-width:480px;margin:0 auto;padding:24px 16px 40px}
h1{font-size:22px;margin:0 0 4px}
.lead{color:var(--sub);margin:0 0 24px;font-size:14px}
label{display:block;font-weight:700;margin:20px 0 6px}
.opt{font-weight:400;color:var(--sub);font-size:13px}
input[type=text],input[type=password],input[type=url]{width:100%;font-size:16px;padding:12px;border:1px solid #bbb;border-radius:10px;background:#fff;color:var(--ink)}
input:focus{outline:2px solid var(--accent);border-color:var(--accent)}
.help{color:var(--sub);font-size:13px;margin:6px 0 0}
.row{display:flex;gap:8px;align-items:center}
.row input{flex:1}
.check{display:flex;gap:6px;align-items:center;font-size:14px;margin-top:8px;color:var(--sub)}
button{font:inherit;border:0;border-radius:999px;cursor:pointer}
.primary{display:block;width:100%;margin-top:32px;padding:14px;background:var(--accent);color:#140700;font-weight:700;font-size:17px}
.secondary{padding:10px 14px;background:#f0f0f0;color:var(--ink);font-size:14px;white-space:nowrap}
.error{background:#fff1eb;border:1px solid var(--accent);border-radius:10px;padding:12px;margin:0 0 16px}
.box{font:600 17px/1.5 ui-monospace,Menlo,monospace;background:#f6f6f6;border-radius:10px;padding:14px;word-break:break-all;user-select:all;-webkit-user-select:all}
ol{padding-left:1.4em}li{margin:6px 0}
hr{border:0;border-top:1px solid var(--line);margin:28px 0 12px}
footer{color:var(--sub);font-size:12px}
</style></head><body><main>
)HTML";

const char PAGE_FOOT[] PROGMEM = R"HTML(
</main></body></html>
)HTML";

String footer() {
  String s = F("<hr><footer>oyobidashi call-button ");
  s += esc(fwVersion);
  s += F("</footer>");
  return s;
}

void sendPage(const String &body) {
  String html = FPSTR(PAGE_HEAD);
  html += body;
  html += footer();
  html += FPSTR(PAGE_FOOT);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html; charset=utf-8", html);
}

void sendForm(const Config &c, const char *error) {
  String b;
  b += F("<h1>oyobidashi の設定</h1>"
         "<p class=\"lead\">ブースのテザリングと、通知の宛先（ntfy のトピック名）を入れて保存してください。</p>");
  if (error && *error) {
    b += F("<p class=\"error\">");
    b += error;
    b += F("</p>");
  }
  b += F("<form method=\"post\" action=\"/save\" autocomplete=\"off\">");

  b += F("<label for=\"ssid\">テザリングの名前（SSID）</label>"
         "<input type=\"text\" id=\"ssid\" name=\"ssid\" maxlength=\"32\" required list=\"nets\" "
         "autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\" value=\"");
  b += esc(c.ssid);
  b += F("\"><datalist id=\"nets\">");
  b += networks;
  b += F("</datalist><p class=\"help\">入力欄を押すと、近くで見つかった Wi-Fi が候補に出ます。"
         "2.4GHz のテザリングだけにつながります。大文字・小文字も同じにしてください。</p>");

  b += F("<label for=\"pass\">テザリングのパスワード</label>"
         "<input type=\"password\" id=\"pass\" name=\"pass\" maxlength=\"63\" "
         "autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\"");
  if (hasCur && cur.pass[0]) b += F(" placeholder=\"変えないときは空のまま\"");
  else b += F(" required minlength=\"8\"");
  b += F("><label class=\"check\"><input type=\"checkbox\" "
         "onclick=\"pass.type=this.checked?'text':'password'\">パスワードを表示する</label>");

  b += F("<label for=\"topic\">ntfy のトピック名</label>"
         "<div class=\"row\"><input type=\"text\" id=\"topic\" name=\"topic\" maxlength=\"64\" required "
         "pattern=\"[A-Za-z0-9_\\-]+\" autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\" value=\"");
  b += esc(c.topic);
  b += F("\"><button type=\"button\" class=\"secondary\" onclick=\"newTopic()\">作り直す</button></div>"
         "<p class=\"help\">通知の合言葉です。最初からランダムな名前が入っています。"
         "このあと、通知を受け取るスマホの ntfy アプリに同じ名前を入れます。人に見せないでください。</p>");

  b += F("<label for=\"qr\">QR コードの行き先 <span class=\"opt\">（なくてもよい）</span></label>"
         "<input type=\"url\" id=\"qr\" name=\"qr\" maxlength=\"200\" placeholder=\"https://\" "
         "autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\" value=\"");
  b += esc(c.qrUrl);
  b += F("\"><p class=\"help\">来場者を待たせているときに、画面に QR コードで出します（SNS やサイトなど）。"
         "空にすると QR コードは出しません。</p>");

  b += F("<label for=\"qrlabel\">QR コードの下の文字 <span class=\"opt\">（なくてもよい・半角英数字）</span></label>"
         "<input type=\"text\" id=\"qrlabel\" name=\"qrlabel\" maxlength=\"32\" placeholder=\"example.com\" "
         "pattern=\"[ -~]*\" autocapitalize=\"off\" autocorrect=\"off\" spellcheck=\"false\" value=\"");
  b += esc(c.qrLabel);
  b += F("\">");

  b += F("<button type=\"submit\" class=\"primary\">保存して再起動する</button></form>"
         "<script>function newTopic(){var a=new Uint8Array(8);"
         "(window.crypto||window.msCrypto).getRandomValues(a);"
         "document.getElementById('topic').value='oyobidashi-'+Array.from(a,function(x){"
         "return('0'+x.toString(16)).slice(-2)}).join('')}</script>");
  sendPage(b);
}

void sendSaved(const Config &c) {
  String b;
  b += F("<h1>保存しました</h1>"
         "<p class=\"lead\">M5Dial が再起動して、テザリングにつながります。この画面は閉じてかまいません。</p>"
         "<p><b>ntfy のトピック名</b></p><p class=\"box\" id=\"t\">");
  b += esc(c.topic);
  b += F("</p><p><button type=\"button\" class=\"secondary\" onclick=\"copyTopic()\" id=\"cp\">コピーする</button></p>"
         "<p><b>次にやること</b></p><ol>"
         "<li>通知を受け取るスマホで ntfy アプリを開き、「+」から上のトピック名を購読する</li>"
         "<li>M5Dial の画面のいちばん下に小さな点が出ていなければ、テザリングにつながっています</li>"
         "<li>M5Dial の画面に触れて、通知が届くか確かめる</li></ol>"
         "<p class=\"help\">トピック名を忘れたときや、テザリングを変えたときは、M5Dial の画面を押したまま"
         "USB ケーブルを挿し直すと、この設定画面にもう一度入れます。</p>"
         "<script>function copyTopic(){var r=document.createRange();r.selectNodeContents(document.getElementById('t'));"
         "var s=getSelection();s.removeAllRanges();s.addRange(r);var ok=false;try{ok=document.execCommand('copy')}catch(e){}"
         "document.getElementById('cp').textContent=ok?'コピーしました':'選んだ文字を長押ししてコピーしてください'}</script>");
  sendPage(b);
}

// --- 入力の確認 --------------------------------------------------------------

bool validTopic(const String &t) {
  if (t.isEmpty() || t.length() > 64) return false;
  for (unsigned i = 0; i < t.length(); i++) {
    const char ch = t[i];
    if (!isalnum((unsigned char)ch) && ch != '-' && ch != '_') return false;
  }
  return true;
}

bool printableAscii(const String &s) {
  for (unsigned i = 0; i < s.length(); i++) {
    const char ch = s[i];
    if (ch < 0x20 || ch > 0x7E) return false;
  }
  return true;
}

void handleRoot() { sendForm(cur, nullptr); }

void handleSave() {
  Config c = {};
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String topic = server.arg("topic");
  String qr = server.arg("qr");
  String label = server.arg("qrlabel");
  ssid.trim();
  topic.trim();
  qr.trim();
  label.trim();

  // 入力し直してもらうときに、入れた値を残しておく（パスワードは残さない）
  strlcpy(c.ssid, ssid.c_str(), sizeof(c.ssid));
  strlcpy(c.topic, topic.c_str(), sizeof(c.topic));
  strlcpy(c.qrUrl, qr.c_str(), sizeof(c.qrUrl));
  strlcpy(c.qrLabel, label.c_str(), sizeof(c.qrLabel));

  const bool keepPass = pass.isEmpty() && hasCur && cur.pass[0];
  const char *error = nullptr;
  if (ssid.isEmpty() || ssid.length() > 32) {
    error = "テザリングの名前（SSID）を入れてください（32文字まで）。";
  } else if (!keepPass && (pass.length() < 8 || pass.length() > 63)) {
    error = "テザリングのパスワードは8〜63文字です。";
  } else if (!validTopic(topic)) {
    error = "トピック名は、半角の英数字・ハイフン・アンダーバーで64文字までにしてください。";
  } else if (topic.length() < 16) {
    error = "トピック名が短すぎます。推測されないように、16文字以上にしてください（「作り直す」で作れます）。";
  } else if (!qr.isEmpty() && !(qr.startsWith("https://") || qr.startsWith("http://"))) {
    error = "QR コードの行き先は https:// で始まる URL にしてください。";
  } else if (qr.length() > 200) {
    error = "QR コードの行き先が長すぎます（200文字まで）。";
  } else if (!printableAscii(label) || label.length() > 32) {
    error = "QR コードの下の文字は、半角の英数字・記号で32文字までにしてください。";
  }
  if (error) {
    Serial.println("[setup] form rejected");
    sendForm(c, error);
    return;
  }

  strlcpy(c.pass, keepPass ? cur.pass : pass.c_str(), sizeof(c.pass));
  saved = c;
  savedReady = true;
  Serial.println("[setup] saved");  // 値はログに出さない
  sendSaved(c);
}

// スマホの「Wi-Fi にログイン」の確認（captive portal の検出）を、すべて設定ページに向ける
void handleOther() {
  server.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
  server.send(302, "text/plain", "");
}

void scanNetworks() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  const int n = WiFi.scanNetworks();
  Serial.printf("[setup] %d networks found\n", n);
  networks = "";
  int added = 0;
  for (int i = 0; i < n && added < MAX_NETWORKS; i++) {
    const String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (int j = 0; j < i; j++) {
      if (WiFi.SSID(j) == s) {
        dup = true;
        break;
      }
    }
    if (dup) continue;
    networks += "<option value=\"" + esc(s.c_str()) + "\">";
    added++;
  }
  WiFi.scanDelete();
}

}  // namespace

void portalBegin(const Config &current, bool hasCurrent, const char *version) {
  cur = current;
  hasCur = hasCurrent;
  fwVersion = version;
  if (!cur.topic[0]) randomTopic(cur.topic, sizeof(cur.topic));

  scanNetworks();  // Wi-Fi の電波を使うので、乱数もここから先で作る

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apSsid, sizeof(apSsid), "%s%02X%02X", AP_PREFIX, mac[4], mac[5]);
  randomChars(apPass, 8, "23456789abcdefghijkmnpqrstuvwxyz");
  snprintf(wifiQr, sizeof(wifiQr), "WIFI:T:WPA;S:%s;P:%s;;", apSsid, apPass);
  snprintf(url, sizeof(url), "http://%s/", AP_IP.toString().c_str());

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid, apPass);
  dns.start(53, "*", AP_IP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleOther);
  server.begin();
  Serial.printf("[setup] access point %s started\n", apSsid);
}

void portalLoop() {
  dns.processNextRequest();
  server.handleClient();
}

bool portalPhoneConnected() { return WiFi.softAPgetStationNum() > 0; }

bool portalTakeSaved(Config &out) {
  if (!savedReady) return false;
  savedReady = false;
  out = saved;
  return true;
}

const char *portalApSsid() { return apSsid; }
const char *portalApPass() { return apPass; }
const char *portalWifiQr() { return wifiQr; }
const char *portalUrl() { return url; }
