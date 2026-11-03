展示会ブースの呼び出しボタン **oyobidashi** の、M5Dial 用ファームウェアです。

> ⚠️ このリリースは、作者が実機で動作を確かめていないトライアル版です。ファームウェア、ブラウザでの書き込み、設定モードのどれも「おまけ」として用意したもので、うまく動かないことがあります。確実に使いたいときは、作者が実機で確かめた [自分でビルドする手順](https://github.com/y-morishita-biz/oyobidashi/blob/main/docs/setup/firmware.md) を使ってください。

## 書き込みかた

いちばん手軽なのは、パソコンの Chrome か Edge で [書き込みページ](https://y-morishita-biz.github.io/oyobidashi/) を開き、「書き込む」を押す方法です。

コマンドで書き込むときは、下の `oyobidashi-call-button-*.bin` をダウンロードして、esptool で 0x0 番地に書き込みます。この方法では、本体に保存した設定は消えます。

```sh
pip install esptool
esptool.py --chip esp32s3 write_flash 0x0 oyobidashi-call-button-<バージョン>.bin
```

書き込んだあと、テザリングと ntfy のトピック名の設定は、M5Dial とスマホだけでできます。手順は [かんたんセットアップガイド](https://github.com/y-morishita-biz/oyobidashi/blob/main/docs/setup/quick-start.md) を見てください。

## 配布版の中身

- Wi-Fi のパスワードやトピック名は入っていません。はじめて起動すると「設定モード」になり、スマホから設定します
- 製品カードと QR コードのサンプルは入っていません（QR コードの行き先は設定モードで入れられます）。製品カードを使いたいときや、画面の文言を変えたいときは、[自分でビルド](https://github.com/y-morishita-biz/oyobidashi/blob/main/docs/setup/firmware.md) してください
- 新しい版に入れ替えるときは、書き込みページで「Erase device」のチェックを外すと設定が残ります

`SHA256SUMS.txt` は、ダウンロードしたファイルが壊れていないかを確かめるためのものです。

> 本ソフトウェアは MIT ライセンスで、無保証で提供しています。使用によって生じた損害やトラブルについて、作者は責任を負いません。
