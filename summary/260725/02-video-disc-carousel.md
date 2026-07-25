# 曲選択画面を CD ディスク風サムネイルカルーセルにした（#193）

テキスト縦リスト（#175/#189）だった動画（曲）選択画面を、選択中の曲のサムネイルを
CD ディスク風に中央へ出すカルーセルへ刷新した。**⚠ 実機未確認**（確認項目は Issue #193）。

## 操作の変更

| 操作 | 旧（#175） | 新（#193） |
|---|---|---|
| 曲送り | 左半分タップ＝次へ（単方向巡回） | **左右スワイプ**（双方向巡回） |
| 決定 | 右半分タップ | **どこでもタップ** |
| メニュー復帰 | 長押し | 長押し（変更なし） |

## 仕組み

- **サムネイル**: SD 上の既存 `frames.bin` から**中間フレーム（frames/2）を1枚だけ**読んで
  デコードする。索引は該当 entry の 8 バイトだけを seek で読み、検証は既存の純粋関数
  `video_pack_entry`（frame_count=1 / idx=0 として渡す）に委譲。**PC 側の再変換・SD への
  再転送は一切不要**（今入っている 13 曲がそのまま対象）。pack の無い旧アセットは
  `video_frame_path` の中間フレームを `drawJpgFile`（互換）。
- **CD 表現**: 132×132 の PSRAM Sprite に JPEG を scale=132/180 で中央クロップ描画
  （320×240 レターボックスの上下 30px 黒帯がちょうど円の外に出る倍率）→ 行単位の円形マスク
  ＋中心穴＋外周リム → 160×160 キャンバスへ `pushRotateZoom`（約20秒/周）＋ sin 浮遊（±5px・
  2.6秒周期）→ 一括 push。合成コストは `[video] disc frame=..us` で間引き実測ログが出る。
- **スワイプ検出**: `gesture.h` の `touch_update` に X 座標を追加（デフォルト引数で後方互換）。
  離した瞬間の移動量 ±50px 以上で SwipeLeft/Right。ドラッグ中は LongPress 抑制、
  一度大きく払った押下は離しても Tap を出さない（決定の誤爆防止・moved ラッチ）。
  `SceneDef` に省略可の `onSwipe` を追加し、loop から対応シーンだけに配送。

## 資源管理（#128 の轍を踏まない）

ディスク用 Sprite 2枚（計約86KB PSRAM）は**選択画面にいる間だけ**確保。
`videoStartPlayback` の先頭（音声 7MB 級の確保前）と `videoExit` で必ず `deleteSprite`。

## 消したもの

- `video_is_decide_tap`（左右二分の決定判定）
- `video_scroll_top` / `kVideoVisibleRows`（8行スクロール窓・#189）＋対応テスト
  → 代わりに `video_list_prev`（逆巡回）と gesture のスワイプ判定テストを追加

## reviewer 指摘の反映

- 🔴 `videoThumbInto` の frames 上限欠落（frames×8 の size_t 溢れ）→ `kVideoMaxFrames` を
  `video.h` に一本化し再生側・サムネ側の両方で課す
- 🟡 pack 名検証のインラインコピー → `video_name_valid`（純粋関数・テスト済み）へ一本化
- 🟡 ヘッダ文字列が 344px で画面幅超え → 全角15文字に短縮
- 🟡 選択画面の合成コスト未計測 → 間引きログ追加（実機確認時に読む）
- nit: 浮遊の sin に渡す t の剰余化 / サムネ失敗時のログ追加

## 見送ったもの（フォローアップ候補）

- pack リーダの共通層化（`videoOpenPack`+`videoDrawFromPack` とサムネ経路の重複解消）。
  再生側は「File を開いたまま保持」、サムネ側は「open→read→close」で寿命が違い、
  素直に共通化できない。上限・名前検証の一本化で drift の主因は塞いだ。
- レイアウト算術（scale/dx/dy・円境界）の純粋関数化と native テスト
- サムネイルの PSRAM キャッシュ（現状は曲送りのたびに SD 再読み・約60ms 想定）
