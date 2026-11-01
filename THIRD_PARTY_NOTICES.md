# サードパーティのライセンス

このリポジトリに含まれる、作者以外が権利を持つもの（またはそれから生成したもの）の一覧です。

## Noto Sans JP

- 使っている場所：`call-button/src/fonts_noto.h`（画面で使う文字だけを取り出して VLW 形式にしたもの）、`plate/` の 3D モデルの文字
- 著作権：Copyright 2014-2021 Adobe (http://www.adobe.com/), with Reserved Font Name 'Source'
- ライセンス：[SIL Open Font License 1.1](https://openfontlicense.org/open-font-license-official-text/)
- 入手先：https://fonts.google.com/noto/specimen/Noto+Sans+JP

`fonts_noto.h` は OFL 1.1 のもとで配布する派生物です。MIT ライセンス（LICENSE）の対象ではありません。

## Let's Encrypt ルート証明書（ISRG Root X1 / X2）

- 使っている場所：`call-button/src/certs.h`
- ntfy.sh の HTTPS 証明書を確かめるための公開のルート証明書です。秘密情報ではありません
- 入手先：https://letsencrypt.org/certificates/

## ビルド時に取得するライブラリ（リポジトリには含まない）

PlatformIO がビルドのときに取得します。それぞれのライセンスに従います。

- [M5Dial](https://github.com/m5stack/M5Dial) / [M5Unified](https://github.com/m5stack/M5Unified) / [M5GFX](https://github.com/m5stack/M5GFX)（MIT）
- [Arduino-ESP32](https://github.com/espressif/arduino-esp32)（LGPL-2.1）
- docs のプロトタイプは [three.js](https://threejs.org/)（MIT）と Google Fonts を CDN から読み込みます
