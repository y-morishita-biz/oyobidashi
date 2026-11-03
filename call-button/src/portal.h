// 設定モード：M5Dial が Wi-Fi のアクセスポイントになり、スマホのブラウザから
// テザリングの SSID・パスワード、ntfy のトピック名、QR の行き先を入れてもらう。
// 画面の描画は main.cpp、ここは Wi-Fi・Web ページ・入力の確認だけを受け持つ。
// （Web ページの日本語は M5Dial の画面のフォントに入れないので、main.cpp とは分けている）
#pragma once
#include <Arduino.h>

struct Config {
  char ssid[33];      // テザリングの名前（Wi-Fi の規格で最大32バイト）
  char pass[64];      // テザリングのパスワード（8〜63文字）
  char topic[65];     // ntfy のトピック名（ntfy の上限は64文字）
  char qrUrl[201];    // 待たせているときに出す QR の行き先。空なら QR を出さない
  char qrLabel[33];   // QR の下の小さな文字（英数字のみ。画面のフォントに日本語が入っていないため）
};

// アクセスポイントを立ち上げ、Web ページを出す。current は今の設定（入力欄の初期値になる）
void portalBegin(const Config &current, bool hasCurrent, const char *version);
// loop() から呼ぶ
void portalLoop();
// スマホがアクセスポイントにつながっているか
bool portalPhoneConnected();
// 保存ボタンが押されたら true を返し、out に新しい設定を入れる（1回だけ）
bool portalTakeSaved(Config &out);

// 画面に出すアクセスポイントの名前・パスワード・接続用 QR の中身
const char *portalApSsid();
const char *portalApPass();
const char *portalWifiQr();
const char *portalUrl();
